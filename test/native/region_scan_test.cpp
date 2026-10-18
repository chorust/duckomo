#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "core_functions_extension.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/metadata.hpp"
#include "duckomo/reader.hpp"
#include "duckomo_extension.hpp"
#include "query_result_compat.hpp"

extern "C" {
#include "om_variable.h"
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/resource.h>
#include <utility>
#include <vector>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb

namespace {
using namespace duckdb;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::filesystem::path SamplingFixture(const std::string &name) {
#ifdef DUCKOMO_SAMPLING_FIXTURE_PATH
	return std::filesystem::path(DUCKOMO_SAMPLING_FIXTURE_PATH) / name;
#else
	return std::filesystem::path("test/data/sampling") / name;
#endif
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		result.push_back(character);
		if (character == '\'') result.push_back('\'');
	}
	result.push_back('\'');
	return result;
}

std::unique_ptr<duckomo_test::CollectedQueryResult> Query(Connection &connection, const std::string &sql) {
	return duckomo_test::Query(connection, sql);
}

std::string SmallRegionScan(const std::string &regions, bool include_source = false) {
	return "read_om_regions('test/data/regions/regions.om', regions:=" + regions +
	       ", region_crs:='grid_lonlat', dimensions:=['member','time','lat','lon'], "
	       "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	       "spatial_axes:=['lat','lon'], axes:={"
	       "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	       "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}}" +
	       (include_source ? ", include_source:=true)" : ")");
}

std::string LastScanMetrics(Connection &connection) {
	return Query(connection, "SELECT metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1")
	    ->GetValue(0, 0).GetValue<std::string>();
}

std::string ValueTotals(const std::string &metrics) {
	const auto start = metrics.find("\"value_totals\":");
	if (start == std::string::npos) throw std::runtime_error("scan metrics omit value_totals");
	const auto end = metrics.find('}', start);
	if (end == std::string::npos) throw std::runtime_error("scan metrics value_totals is incomplete");
	return metrics.substr(start, end - start + 1);
}

std::uint64_t JsonUintField(const std::string &json, const std::string &key) {
	const auto marker = "\"" + key + "\":";
	const auto start = json.find(marker);
	if (start == std::string::npos) throw std::runtime_error("metrics omit " + key);
	const auto value_start = start + marker.size();
	const auto value_end = json.find_first_not_of("0123456789", value_start);
	return std::stoull(json.substr(value_start, value_end - value_start));
}

std::uint64_t ProcessPeakRssBytes() {
	struct rusage usage {};
	if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
#if defined(__APPLE__)
	return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
	return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
}

std::string ReadWkbHex(const std::string &path) {
	std::ifstream input(path, std::ios::binary);
	if (!input.good()) throw std::runtime_error("cannot read frozen region WKB " + path);
	const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	std::ostringstream hex;
	hex << std::hex << std::setfill('0');
	for (const auto byte : bytes) hex << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(byte));
	return hex.str();
}

std::pair<std::uint64_t, std::uint64_t> FirstValueLookupRange(Connection &connection,
                                                             const std::filesystem::path &path) {
	duckomo::OmV3Reader reader(duckomo::LocalFile::Open(*connection.context, path.string()));
	const auto tree = duckomo::ReadMetadataTree(reader);
	Require(!tree.arrays.empty(), "corrupt-value fixture has no value array");
	const auto &array = tree.arrays.front();
	const auto *variable = om_variable_init(array.metadata_owner->Data());
	Require(variable != nullptr, "corrupt-value fixture metadata could not be opened by the official reader");
	const auto *array_metadata = reinterpret_cast<const OmVariableArrayV3_t *>(variable);
	Require(om_variable_get_dimensions_count(variable) > 0 && array_metadata->lut_size > 0,
	        "corrupt-value fixture has no dimensions or compressed lookup table");
	return {array_metadata->lut_offset, array_metadata->lut_size};
}

void RunRegionResourceProbe(Connection &connection, const std::string &scenario,
                            std::uint64_t startup_peak_rss_bytes) {
	if (scenario != "small" && scenario != "large") {
		throw std::invalid_argument("resource probe scenario must be small or large");
	}
	const std::uint64_t side = scenario == "large" ? 1024 : 256;
	const double origin = scenario == "large" ? -64.0 : -32.0;
	const double spacing = scenario == "large" ? 0.125 : 0.25;
	const auto file = "test/data/grids/memory-" + scenario + ".om";
	const auto region_wkb = ReadWkbHex("test/data/regions/resource-memory-center.wkb");
	Query(connection, "SET threads=1; SET duckomo_max_threads=1");
	const auto result = Query(
	    connection,
	    "SELECT count(value)::UBIGINT,sum(value)::DOUBLE,min(value)::DOUBLE,max(value)::DOUBLE FROM "
	    "read_om_regions(" + SqlLiteral(file) + ", regions:=[{'id':'center','wkb':from_hex(" +
	        SqlLiteral(region_wkb) + ")}], region_crs:='grid_lonlat', "
	    "dimensions:=map(['value'],[['latitude_axis','longitude_axis']]), "
	    "grid:={'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1',"
	    "'earth':{'model':'sphere','radius_m':6371229.0},"
	    "'layout':{'nx':" + std::to_string(side) + ",'ny':" + std::to_string(side) + ",'order':'separate'},"
	    "'parameters':{'x0':" + std::to_string(origin) + ",'y0':" + std::to_string(origin) +
	    ",'dx':" + std::to_string(spacing) + ",'dy':" + std::to_string(spacing) +
	    ",'north_pole_latitude':90.0,'north_pole_longitude':0.0,'rotation':0.0}}, "
	    "spatial_axes:=['latitude_axis','longitude_axis'])");
	if (result->RowCount() != 1 || result->GetValue(0, 0).GetValue<std::uint64_t>() != 1 ||
	    result->GetValue(1, 0).GetValue<double>() != 0.0 || result->GetValue(2, 0).GetValue<double>() != 0.0 ||
	    result->GetValue(3, 0).GetValue<double>() != 0.0) {
		throw std::runtime_error("small and large region resource probes must return the same single value");
	}
	const auto metrics = LastScanMetrics(connection);
	std::cout << "{\"scenario\":\"memory_" << scenario << "\",\"startup_peak_rss_bytes\":"
	          << startup_peak_rss_bytes << ",\"result_rows\":1,\"result_sum\":0,\"result_min\":0,"
	             "\"result_max\":0,\"metrics\":"
	          << metrics << "}\n";
}

