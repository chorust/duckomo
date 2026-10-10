#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>
#include <unistd.h>
extern "C" {
#include "om_encoder.h"
#include "om_file.h"
#include "om_variable.h"
}

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &db) {
	db.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb
namespace {
using Bytes = std::vector<std::uint8_t>;
struct Ref {
	std::uint64_t offset, size;
};
void Require(bool ok, const std::string &message) {
	if (!ok)
		throw std::runtime_error(message);
}
Ref Append(Bytes &file, const Bytes &data) {
	Ref ref {file.size(), data.size()};
	file.insert(file.end(), data.begin(), data.end());
	return ref;
}
Ref Scalar(Bytes &file, const std::string &name, OmDataType_t type, const void *value, std::size_t size = 0) {
	Bytes meta(om_variable_write_scalar_size(name.size(), 0, type, size));
	om_variable_write_scalar(meta.data(), name.size(), 0, nullptr, nullptr, name.data(), type, value, size);
	return Append(file, meta);
}
Ref Array(Bytes &file, const std::string &name, const std::vector<std::uint64_t> &shape, OmDataType_t type,
          const void *values, const std::vector<Ref> &children = {}) {
	auto chunks = shape;
	for (auto &chunk : chunks)
		chunk = std::min<std::uint64_t>(chunk, 2);
	auto compression = type == DATA_TYPE_INT64_ARRAY ? COMPRESSION_PFOR_DELTA2D : COMPRESSION_FPX_XOR2D;
	OmEncoder_t encoder {};
	Require(om_encoder_init(&encoder, 1, 0, compression, type, shape.data(), chunks.data(), shape.size()) == ERROR_OK,
	        "cannot initialize fixture encoder");
	std::vector<std::uint64_t> lut(om_encoder_count_chunks(&encoder) + 1), zero(shape.size(), 0);
	Bytes compressed(om_encoder_compressed_chunk_buffer_size(&encoder)),
	    scratch(om_encoder_chunk_buffer_size(&encoder));
	lut[0] = file.size();
	for (std::size_t i = 0; i + 1 < lut.size(); i++) {
		auto size = om_encoder_compress_chunk(&encoder, values, shape.data(), zero.data(), shape.data(), i, i,
		                                      compressed.data(), scratch.data());
		Require(size > 0 && size <= compressed.size(), "fixture compression failed");
		file.insert(file.end(), compressed.begin(), compressed.begin() + size);
		lut[i + 1] = file.size();
	}
	Bytes lookup(om_encoder_lut_buffer_size(lut.data(), lut.size()));
	auto lookup_size = om_encoder_compress_lut(lut.data(), lut.size(), lookup.data(), lookup.size());
	lookup.resize(lookup_size);
	const auto lookup_offset = file.size();
	Append(file, lookup);
	std::vector<std::uint64_t> offsets, sizes;
	for (const auto &child : children) {
		offsets.push_back(child.offset);
		sizes.push_back(child.size);
	}
	Bytes meta(om_variable_write_numeric_array_size(name.size(), children.size(), shape.size()));
	om_variable_write_numeric_array(meta.data(), name.size(), children.size(), offsets.data(), sizes.data(),
	                                name.data(), type, compression, 1, 0, shape.size(), shape.data(), chunks.data(),
	                                lookup_size, lookup_offset);
	return Append(file, meta);
}
void Fixture(const std::filesystem::path &path, const std::vector<std::uint64_t> &shape, const std::string &axes,
             const std::vector<std::int64_t> &times, bool scalar = false) {
	Bytes file(om_header_write_size());
	om_header_write(file.data());
	std::vector<Ref> children;
	children.push_back(Scalar(file, "coordinates", DATA_TYPE_STRING, axes.data(), axes.size()));
	if (scalar)
		children.push_back(Scalar(file, "valid_time", DATA_TYPE_INT64, times.data()));
	else
		children.push_back(Array(file, "time", {times.size()}, DATA_TYPE_INT64_ARRAY, times.data()));
	std::vector<float> values(
	    std::accumulate(shape.begin(), shape.end(), std::uint64_t(1), std::multiplies<std::uint64_t>()));
	std::iota(values.begin(), values.end(), 0.0f);
	auto root = Array(file, "", shape, DATA_TYPE_FLOAT_ARRAY, values.data(), children);
	Bytes trailer(om_trailer_size());
	om_trailer_write(trailer.data(), root.offset, root.size);
	Append(file, trailer);
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(file.data()), file.size());
	Require(out.good(), "cannot write fixture");
}
std::string Read(const std::filesystem::path &path, const std::string &params = "") {
	return "read_om('" + path.string() + "'" + params + ")";
}
std::string Spatial() {
	return ", grid := {'nx':2,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':2.0,'order':'separate'}, spatial_axes "
	       ":= ['lat','lon']";
}
void Expect(duckdb::Connection &connection, const std::string &sql, const std::vector<std::string> &expected) {
	auto result = connection.Query(sql);
	Require(result && !result->HasError(), sql + "\n" + (result ? result->GetError() : "no result"));
	Require(result->RowCount() * result->ColumnCount() == expected.size(), "unexpected result size: " + sql);
	std::size_t index = 0;
	for (duckdb::idx_t row = 0; row < result->RowCount(); row++) {
		for (duckdb::idx_t col = 0; col < result->ColumnCount(); col++) {
			const auto actual = result->GetValue(col, row).ToString();
			Require(actual == expected[index], "expected " + expected[index] + ", got " + actual + ": " + sql);
			index++;
		}
	}
}
void Reject(duckdb::Connection &connection, const std::string &sql, const std::string &message) {
	auto result = connection.Query(sql);
	Require(result && result->HasError() && result->GetError().find(message) != std::string::npos,
	        "expected rejection containing '" + message + "': " + sql + "\n" +
	            (result ? result->ToString() : "no result"));
}
bool PositiveJsonField(const std::string &json, const std::string &object, const std::string &field) {
	const auto object_start = json.find("\"" + object + "\":{");
	if (object_start == std::string::npos) return false;
	const auto object_end = json.find('}', object_start);
	const auto field_start = json.find("\"" + field + "\":", object_start);
	if (field_start == std::string::npos || field_start > object_end) return false;
	const auto value_start = field_start + field.size() + 3;
	const auto value_end = json.find_first_not_of("0123456789", value_start);
	if (value_start == value_end) return false;
	return std::stoull(json.substr(value_start, value_end - value_start)) > 0;
}
void Run(const std::filesystem::path &dir) {
	duckdb::DBConfig config;
	config.SetOptionByName("allow_unsigned_extensions", true);
	duckdb::DuckDB db(nullptr, &config);
	db.LoadStaticExtension<duckdb::DuckomoExtension>();
	duckdb::Connection connection(db);
	const auto *core_override = std::getenv("DUCKOMO_CORE_FUNCTIONS_EXTENSION");
	const std::string core_path = core_override ? core_override : "build/release/extension/core_functions/core_functions.duckdb_extension";
	Expect(connection, "LOAD '" + core_path + "'", {});
	const auto run = dir / "run.om", permuted = dir / "permuted.om", snapshot = dir / "snapshot.om";
	const std::int64_t start = 1789948800; // 2026-09-21 00:00 UTC, independent known reference.
	Fixture(run, {2, 2, 3}, "lat lon time", {start, start + 3600, start + 10800});
	Expect(connection, "SELECT value, valid_time FROM " + Read(run) + " WHERE value BETWEEN 6 AND 8 ORDER BY value",
	       {"6.0", "2026-09-21 00:00:00", "7.0", "2026-09-21 01:00:00", "8.0", "2026-09-21 03:00:00"});
	auto metric_rows = connection.Query("SELECT metrics FROM duckomo_last_scan_metrics()");
	Require(metric_rows && !metric_rows->HasError() && metric_rows->RowCount() == 1,
	        "time array scan must publish one v3 metrics row");
	const auto time_metrics = metric_rows->GetValue(0, 0).GetValue<std::string>();
	Require(PositiveJsonField(time_metrics, "coordinate", "index_bytes") &&
	            PositiveJsonField(time_metrics, "coordinate", "data_bytes") &&
	            PositiveJsonField(time_metrics, "coordinate", "decoded_chunks"),
	        "time coordinate index/data/decode costs must be attributed to coordinate metrics");
	Require(time_metrics.find("\"variables\":{\"/\":{") != std::string::npos &&
	            time_metrics.find("\"/time\":") == std::string::npos,
	        "time coordinate reads must not be attributed to the value variable map");
	Require(time_metrics.find("\"query_memory_count_complete\":true") != std::string::npos &&
	            time_metrics.find("\"peak_query_owned_bytes\":null") == std::string::npos,
	        "successful local scans must publish the tracked query-owned buffer high-water mark");
	Expect(connection,
	       "SELECT value, valid_time FROM " + Read(run, Spatial()) +
	           " WHERE lat=11 AND lon=100 ORDER BY valid_time",
	       {"6.0", "2026-09-21 00:00:00", "7.0", "2026-09-21 01:00:00", "8.0", "2026-09-21 03:00:00"});
	Expect(connection,
	       "SELECT count(*) FROM " + Read(run, Spatial()) + " WHERE valid_time=TIMESTAMP '2026-09-21 03:00:00'", {"4"});
	Expect(connection,
	       "SELECT value, valid_time FROM " + Read(run, Spatial()) +
	           " WHERE lat=11 AND lon=100 AND valid_time=TIMESTAMP '2026-09-21 03:00:00'",
	       {"8.0", "2026-09-21 03:00:00"});
	Expect(connection, "SELECT min(valid_time), max(valid_time), count(*) FROM " + Read(run),
	       {"2026-09-21 00:00:00", "2026-09-21 03:00:00", "12"});
	Fixture(permuted, {2, 2, 3}, "time lat lon", {-3600, 0});
	Expect(connection, "SELECT value,valid_time FROM " + Read(permuted) + " WHERE value IN (0,5,6,11) ORDER BY value",
	       {"0.0", "1969-12-31 23:00:00", "5.0", "1969-12-31 23:00:00", "6.0", "1970-01-01 00:00:00", "11.0",
	        "1970-01-01 00:00:00"});
	Fixture(snapshot, {2, 3}, "lat lon", {start}, true);
	Expect(connection, "SELECT min(valid_time),max(valid_time),count(*) FROM " + Read(snapshot),
	       {"2026-09-21 00:00:00", "2026-09-21 00:00:00", "6"});
	Fixture(permuted, {2, 2, 3}, "lat time lon", {start, start + 3600});
	Expect(connection,
	       "SELECT value,valid_time FROM " + Read(permuted) + " WHERE value IN (0,3,6,9) ORDER BY value",
	       {"0.0", "2026-09-21 00:00:00", "3.0", "2026-09-21 01:00:00", "6.0", "2026-09-21 00:00:00", "9.0",
	        "2026-09-21 01:00:00"});
	const auto bad = dir / "bad.om";
	Fixture(bad, {2, 2, 3}, "lat lon time", {start, start + 1});
	Reject(connection, "SELECT * FROM " + Read(bad), "length must match");
	Fixture(bad, {2, 3}, "lat lon", {start, start + 1, start + 2});
	Reject(connection, "SELECT * FROM " + Read(bad), "requires a complete ordered time axis");
	Fixture(bad, {2, 3}, "lat lon", {std::numeric_limits<std::int64_t>::max()}, true);
	Reject(connection, "SELECT * FROM " + Read(bad), "outside the finite TIMESTAMP range");
	Reject(connection, "SELECT * FROM " + Read(run, ", valid_times := [TIMESTAMP '2000-01-01']"), "conflicts");
	const auto raw = std::filesystem::path("test/data/raw.om");
	const auto explicit_params = ", dimensions := map(['value'], [['time','point']]), valid_times := [TIMESTAMP "
	                             "'2000-01-01',TIMESTAMP '2000-01-02']";
	Expect(connection,
	       "SELECT value,valid_time FROM " + Read(raw, explicit_params) + " WHERE value IN (0,3) ORDER BY value",
	       {"0.0", "2000-01-01 00:00:00", "3.0", "2000-01-02 00:00:00"});
	Reject(connection, "SELECT * FROM " + Read(raw, ", valid_times := []"), "must not be empty");
	Reject(connection, "SELECT * FROM " + Read(raw, ", valid_times := [NULL]"), "finite, non-NULL");
	Reject(connection, "SELECT * FROM " + Read(raw, ", valid_times := [TIMESTAMP 'infinity']"), "finite, non-NULL");
	// Cross a DuckDB batch boundary with time as an outer axis.
	Fixture(permuted, {2, 1200}, "time point", {start, start + 1});
	Expect(connection,
	       "SELECT value,valid_time FROM " + Read(permuted) + " WHERE value IN (1199,1200,2048,2399) ORDER BY value",
	       {"1199.0", "2026-09-21 00:00:00", "1200.0", "2026-09-21 00:00:01", "2048.0", "2026-09-21 00:00:01", "2399.0",
	        "2026-09-21 00:00:01"});
}
} // namespace
int main() {
	char temp[] = "/tmp/duckomo-time-test-XXXXXX";
	const auto *dir = mkdtemp(temp);
	if (!dir)
		return 1;
	try {
		Run(dir);
		std::filesystem::remove_all(dir);
		std::cout << "time coordinate checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::filesystem::remove_all(dir);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