void TestRepeatedCoordinatesAndEmptyRegion(Connection &connection) {
	Query(connection,
	      "SET VARIABLE region_input=["
	      "{'id':'A','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))'))},"
	      "{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))'))},"
	      "{'id':'CopyA','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))'))}]");
	const auto result = Query(
	    connection,
	    "SELECT region_id, count(*)::UBIGINT, count(DISTINCT om_source.logical_index)::UBIGINT, "
	    "count(DISTINCT (lat,lon))::UBIGINT FROM read_om_regions(" + SqlLiteral("test/data/regions/regions.om") +
	        ", regions:=getvariable('region_input'), region_crs:='grid_lonlat', "
	        "dimensions:=['member','time','lat','lon'], "
	        "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	        "spatial_axes:=['lat','lon'], axes:={" 
	        "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'}," 
	        "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}} , include_source:=true) "
	        "GROUP BY region_id ORDER BY region_id");
	Require(result->RowCount() == 2, "an envelope-empty middle region must be skipped without ending the scan");
	for (idx_t row = 0; row < result->RowCount(); ++row) {
		Require(result->GetValue(1, row).GetValue<std::uint64_t>() == 54 &&
		            result->GetValue(2, row).GetValue<std::uint64_t>() == 54 &&
		            result->GetValue(3, row).GetValue<std::uint64_t>() == 9,
		        "same coordinates at distinct source positions and duplicate region IDs remain separate rows");
	}
	const auto shared_positions = Query(
	    connection,
	    "SELECT count(*)::UBIGINT FROM (SELECT om_source.logical_index "
	    "FROM read_om_regions('test/data/regions/regions.om', regions:=getvariable('region_input'), "
	    "region_crs:='grid_lonlat', dimensions:=['member','time','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}}, include_source:=true) "
	    "GROUP BY om_source.logical_index HAVING count(DISTINCT region_id)=2)");
	Require(shared_positions->GetValue(0, 0).GetValue<std::uint64_t>() == 54,
	        "overlapping regions must retain the same source position once for each region identity");
}

void TestTenRegionMixedShapes(Connection &connection) {
	Query(connection,
	      "SET VARIABLE many_regions=["
	      "{'id':'A','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))'))},"
	      "{'id':'CopyA','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))'))},"
	      "{'id':'Corner','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((101.9 21.9,102.1 21.9,102.1 22.1,101.9 22.1,101.9 21.9))'))},"
	      "{'id':'Empty','wkb':ST_AsWKB(ST_GeomFromText('POLYGON EMPTY'))},"
	      "{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100.8 20.8,101.2 20.8,101.2 21.2,100.8 21.2,100.8 20.8),"
	      "(100.9 20.9,101.1 20.9,101.1 21.1,100.9 21.1,100.9 20.9))'))},"
	      "{'id':'Multi','wkb':ST_AsWKB(ST_GeomFromText('MULTIPOLYGON(((99.9 19.9,100.1 19.9,100.1 20.1,99.9 20.1,99.9 19.9)),"
	      "((101.9 21.9,102.1 21.9,102.1 22.1,101.9 22.1,101.9 21.9)))'))},"
	      "{'id':'Narrow','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((99.9 19.9,100.1 19.9,100.1 20.1,99.9 20.1,99.9 19.9))'))},"
	      "{'id':'Outside','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((110 30,111 30,111 31,110 31,110 30))'))},"
	      "{'id':'OtherCorner','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((99.9 21.9,100.1 21.9,100.1 22.1,99.9 22.1,99.9 21.9))'))},"
	      "{'id':'Repeat','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))'))}]");
	const auto result = Query(connection,
	                          "SELECT region_id,count(*)::UBIGINT FROM " +
	                              SmallRegionScan("getvariable('many_regions')") +
	                              " GROUP BY region_id ORDER BY region_id");
	const std::vector<std::pair<std::string, std::uint64_t>> expected = {
	    {"A", 54}, {"CopyA", 54}, {"Corner", 6}, {"Multi", 12}, {"Narrow", 6}, {"OtherCorner", 6}, {"Repeat", 54}};
	Require(result->RowCount() == expected.size(),
	        "ten mixed regions must retain non-empty identities while empty geometries add no rows");
	for (idx_t row = 0; row < result->RowCount(); ++row) {
		Require(result->GetValue(0, row).GetValue<std::string>() == expected[row].first &&
		            result->GetValue(1, row).GetValue<std::uint64_t>() == expected[row].second,
		        "overlap, duplicate geometry, MultiPolygon, narrow, corner, and empty regions retain separate results");
	}
	const auto metrics = LastScanMetrics(connection);
	Require(metrics.find("\"input_region_count\":10") != std::string::npos &&
	            metrics.find("\"matched_pairs_exact\":192") != std::string::npos &&
	            metrics.find("\"match_count_complete\":true") != std::string::npos,
	        "ten-region exact pair counts must be complete and independent of emitted grouping rows");
}

void TestRegionOrdinalAcrossTasks(Connection &connection) {
	Query(connection, "SET threads=4; SET duckomo_max_threads=4");
	Query(connection,
	      "SET VARIABLE long_regions=["
	      "{'id':'A','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((-32.1 -89.9,31.9 -89.9,31.9 89.9,-32.1 89.9,-32.1 -89.9))'))},"
	      "{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.01 0.01,0.02 0.01,0.02 0.02,0.01 0.02,0.01 0.01))'))},"
	      "{'id':'B','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((-32.1 -89.9,31.9 -89.9,31.9 89.9,-32.1 89.9,-32.1 -89.9))'))}]");
	const auto read_sql = "read_om_regions(" + SqlLiteral("test/data/dimensions_perf.om") +
	                 ", regions:=getvariable('long_regions'), region_crs:='grid_lonlat', "
	                 "dimensions:=map(['value'],[['row','column']]), "
	                 "grid:={'nx':256,'ny':512,'lat0':-89.5,'lon0':-32.0,'dlat':0.35,'dlon':0.25,'order':'separate'}, "
	                 "spatial_axes:=['row','column'], include_source:=true)";
	const auto sql = "SELECT region_id, count(*)::UBIGINT, count(DISTINCT om_source.logical_index)::UBIGINT FROM " +
	                 read_sql + " GROUP BY region_id ORDER BY region_id";
	for (const auto thread_count : {1, 2, 4}) {
		Query(connection, "SET threads=" + std::to_string(thread_count) +
		                      "; SET duckomo_max_threads=" + std::to_string(thread_count));
		const auto result = Query(connection, sql);
		Require(result->RowCount() == 2, "a no-match middle region must not prevent later region tasks from running");
		for (idx_t row = 0; row < result->RowCount(); ++row) {
			Require(result->GetValue(1, row).GetValue<std::uint64_t>() == 512U * 256U &&
			                result->GetValue(2, row).GetValue<std::uint64_t>() == 512U * 256U,
			            "region ordinal must remain attached through multiple output batches and task windows");
		}
		const auto metrics = Query(
		    connection,
		    "SELECT metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1")
		                        ->GetValue(0, 0).GetValue<std::string>();
		const auto marker = metrics.find("\"max_active_workers\":");
		Require(marker != std::string::npos, "region execution metrics must record actual active workers");
		const auto active_workers = std::stoull(metrics.substr(marker + std::string("\"max_active_workers\":").size()));
		Require(active_workers == thread_count,
		        "long-window scans must exercise the configured 1/2/4 actual workers");
	}
	const auto limited = Query(
	    connection,
	    "SELECT region_id, om_source.logical_index FROM read_om_regions('test/data/dimensions_perf.om', "
	    "regions:=getvariable('long_regions'), region_crs:='grid_lonlat', "
	    "dimensions:=map(['value'],[['row','column']]), "
	    "grid:={'nx':256,'ny':512,'lat0':-89.5,'lon0':-32.0,'dlat':0.35,'dlon':0.25,'order':'separate'}, "
	    "spatial_axes:=['row','column'], include_source:=true) LIMIT 1");
	Require(limited->RowCount() == 1, "LIMIT must stop a long region scan after its first output row");
	const auto limit_metrics = Query(
	    connection,
	    "SELECT metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1")
	                              ->GetValue(0, 0).GetValue<std::string>();
	Require(limit_metrics.find("\"scan_complete\":false") != std::string::npos &&
	            limit_metrics.find("\"matched_pairs_exact\":null") != std::string::npos &&
	            limit_metrics.find("\"match_count_complete\":false") != std::string::npos,
	        "stopping with long-window tasks outstanding must not publish complete exact matches");

	Query(connection, "SET threads=1; SET duckomo_max_threads=1");
	bool cancelled = false;
	std::string cancellation_error;
	double cancellation_response_ms = 0;
	for (std::size_t attempt = 0; attempt < 3 && !cancelled; ++attempt) {
		std::atomic<bool> first_chunk{false};
		std::atomic<bool> allow_fetch{false};
		std::atomic<bool> finished{false};
		cancellation_error.clear();
		std::thread query_thread([&] {
			try {
				auto result = connection.SendQuery("SELECT region_id,value FROM " + read_sql);
				if (!result) {
					cancellation_error = "query returned no result";
				} else if (result->HasError()) {
					cancellation_error = result->GetError();
				} else {
					first_chunk.store(result->Fetch() != nullptr, std::memory_order_release);
					while (!allow_fetch.load(std::memory_order_acquire)) std::this_thread::yield();
					while (result->Fetch()) {
					}
					if (result->HasError()) cancellation_error = result->GetError();
				}
			} catch (const std::exception &error) {
				cancellation_error = error.what();
			}
			finished.store(true, std::memory_order_release);
		});
		while (!first_chunk.load(std::memory_order_acquire) && !finished.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		const auto interrupted_at = std::chrono::steady_clock::now();
		if (first_chunk.load(std::memory_order_acquire)) connection.Interrupt();
		allow_fetch.store(true, std::memory_order_release);
		query_thread.join();
		std::transform(cancellation_error.begin(), cancellation_error.end(), cancellation_error.begin(),
		               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		cancelled = cancellation_error.find("interrupt") != std::string::npos ||
		            cancellation_error.find("cancel") != std::string::npos;
		if (cancelled) {
			cancellation_response_ms = std::chrono::duration<double, std::milli>(
			    std::chrono::steady_clock::now() - interrupted_at).count();
		}
	}
	Require(cancelled, "region scan did not cancel after its first output batch: " + cancellation_error);
	const auto cancelled_metrics = LastScanMetrics(connection);
	Require(cancelled_metrics.find("\"status\":\"cancelled\"") != std::string::npos &&
	            cancelled_metrics.find("\"matched_pairs_exact\":null") != std::string::npos &&
	            cancelled_metrics.find("\"match_count_complete\":false") != std::string::npos &&
	            cancelled_metrics.find("\"query_owned_released_at_terminal\":true") != std::string::npos,
	        "cancelled region scans must publish incomplete exact counts and release owned work at QueryEnd");
	const auto recovered_rows = Query(connection, "SELECT count(*)::UBIGINT FROM " + read_sql);
	Require(recovered_rows->GetValue(0, 0).GetValue<std::uint64_t>() == 2ULL * 512 * 256,
	        "a complete region query must recover on the same connection after cancellation");
	Require(LastScanMetrics(connection).find("\"status\":\"success\"") != std::string::npos,
	        "recovery after region cancellation must publish successful terminal metrics");
	std::cout << "region_scan_test: cancellation response after first output was " << cancellation_response_ms
	          << " ms (observed only; no fixed bound)\n";
}

void TestAxisOrderAndMultipleValues(Connection &connection) {
	Query(connection,
	      "SET VARIABLE axis_regions=["
	      "{'id':'A','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))'))}]");
	const auto axis_rows = Query(
	    connection,
	    "SELECT valid_time, member, count(*)::UBIGINT FROM read_om_regions(" +
	        SqlLiteral("test/data/regions/regions_axis_order.om") +
	        ", regions:=getvariable('axis_regions'), region_crs:='grid_lonlat', "
	        "dimensions:=['time','member','lat','lon'], "
	        "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	        "spatial_axes:=['lat','lon'], axes:={" 
	        "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'}," 
	        "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}}) "
	        "GROUP BY valid_time, member ORDER BY valid_time, member");
	Require(axis_rows->RowCount() == 6, "reordered time/member axes must expose all declared groups");
	for (idx_t row = 0; row < axis_rows->RowCount(); ++row) {
		Require(axis_rows->GetValue(2, row).GetValue<std::uint64_t>() == 9,
		        "axis-order fixture must preserve nine spatial records in each time/member group");
	}

	Query(connection,
	      "SET VARIABLE multi_regions=["
	      "{'id':'All','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((-1 -1,3 -1,3 2,-1 2,-1 -1))'))}]");
	const auto multi = Query(
	    connection,
	    "SELECT count(*)::UBIGINT, sum(temperature)::DOUBLE, sum(humidity)::DOUBLE FROM read_om_regions(" +
	        SqlLiteral("test/data/regions/regions_multi.om") +
	        ", regions:=getvariable('multi_regions'), region_crs:='grid_lonlat', "
	        "dimensions:=map(['temperature','humidity'],[['row','column'],['row','column']]), "
	        "grid:={'nx':3,'ny':2,'lat0':0.0,'lon0':0.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	        "spatial_axes:=['row','column'])");
	Require(multi->RowCount() == 1 && multi->GetValue(0, 0).GetValue<std::uint64_t>() == 6 &&
	            multi->GetValue(1, 0).GetValue<double>() == 15.0 &&
	            multi->GetValue(2, 0).GetValue<double>() == 615.0,
	        "multi-value region scans must decode each requested array against the same exact positions");
}

void TestZeroValueIoAndRegionMetrics(Connection &connection) {
	Query(connection,
	      "SET VARIABLE zero_io_regions=["
	      "{'id':'A','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))'))}]");
	const auto rows = Query(
	    connection,
	    "SELECT count(*)::UBIGINT FROM read_om_regions('test/data/regions/regions.om', "
	    "regions:=getvariable('zero_io_regions'), region_crs:='grid_lonlat', "
	    "dimensions:=['member','time','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}})");
	Require(rows->GetValue(0, 0).GetValue<std::uint64_t>() == 54,
	        "COUNT(*) must apply exact covers while avoiding the value column");
	const auto metrics_result = Query(
	    connection,
	    "SELECT metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1");
	const auto metrics = metrics_result->GetValue(0, 0).GetValue<std::string>();
	Require(metrics.find("\"operation\":\"read_om_regions\"") != std::string::npos &&
	            metrics.find("\"count_unit\":\"region_record_pair\"") != std::string::npos &&
	            metrics.find("\"candidate_pairs_observed\":150") != std::string::npos &&
	            metrics.find("\"geometry_tested_pairs\":54") != std::string::npos &&
	            metrics.find("\"envelope_rejected_pairs\":96") != std::string::npos &&
	            metrics.find("\"matched_pairs_exact\":54") != std::string::npos &&
	            metrics.find("\"match_count_complete\":true") != std::string::npos,
	        "region metrics must report actual candidate, envelope, and exact predicate counts");
	Require(metrics.find("\"value_totals\":{\"index_bytes\":0,\"index_requests\":0,"
	                     "\"data_bytes\":0,\"data_requests\":0,\"decoded_chunks\":0,\"decode_complete\":true}") !=
	            std::string::npos,
	        "region_id and COUNT(*) paths must prove zero value index, data, and decode work");
}

void TestRegionInputsAreIncludedInQueryMemory(Connection &connection, std::uint32_t region_count = 256,
                                             std::uint32_t point_count = 1025) {
	std::string wkb(1, '\x01');
	auto append_u32 = [&](std::uint32_t value) {
		for (std::uint32_t byte = 0; byte < 4; ++byte) wkb.push_back(static_cast<char>((value >> (byte * 8)) & 0xff));
	};
	auto append_double = [&](double value) {
		std::uint64_t bits;
		std::memcpy(&bits, &value, sizeof(bits));
		for (std::uint32_t byte = 0; byte < 8; ++byte) wkb.push_back(static_cast<char>((bits >> (byte * 8)) & 0xff));
	};
	append_u32(3); // Polygon
	append_u32(1); // one ring
	append_u32(point_count);
	constexpr double pi = 3.14159265358979323846;
	for (std::uint32_t point = 0; point < point_count; ++point) {
		const auto angle = point + 1 == point_count ? 0.0 : 2.0 * pi * point / (point_count - 1);
		append_double(0.5 * std::cos(angle));
		append_double(0.5 * std::sin(angle));
	}
	static constexpr char HEX[] = "0123456789abcdef";
	std::string wkb_hex;
	wkb_hex.reserve(wkb.size() * 2);
	for (const auto byte : wkb) {
		const auto value = static_cast<unsigned char>(byte);
		wkb_hex.push_back(HEX[value >> 4]);
		wkb_hex.push_back(HEX[value & 0x0f]);
	}
	Query(connection,
	      "SET VARIABLE accounting_regions=(SELECT list({'id':'R'||i::VARCHAR,"
	      "'wkb':from_hex('" + wkb_hex + "')} ORDER BY i) FROM range(" + std::to_string(region_count) + ") t(i))");
	const auto rows = Query(connection, "SELECT count(*) FROM " + SmallRegionScan("getvariable('accounting_regions')"));
	Require(rows->GetValue(0, 0).GetValue<std::uint64_t>() == 0,
	        "large non-empty regions outside the fixture domain should preserve an exact zero result");
	const auto metrics = LastScanMetrics(connection);
	const auto input_wkb_bytes = JsonUintField(metrics, "input_wkb_bytes");
	const auto owned_input_bytes = JsonUintField(metrics, "owned_input_peak_bytes");
	const auto query_owned_peak_bytes = JsonUintField(metrics, "query_owned_peak_bytes");
	Require(JsonUintField(metrics, "input_region_count") == region_count &&
	            input_wkb_bytes == static_cast<std::uint64_t>(wkb.size()) * region_count,
	        "memory-accounting fixture must retain the expected non-empty WKB collection");
	Require(owned_input_bytes >= input_wkb_bytes * 2,
	        "owned input accounting must include the region collection and retained bind-data copies");
	Require(owned_input_bytes < input_wkb_bytes * 4 && owned_input_bytes <= 64 * 1024 * 1024,
	        "shared geometry payloads must be charged once across constants and bind-data copies");
	Require(query_owned_peak_bytes >= owned_input_bytes,
	        "query-owned memory peak must include retained geometry values and bound predicates");
	Require(query_owned_peak_bytes < input_wkb_bytes * 4,
	        "query memory must not charge shared geometry payloads again for bind-data copies");
	Require(JsonUintField(metrics, "owned_work_peak_bytes") < input_wkb_bytes,
	        "retained region storage must not inflate scan working memory");
}

void TestNearestRegionScan(Connection &connection) {
	const auto small_file = SamplingFixture("small-separate.om").string();
	const auto spatial_first_file = SamplingFixture("small-spatial-first.om").string();
	const auto many_axis_file = SamplingFixture("small-many-axis.om").string();
	const std::string small =
	    "read_om_regions(" + SqlLiteral(small_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest', include_source:=true)";
	const auto result = Query(connection,
	                          "SELECT count(*)::UBIGINT, count(DISTINCT sampling.source_point_index)::UBIGINT, "
	                          "min(\"small-separate/temperature\")::FLOAT, max(\"small-separate/temperature\")::FLOAT, "
	                          "min(om_source.logical_index)::UBIGINT, max(om_source.logical_index)::UBIGINT FROM " + small);
	Require(result->RowCount() == 1 && result->GetValue(0, 0).GetValue<std::uint64_t>() == 12 &&
	            result->GetValue(1, 0).GetValue<std::uint64_t>() == 1 &&
	            result->GetValue(2, 0).GetValue<float>() == 12.0F &&
	            result->GetValue(3, 0).GetValue<float>() == 211012.0F &&
	            result->GetValue(4, 0).GetValue<std::uint64_t>() == 12 &&
	            result->GetValue(5, 0).GetValue<std::uint64_t>() == 287,
	        "native nearest scan must preserve each official value and full source logical position");

	const auto interleaved = Query(
	    connection,
	    "SELECT sampling.axis_indices, \"small-spatial-first/temperature\", om_source.logical_index FROM "
	    "read_om_regions(" + SqlLiteral(spatial_first_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['lat','lon','member','time','level'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
	    "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
	    "method:='nearest', include_source:=true) WHERE sampling.axis_indices=[1,2,1]");
	Require(interleaved->RowCount() == 1 &&
	            interleaved->GetValue(0, 0).ToString() == "[1, 2, 1]" &&
	            interleaved->GetValue(1, 0).GetValue<float>() == 211012.0F &&
	            interleaved->GetValue(2, 0).GetValue<std::uint64_t>() == 155,
	        "native nearest scan must restore interleaved original source-axis positions");

	const auto many = Query(
	    connection,
	    "SELECT count(*)::UBIGINT, first(sampling.axis_indices), last(sampling.axis_indices), "
	    "sum(\"small-many-axis/temperature\")::BIGINT FROM "
	    "read_om_regions(" + SqlLiteral(many_axis_file) + ", "
	    "regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
	    "region_crs:='grid_lonlat', dimensions:=['sample','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], method:='nearest')");
	Require(many->RowCount() == 1 && many->GetValue(0, 0).GetValue<std::uint64_t>() == 513 &&
	            many->GetValue(1, 0).ToString() == "[0]" && many->GetValue(2, 0).ToString() == "[512]" &&
	            many->GetValue(3, 0).GetValue<std::int64_t>() == 6156,
	        "native nearest scan must exhaust all non-spatial positions across bounded task batches");
}

void TestRegionZeroIoEmptyAndFilterPaths(Connection &connection) {
	const auto empty_ring = Query(connection, "SELECT count(*) FROM " + SmallRegionScan(
	    "[{'id':'EmptyRing','wkb':ST_AsWKB(ST_GeomFromText('POLYGON (EMPTY)'))}]"));
	Require(empty_ring->GetValue(0, 0).GetValue<std::uint64_t>() == 0,
	        "spatial-produced zero-point rings are accepted as empty regions");
	const auto mixed_components = Query(connection, "SELECT count(*) FROM " + SmallRegionScan(
	    "[{'id':'Mixed','wkb':ST_AsWKB(ST_GeomFromText("
	    "'MULTIPOLYGON ((EMPTY),((100 20,102 20,102 22,100 22,100 20)))'))}]"));
	Require(mixed_components->GetValue(0, 0).GetValue<std::uint64_t>() == 54,
	        "an empty MultiPolygon component must not suppress the non-empty component's records");
	const auto counted = Query(connection, "SELECT count(*) FROM " + SmallRegionScan("getvariable('zero_io_regions')"));
	Require(counted->GetValue(0, 0).GetValue<std::uint64_t>() == 54, "count retains exact region membership");
	const auto count_metrics = LastScanMetrics(connection);
	Require(count_metrics.find("\"coordinate_preparation\":{\"evaluations\":150,\"complete\":true}") != std::string::npos &&
	            count_metrics.find("\"repeated_work\":{\"coordinate_evaluations\":150") != std::string::npos,
	        "count-only queries report coordinates evaluated for all 150 candidate records");
	const auto positions = Query(connection,
	                             "SELECT region_id,lat,lon,om_source.logical_index FROM "
	                             + SmallRegionScan("getvariable('zero_io_regions')", true));
	Require(positions->RowCount() == 54,
	        "region_id, coordinates, and source-only projection must retain exact positions without values");
	auto metrics = LastScanMetrics(connection);
	Require(ValueTotals(metrics) ==
	            "\"value_totals\":{\"index_bytes\":0,\"index_requests\":0,"
	            "\"data_bytes\":0,\"data_requests\":0,\"decoded_chunks\":0,\"decode_complete\":true}" &&
	            metrics.find("\"geometry_tested_pairs\":54") != std::string::npos,
	        "coordinate and geometry preparation must stay separate from zero value-I/O costs");

	Query(connection,
	      "SET VARIABLE gap_regions=["
	      "{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText('POLYGON((100.8 20.8,101.2 20.8,101.2 21.2,100.8 21.2,100.8 20.8),"
	      "(100.9 20.9,101.1 20.9,101.1 21.1,100.9 21.1,100.9 20.9))'))}];"
	      "SET VARIABLE empty_regions=["
	      "{'id':'Empty','wkb':ST_AsWKB(ST_GeomFromText('POLYGON EMPTY'))}]");
	const auto no_exact = Query(connection,
	                            "SELECT count(*) FROM " + SmallRegionScan("getvariable('gap_regions')"));
	Require(no_exact->GetValue(0, 0).GetValue<std::uint64_t>() == 0,
	        "a nonempty candidate scan with an envelope that matches no grid point must emit zero rows");
	metrics = LastScanMetrics(connection);
	Require(metrics.find("\"candidate_pairs_observed\":150") != std::string::npos &&
	            metrics.find("\"geometry_tested_pairs\":6") != std::string::npos &&
	            metrics.find("\"envelope_rejected_pairs\":144") != std::string::npos &&
	            metrics.find("\"matched_pairs_exact\":0") != std::string::npos &&
	            metrics.find("\"match_count_complete\":true") != std::string::npos &&
	            metrics.find("\"value_totals\":{\"index_bytes\":0,\"index_requests\":0,"
	                         "\"data_bytes\":0,\"data_requests\":0,\"decoded_chunks\":0,\"decode_complete\":true}") !=
	                std::string::npos,
	        "candidate-nonempty exact-empty scans must finish with zero exact matches and zero value I/O");
	const auto batch_memory_start = metrics.find("\"batch_segments\":{");
	Require(batch_memory_start != std::string::npos, "metrics must expose batch working memory");
	Require(JsonUintField(metrics.substr(batch_memory_start), "peak_owned_capacity_bytes") >= 150 * sizeof(std::uint64_t),
	        "exact-empty filtering must charge the live candidate position buffer");
	Require(metrics.find("\"coordinate_preparation\":{\"evaluations\":150,\"complete\":true}") != std::string::npos,
	        "exact-empty filtering reports its coordinate work");

	for (const auto &regions : {std::string("getvariable('empty_regions')"),
	                            std::string("[]::STRUCT(id VARCHAR,wkb BLOB)[]")}) {
		const auto empty = Query(connection, "SELECT count(*) FROM " + SmallRegionScan(regions));
		Require(empty->GetValue(0, 0).GetValue<std::uint64_t>() == 0,
		        "all-EMPTY geometry and an empty region list must both return no rows");
		metrics = LastScanMetrics(connection);
		Require(metrics.find("\"matched_pairs_exact\":0") != std::string::npos &&
		            metrics.find("\"match_count_complete\":true") != std::string::npos &&
		            metrics.find("\"value_totals\":{\"index_bytes\":0,\"index_requests\":0,"
		                         "\"data_bytes\":0,\"data_requests\":0,\"decoded_chunks\":0,\"decode_complete\":true}") !=
		                std::string::npos,
		        "empty-region scans must complete exact zero metrics without value reads");
	}

	const auto empty_axis = Query(connection,
	                              "SELECT count(*) FROM " + SmallRegionScan("getvariable('zero_io_regions')") +
	                                  " WHERE member=99");
	Require(empty_axis->GetValue(0, 0).GetValue<std::uint64_t>() == 0,
	        "an empty semantic-axis selection must not produce region rows");
	metrics = LastScanMetrics(connection);
	Require(metrics.find("\"matched_pairs_exact\":0") != std::string::npos &&
	            metrics.find("\"match_count_complete\":true") != std::string::npos &&
	            metrics.find("\"value_totals\":{\"index_bytes\":0,\"index_requests\":0,"
	                         "\"data_bytes\":0,\"data_requests\":0,\"decoded_chunks\":0,\"decode_complete\":true}") !=
	                std::string::npos,
	        "empty axis selections must complete without value reads");

	const auto filtered = Query(connection,
	                            "SELECT region_id FROM " + SmallRegionScan("getvariable('zero_io_regions')") +
	                                " WHERE member=1 AND valid_time=TIMESTAMP '2026-01-01 00:00:00' AND value>=1010");
	Require(filtered->RowCount() == 5,
	        "an unprojected value filter must return five non-NULL rows, got " + std::to_string(filtered->RowCount()));
	metrics = LastScanMetrics(connection);
	const auto value_totals = ValueTotals(metrics);
	Require(value_totals.find("\"data_requests\":0") == std::string::npos &&
	            value_totals.find("\"decoded_chunks\":0") == std::string::npos,
	        "an unprojected value filter must perform and report the required value read");

	const auto filtered_empty = Query(
	    connection, "SELECT region_id FROM " + SmallRegionScan("getvariable('gap_regions')") + " WHERE value>0");
	Require(filtered_empty->RowCount() == 0,
	        "exact geometry rejection must remain ahead of the residual value filter");
	metrics = LastScanMetrics(connection);
	Require(metrics.find("\"geometry_tested_pairs\":6") != std::string::npos &&
	            ValueTotals(metrics).find("\"data_bytes\":0") != std::string::npos &&
	            ValueTotals(metrics).find("\"decoded_chunks\":0") != std::string::npos,
	        "an unprojected filter dependency must not force value reads for exact-empty candidates");
}

void TestLimitKeepsExactUnknownAndAllowsRecovery(Connection &connection) {
	const auto limited_rows = Query(
	    connection,
	    "SELECT region_id FROM read_om_regions('test/data/regions/regions.om', "
	    "regions:=getvariable('zero_io_regions'), region_crs:='grid_lonlat', "
	    "dimensions:=['member','time','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}}) LIMIT 1");
	Require(limited_rows->RowCount() == 1, "LIMIT must emit its requested region row");
	const auto limited_metrics = Query(
	    connection,
	    "SELECT metrics FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1")
	                                ->GetValue(0, 0).GetValue<std::string>();
	Require(limited_metrics.find("\"scan_complete\":false") != std::string::npos &&
	            limited_metrics.find("\"matched_pairs_observed\":54") != std::string::npos &&
	            limited_metrics.find("\"matched_pairs_exact\":null") != std::string::npos &&
	            limited_metrics.find("\"match_count_complete\":false") != std::string::npos,
	        "LIMIT must retain observed predicate work and leave complete matches unknown");
	const auto recovered = Query(
	    connection,
	    "SELECT count(*)::UBIGINT FROM read_om_regions('test/data/regions/regions.om', "
	    "regions:=getvariable('zero_io_regions'), region_crs:='grid_lonlat', "
	    "dimensions:=['member','time','lat','lon'], "
	    "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
	    "spatial_axes:=['lat','lon'], axes:={"
	    "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	    "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}})");
	Require(recovered->GetValue(0, 0).GetValue<std::uint64_t>() == 54,
	        "a successful full scan must work after a LIMIT-stopped region scan");
}

void TestRegionMultiScanAndConnectionIsolation(Connection &connection, DuckDB &database,
                                               const std::string &spatial_path) {
	const auto wkb = ReadWkbHex("test/data/regions/geometries/a.wkb");
	const auto regions = "[{'id':'A','wkb':from_hex(" + SqlLiteral(wkb) + ")}]";
	Query(connection, "SET VARIABLE lifecycle_regions=" + regions);
	const auto scan = SmallRegionScan("getvariable('lifecycle_regions')");
	const auto result = Query(connection, "SELECT (SELECT count(*) FROM " + scan +
	                                           "), (SELECT count(*) FROM " + scan + ")");
	Require(result->RowCount() == 1 && result->GetValue(0, 0).GetValue<std::uint64_t>() == 54 &&
	            result->GetValue(1, 0).GetValue<std::uint64_t>() == 54,
	        "both region table functions in one query must run to completion");
	const auto same_query_scans = Query(
	    connection,
	    "SELECT count(*)::UBIGINT || '/' || count(DISTINCT query_id)::VARCHAR || '/' || "
	    "min(scan_id)::VARCHAR || '/' || max(scan_id)::VARCHAR || '/' || "
	    "count(*) FILTER (WHERE metrics LIKE '%\"operation\":\"read_om_regions\"%' "
	    "AND metrics LIKE '%\"terminal_published\":true%' "
	    "AND metrics LIKE '%\"query_owned_released_at_terminal\":true%')::VARCHAR "
	    "FROM duckomo_last_scan_metrics()");
	Require(same_query_scans->GetValue(0, 0).GetValue<std::string>() == "2/1/0/1/2",
	        "one query must publish two independently terminal region scans exactly once");

	Connection second(database);
	Query(second, "LOAD " + SqlLiteral(spatial_path));
	Require(Query(second, "SELECT count(*) FROM duckomo_last_scan_metrics()")
	            ->GetValue(0, 0).GetValue<std::uint64_t>() == 0,
	        "region metrics must begin empty on another connection");
	Query(second, "SET VARIABLE lifecycle_regions=" + regions);
	const auto second_count = Query(second, "SELECT count(*) FROM " + scan);
	Require(second_count->GetValue(0, 0).GetValue<std::uint64_t>() == 54 &&
	            Query(second, "SELECT count(*) FROM duckomo_last_scan_metrics()")
	                    ->GetValue(0, 0).GetValue<std::uint64_t>() == 1,
	        "a second connection must execute regions and publish only its own scan");
	Require(Query(connection, "SELECT count(*) FROM duckomo_last_scan_metrics()")
	            ->GetValue(0, 0).GetValue<std::uint64_t>() == 2,
	        "a second connection must not replace the first connection's two-scan profile");
}

void TestRegionObjectChangeFailureAndRecovery(Connection &connection) {
	namespace fs = std::filesystem;
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto wkb = ReadWkbHex("test/data/regions/geometries/a.wkb");
	auto run_failure = [&](const std::string &name, auto mutate) {
		const auto path = fs::temp_directory_path() /
		                  ("duckomo-region-" + name + "-" + std::to_string(stamp) + ".om");
		const bool nearest = name.rfind("nearest_", 0) == 0;
		const auto fixture_path = nearest ? SamplingFixture("small-separate.om") : fs::path("test/data/regions/regions.om");
		fs::copy_file(fixture_path, path, fs::copy_options::overwrite_existing);
		const bool decoder_failure = name.find("decoder_failure") != std::string::npos;
		const std::string aggregate = decoder_failure ?
		    (nearest ? "sum(\"small-separate/temperature\")" : "sum(value)") : "count(*)";
		const auto scan = nearest
		    ? "SELECT " + aggregate + " FROM read_om_regions(" + SqlLiteral(path.string()) +
		          ", regions:=[{'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText("
		          "'POLYGON((0.1 0.1,0.2 0.1,0.2 0.2,0.1 0.2,0.1 0.1))'))}], "
		          "region_crs:='grid_lonlat', dimensions:=['member','time','level','lat','lon'], "
		          "grid:={'nx':5,'ny':5,'lat0':-2.0,'lon0':-2.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
		          "spatial_axes:=['lat','lon'], axes:={"
		          "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
		          "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]},"
		          "'level':{'axis':'level','values':[100::BIGINT,200::BIGINT],'kind':'pressure','unit':'hPa'}}, "
		          "method:='nearest')"
		    : "SELECT " + aggregate + " FROM read_om_regions(" + SqlLiteral(path.string()) +
		          ", regions:=[{'id':'A','wkb':from_hex(" + SqlLiteral(wkb) + ")}], "
		          "region_crs:='grid_lonlat', dimensions:=['member','time','lat','lon'], "
		          "grid:={'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
		          "spatial_axes:=['lat','lon'], axes:={"
		          "'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
		          "'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}})";
		Query(connection, "PREPARE region_" + name + " AS " + scan);
		mutate(path);
		auto failed = connection.Query("EXECUTE region_" + name);
		Require(failed && failed->HasError(), "a prepared region scan must fail after " + name);
		const auto failure_metrics = LastScanMetrics(connection);
		Require(failure_metrics.find("\"status\":\"failure\"") != std::string::npos &&
		            failure_metrics.find("\"scan_complete\":false") != std::string::npos &&
		            failure_metrics.find("\"exact_candidate_records\":null") != std::string::npos &&
		            failure_metrics.find("\"terminal_published\":true") != std::string::npos &&
		            failure_metrics.find("\"query_owned_released_at_terminal\":true") != std::string::npos,
		        name + " failures must publish incomplete terminal metrics and release scan-owned work at QueryEnd");
		if (nearest && name == "nearest_decoder_failure") {
			Require(failure_metrics.find("\"matched_pairs_exact\":null") != std::string::npos &&
				            failure_metrics.find("\"match_count_complete\":null") != std::string::npos &&
				            failure_metrics.find("\"result_rows_exact\":null") != std::string::npos &&
				            failure_metrics.find("\"result_count_complete\":false") != std::string::npos,
				        name + " failures after nearest plan publication must leave point matches inapplicable and result counts incomplete");
		} else if (!nearest) {
			Require(failure_metrics.find("\"matched_pairs_exact\":null") != std::string::npos &&
				            failure_metrics.find("\"match_count_complete\":false") != std::string::npos,
				        name + " failures must publish incomplete points-region pair counts");
		}
		if (name == "decoder_failure") {
			const auto totals = ValueTotals(failure_metrics);
			const auto marker = totals.find("\"index_requests\":");
			Require(marker != std::string::npos &&
			            std::stoull(totals.substr(marker + std::string("\"index_requests\":").size())) > 0,
			        "a corrupted decoder lookup must fail after the projected value index is read");
		}
		Query(connection, "DEALLOCATE region_" + name);
		fs::remove(path);
	};
	run_failure("object_change", [](const fs::path &path) {
		std::ofstream changed(path, std::ios::binary | std::ios::app);
		Require(changed.good(), "could not open copied OM object for mutation");
		changed.put('\0');
		Require(changed.good(), "could not mutate copied OM object");
	});
	run_failure("short_read", [](const fs::path &path) {
		const auto file_size = fs::file_size(path);
		Require(file_size > 0, "copied OM object is empty");
		fs::resize_file(path, file_size - 1);
	});
	run_failure("decoder_failure", [&](const fs::path &path) {
		const auto [offset, count] = FirstValueLookupRange(connection, path);
		std::fstream changed(path, std::ios::binary | std::ios::in | std::ios::out);
		Require(changed.good(), "could not open copied OM object for index corruption");
		changed.seekp(static_cast<std::streamoff>(offset));
		for (std::uint64_t index = 0; index < count; ++index) changed.put(static_cast<char>(0xff));
		Require(changed.good(), "could not corrupt copied OM lookup table");
	});
	run_failure("nearest_object_change", [](const fs::path &path) {
		std::ofstream changed(path, std::ios::binary | std::ios::app);
		Require(changed.good(), "could not open copied nearest OM object for mutation");
		changed.put('\0');
		Require(changed.good(), "could not mutate copied nearest OM object");
	});
	run_failure("nearest_short_read", [](const fs::path &path) {
		const auto file_size = fs::file_size(path);
		Require(file_size > 0, "copied nearest OM object is empty");
		fs::resize_file(path, file_size - 1);
	});
	run_failure("nearest_decoder_failure", [&](const fs::path &path) {
		const auto [offset, count] = FirstValueLookupRange(connection, path);
		std::fstream changed(path, std::ios::binary | std::ios::in | std::ios::out);
		Require(changed.good(), "could not open copied nearest OM object for index corruption");
		changed.seekp(static_cast<std::streamoff>(offset));
		for (std::uint64_t index = 0; index < count; ++index) changed.put(static_cast<char>(0xff));
		Require(changed.good(), "could not corrupt copied nearest OM lookup table");
	});
	const auto recovered = Query(connection, "SELECT count(*) FROM " + SmallRegionScan("getvariable('lifecycle_regions')"));
	Require(recovered->GetValue(0, 0).GetValue<std::uint64_t>() == 54 &&
	            LastScanMetrics(connection).find("\"status\":\"success\"") != std::string::npos,
	        "a valid region scan must recover on the same connection after an object-change failure");
}

} // namespace

int main(int argc, char **argv) {
	try {
		const auto *spatial_path = argc >= 2 ? argv[1] : std::getenv("DUCKOMO_SPATIAL_EXTENSION");
		Require(spatial_path != nullptr && spatial_path[0] != '\0',
		        "usage: region_scan_test <matching-spatial-extension-path> (or DUCKOMO_SPATIAL_EXTENSION)");
		const auto *core_path = argc >= 3 ? argv[2] : std::getenv("DUCKOMO_CORE_FUNCTIONS_EXTENSION");
		DBConfig config;
		config.SetOptionByName("allow_unsigned_extensions", true);
		config.options.maximum_threads = 4;
		DuckDB database(nullptr, &config);
		database.LoadStaticExtension<CoreFunctionsExtension>();
		Connection connection(database);
		if (core_path != nullptr && core_path[0] != '\0') Query(connection, "LOAD " + SqlLiteral(core_path));
		Query(connection, "LOAD " + SqlLiteral(spatial_path));
		if (argc == 5 && std::string(argv[3]) == "--resource-probe") {
			RunRegionResourceProbe(connection, argv[4], ProcessPeakRssBytes());
			return 0;
		}
		TestRepeatedCoordinatesAndEmptyRegion(connection);
		TestTenRegionMixedShapes(connection);
		TestRegionOrdinalAcrossTasks(connection);
		TestAxisOrderAndMultipleValues(connection);
		TestNearestRegionScan(connection);
		TestZeroValueIoAndRegionMetrics(connection);
		TestRegionInputsAreIncludedInQueryMemory(connection);
		TestRegionInputsAreIncludedInQueryMemory(connection, 31, 32768);
		TestRegionZeroIoEmptyAndFilterPaths(connection);
		TestLimitKeepsExactUnknownAndAllowsRecovery(connection);
		TestRegionMultiScanAndConnectionIsolation(connection, database, spatial_path);
		TestRegionObjectChangeFailureAndRecovery(connection);
		std::cout << "region_scan_test: task windows, ordinals, zero-value I/O, metrics, cancellation, short-read/decoder/object-change failure recovery, and multi-connection lifecycle passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "region_scan_test: " << error.what() << '\n';
		return 1;
	}
}
