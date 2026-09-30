#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using duckomo_validation_support::CommandResult;
using duckomo_validation_support::RunProcess;

namespace {

struct Options final {
	fs::path root;
	fs::path fixtures;
	fs::path output;
	fs::path duckdb;
	fs::path extension;
};

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

Options ParseOptions(int argc, char **argv) {
	const auto executable = fs::weakly_canonical(fs::absolute(argv[0]));
	Options options;
	options.root = executable.parent_path().parent_path().parent_path().parent_path().parent_path();
	options.fixtures = options.root / "test/data";
	options.output = options.root / "build/evidence/dimensions";
	options.duckdb = options.root / "build/release/duckdb";
	options.extension = options.root / "build/release/extension/duckomo/duckomo.duckdb_extension";
	for (int i = 1; i < argc; i++) {
		const std::string name = argv[i];
		if (name == "--help" || name == "-h") {
			std::cout << "Usage: duckomo_dimensions_validation [--root PATH] [--fixtures PATH] [--output PATH] "
			             "[--duckdb PATH] [--extension PATH]\n";
			std::exit(0);
		}
		Require(i + 1 < argc, "missing value after " + name);
		const fs::path value = argv[++i];
		if (name == "--root") options.root = value;
		else if (name == "--fixtures") options.fixtures = value;
		else if (name == "--output") options.output = value;
		else if (name == "--duckdb") options.duckdb = value;
		else if (name == "--extension") options.extension = value;
		else throw std::runtime_error("unknown argument " + name);
	}
	options.root = fs::weakly_canonical(fs::absolute(options.root));
	auto resolve = [&](fs::path &path) {
		if (path.is_relative()) path = fs::current_path() / path;
		path = fs::weakly_canonical(path);
	};
	resolve(options.fixtures);
	resolve(options.output);
	resolve(options.duckdb);
	resolve(options.extension);
	return options;
}

std::string ReadText(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "cannot read " + path.string());
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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

std::string JoinPath(const std::vector<std::string> &fields) {
	std::string result;
	for (std::size_t i = 0; i < fields.size(); i++) {
		if (i) result.push_back(',');
		result += fields[i];
	}
	return result;
}

std::vector<std::string> SplitCsv(const std::string &line) {
	std::vector<std::string> fields;
	std::string value;
	bool quoted = false;
	for (std::size_t i = 0; i < line.size(); i++) {
		const auto ch = line[i];
		if (quoted) {
			if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
				value.push_back('"');
				i++;
			} else if (ch == '"') {
				quoted = false;
			} else {
				value.push_back(ch);
			}
		} else if (ch == '"' && value.empty()) {
			quoted = true;
		} else if (ch == ',') {
			fields.emplace_back(std::move(value));
			value.clear();
		} else {
			value.push_back(ch);
		}
	}
	Require(!quoted, "DuckDB returned malformed CSV output");
	fields.emplace_back(std::move(value));
	return fields;
}

std::vector<std::vector<std::string>> CsvRows(const std::string &text) {
	std::vector<std::vector<std::string>> rows;
	std::istringstream input(text);
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (!line.empty()) rows.emplace_back(SplitCsv(line));
	}
	return rows;
}

void WriteCsvRows(const fs::path &path, const std::vector<std::vector<std::string>> &rows) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	Require(output.good(), "cannot create CSV evidence " + path.string());
	for (const auto &row : rows) {
		for (std::size_t column = 0; column < row.size(); column++) {
			if (column) output << ',';
			const auto &field = row[column];
			const bool quote = field.find_first_of(",\"\r\n") != std::string::npos;
			if (quote) output << '"';
			for (const auto ch : field) {
				if (ch == '"') output << "\"\"";
				else output << ch;
			}
			if (quote) output << '"';
		}
		output << '\n';
	}
	Require(output.good(), "failed to write CSV evidence " + path.string());
}

std::vector<double> ReadReferenceValues(const fs::path &path) {
	std::istringstream input(ReadText(path));
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)), "performance value reference is empty");
	std::vector<double> values;
	while (std::getline(input, line)) {
		if (line.empty()) continue;
		const auto fields = SplitCsv(line);
		Require(fields.size() == 2 && std::stoull(fields[0]) == values.size(),
		        "performance value reference has an invalid logical position");
		values.push_back(std::stod(fields[1]));
	}
	return values;
}

double Median(std::vector<double> values) {
	Require(!values.empty(), "timing sample must not be empty");
	std::sort(values.begin(), values.end());
	return values[values.size() / 2];
}

std::uint64_t UInt(const fs::path &json, const std::string &expression, const fs::path &root) {
	const auto result = RunProcess({"jq", "-er", expression, json.string()}, root);
	Require(result.exit_code == 0, "cannot read metric " + expression + ": " + result.output);
	return std::stoull(result.output);
}

std::string StringMetric(const fs::path &json, const std::string &expression, const fs::path &root) {
	const auto result = RunProcess({"jq", "-er", expression, json.string()}, root);
	Require(result.exit_code == 0, "cannot read metric " + expression + ": " + result.output);
	return result.output.substr(0, result.output.find_first_of("\r\n"));
}

struct Query final {
	std::vector<std::vector<std::string>> rows;
	fs::path metrics;
	double elapsed_ms = 0;
};

Query RunQuery(const Options &options, const std::string &name, const std::string &sql, std::uint64_t threads,
               std::uint64_t duckomo_limit) {
	Query result;
	result.metrics = options.output / (name + ".metrics.json");
	const auto full_sql = "LOAD " + SqlLiteral(options.extension.string()) + "; SET threads=" +
	                      std::to_string(threads) + "; SET duckomo_max_threads=" +
	                      std::to_string(duckomo_limit) + "; " + sql;
	const std::vector<std::string> command = {options.duckdb.string(), "-unsigned", "-bail", "-no-stdin", "-csv",
	                                         "-noheader", "-nullvalue", "NULL", "-c", full_sql, ":memory:"};
	const auto child = RunProcess(command, options.root,
	                              {{"DUCKOMO_METRICS_OUTPUT", result.metrics.string()},
	                               {"DUCKOMO_SCENARIO", name}, {"DUCKOMO_FIXTURE_ID", "dimensions_perf"}});
	Require(child.exit_code == 0, "DuckDB scenario '" + name + "' failed: " + child.output);
	Require(fs::is_regular_file(result.metrics), "DuckDB scenario '" + name + "' did not write metrics");
	result.rows = CsvRows(child.output);
	result.elapsed_ms = child.elapsed_ms;
	Require(StringMetric(result.metrics, ".status", options.root) == "success",
	        "DuckDB scenario '" + name + "' did not complete successfully");
	return result;
}

std::vector<std::vector<std::string>> IndependentCoordinates(const fs::path &csv, const std::string &fixture_id) {
	std::istringstream input(ReadText(csv));
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)), "coordinate oracle is empty");
	std::vector<std::vector<std::string>> rows;
	while (std::getline(input, line)) {
		const auto fields = SplitCsv(line);
		if (fields.size() != 14 || fields[0] != fixture_id) continue;
		rows.push_back({fields[12], fields[3], fields[9]});
	}
	std::sort(rows.begin(), rows.end(), [](const auto &left, const auto &right) {
		return std::stoi(left[0]) < std::stoi(right[0]);
	});
	return rows;
}

std::vector<std::vector<std::string>> IndependentFiveAxisCoordinates(const fs::path &csv) {
	std::istringstream input(ReadText(csv));
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)), "coordinate oracle is empty");
	std::vector<std::vector<std::string>> rows;
	while (std::getline(input, line)) {
		const auto fields = SplitCsv(line);
		if (fields.size() != 14 || fields[0] != "dimensions_five_axes") continue;
		rows.push_back({fields[12], fields[3], fields[5], fields[7], fields[9], fields[11], fields[13]});
	}
	Require(rows.size() == 32, "five-axis coordinate oracle has an unexpected row count");
	return rows;
}

std::string AxesSql(const std::string &dimensions) {
	if (dimensions == "time,member") {
		return "dimensions := map(['value'], [['time','member']]), axes := {"
		       "'time': {'axis':'time','values':[TIMESTAMP '2026-09-30 00:00:00',TIMESTAMP '2026-09-30 01:00:00']},"
		       "'member': {'axis':'member','values':[10,20,30]}}";
	}
	return "dimensions := map(['value'], [['member','time']]), axes := {"
	       "'member': {'axis':'member','values':[10,20,30]},"
	       "'time': {'axis':'time','values':[TIMESTAMP '2026-09-30 00:00:00',TIMESTAMP '2026-09-30 01:00:00']}}";
}

std::string FiveAxesSql(const fs::path &file) {
	return "read_om(" + SqlLiteral(file.string()) + ", dimensions := map(['value'], "
	       "[['time','level','lead_time','member','run']]), axes := {"
	       "'time': {'axis':'time','values':[TIMESTAMP '2026-09-30 00:00:00',TIMESTAMP '2026-09-30 01:00:00']},"
	       "'level': {'axis':'level','kind':'pressure','unit':'hPa','values':[500.25,850.5]},"
	       "'lead_time': {'axis':'lead_time','values':[INTERVAL '-1 hour',INTERVAL '0 second']},"
	       "'member': {'axis':'member','values':[10::BIGINT,9223372036854775807::BIGINT]},"
	       "'run': {'axis':'run','values':[TIMESTAMP '2026-09-29 00:00:00',TIMESTAMP '2026-09-29 12:00:00']}})";
}

std::string PerfRead(const fs::path &file) {
	return "read_om(" + SqlLiteral(file.string()) + ", dimensions := map(['value'], [['time','member']]), axes := {"
	       "'time': {'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	       "'member': {'axis':'member','start':0,'step':1}})";
}

std::string JsonEscape(const std::string &value) {
	std::string result = "\"";
	for (const auto ch : value) {
		if (ch == '"' || ch == '\\') result.push_back('\\');
		if (static_cast<unsigned char>(ch) >= 0x20) result.push_back(ch);
	}
	result.push_back('"');
	return result;
}

void WriteReport(const fs::path &path, std::uint64_t full_data, std::uint64_t local_data,
                 std::uint64_t full_chunks, std::uint64_t local_chunks, std::uint64_t workers,
                 std::uint64_t tasks, std::uint64_t rows, const std::vector<double> &serial_ms,
                 const std::vector<double> &two_worker_ms, const std::vector<double> &four_worker_ms) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	Require(output.good(), "cannot create dimensions validation report");
	output << std::fixed << std::setprecision(3);
	output << "{\n  \"schema_version\": 2,\n  \"status\": \"success\",\n"
	       << "  \"coordinate_fixtures\": [\"dimensions\",\"dimensions_axis_order\",\"dimensions_five_axes\"],\n"
	       << "  \"full_data_bytes\": " << full_data << ",\n  \"local_data_bytes\": " << local_data << ",\n"
	       << "  \"full_decoded_chunks\": " << full_chunks << ",\n  \"local_decoded_chunks\": " << local_chunks << ",\n"
	       << "  \"parallel_rows\": " << rows << ",\n  \"parallel_tasks\": " << tasks << ",\n"
	       << "  \"active_workers\": " << workers << ",\n  \"timing_ms\": {\n"
	       << "    \"threads_1\": [";
	for (std::size_t i = 0; i < serial_ms.size(); i++) output << (i ? ", " : "") << serial_ms[i];
	output << "],\n    \"threads_2\": [";
	for (std::size_t i = 0; i < two_worker_ms.size(); i++) output << (i ? ", " : "") << two_worker_ms[i];
	output << "],\n    \"threads_4\": [";
	for (std::size_t i = 0; i < four_worker_ms.size(); i++) output << (i ? ", " : "") << four_worker_ms[i];
	const auto serial_median = Median(serial_ms);
	const auto two_median = Median(two_worker_ms);
	const auto four_median = Median(four_worker_ms);
	output << "],\n    \"median_threads_1\": " << serial_median << ",\n"
	       << "    \"median_threads_2\": " << two_median << ",\n"
	       << "    \"median_threads_4\": " << four_median << "\n  },\n"
	       << "  \"two_worker_speedup_passed\": " << (two_median < serial_median ? "true" : "false") << "\n}\n";
	Require(output.good(), "failed to write dimensions validation report");
}

} // namespace

int main(int argc, char **argv) {
	try {
		const auto options = ParseOptions(argc, argv);
		Require(fs::is_regular_file(options.duckdb), "release DuckDB CLI is missing");
		Require(fs::is_regular_file(options.extension), "release DuckOMO extension is missing");
		Require(fs::is_regular_file(options.fixtures / "dimensions-coordinates.csv"), "independent coordinate CSV is missing");
		Require(fs::is_regular_file(options.fixtures / "dimensions-perf-manifest.json"), "performance manifest is missing");
		fs::create_directories(options.output);

		for (const auto *fixture : {"dimensions", "dimensions_axis_order"}) {
			const auto file = (options.fixtures / (std::string(fixture) + ".om")).string();
			const auto axes = AxesSql(std::string(fixture) == "dimensions" ? "time,member" : "member,time");
			const auto sql = std::string("SELECT value::INTEGER, strftime(valid_time, '%Y-%m-%d %H:%M:%S'), member::INTEGER FROM ") +
			                 "read_om(" + SqlLiteral(file) + ", " + axes + ") ORDER BY value";
			const auto result = RunQuery(options, std::string(fixture), sql, 1, 1);
			const auto expected = IndependentCoordinates(options.fixtures / "dimensions-coordinates.csv", fixture);
			WriteCsvRows(options.output / (std::string(fixture) + ".actual.csv"), result.rows);
			if (result.rows != expected) {
				WriteCsvRows(options.output / (std::string(fixture) + ".oracle.csv"), expected);
				std::vector<std::vector<std::string>> diff {{"expected_value", "expected_valid_time", "expected_member",
				                                           "actual_value", "actual_valid_time", "actual_member"}};
				for (std::size_t row = 0; row < std::max(result.rows.size(), expected.size()); row++) {
					const auto actual = row < result.rows.size() ? result.rows[row] : std::vector<std::string>(3, "<missing>");
					const auto oracle = row < expected.size() ? expected[row] : std::vector<std::string>(3, "<missing>");
					if (actual != oracle) diff.push_back({oracle[0], oracle[1], oracle[2], actual[0], actual[1], actual[2]});
				}
				WriteCsvRows(options.output / (std::string(fixture) + ".diff.csv"), diff);
				throw std::runtime_error(std::string(fixture) + " rows differ from the independent coordinate/value oracle; see .diff.csv");
			}
		}

		const auto five_axis_file = options.fixtures / "dimensions_five_axes.om";
		const auto five_axis_sql = "SELECT coalesce(value::INTEGER::VARCHAR, 'NULL'), "
		                           "strftime(valid_time, '%Y-%m-%d %H:%M:%S'), level::VARCHAR, "
		                           "lead_time::VARCHAR, member::VARCHAR, strftime(run, '%Y-%m-%d %H:%M:%S'), "
		                           "(value IS NULL)::VARCHAR FROM " + FiveAxesSql(five_axis_file) +
		                           " ORDER BY valid_time, level, lead_time, member, run";
		const auto five_axis_result = RunQuery(options, "dimensions_five_axes", five_axis_sql, 1, 1);
		const auto five_axis_expected = IndependentFiveAxisCoordinates(options.fixtures / "dimensions-coordinates.csv");
		WriteCsvRows(options.output / "dimensions_five_axes.actual.csv", five_axis_result.rows);
		if (five_axis_result.rows != five_axis_expected) {
			WriteCsvRows(options.output / "dimensions_five_axes.oracle.csv", five_axis_expected);
			throw std::runtime_error("five semantic coordinate columns, values, or missing positions differ from the "
			                         "independent coordinate oracle");
		}

		const auto perf_file = options.fixtures / "dimensions_perf.om";
		const auto perf = PerfRead(perf_file);
		const auto reference_values = ReadReferenceValues(options.fixtures / "dimensions_perf.reference.csv");
		Require(reference_values.size() == 131072, "performance value oracle row count differs from manifest");
		double expected_full_sum = 0;
		for (const auto value : reference_values) expected_full_sum += value;
		double expected_local_sum = 0;
		for (std::uint64_t member = 100; member < 104; member++) expected_local_sum += reference_values.at(member);
		const auto full = RunQuery(options, "g2_full", "SELECT sum(value) FROM " + perf, 1, 1);
		const auto local = RunQuery(options, "g2_local",
		                            "SELECT sum(value) FROM " + perf +
		                                " WHERE valid_time = TIMESTAMP '2026-01-01 00:00:00' AND member BETWEEN 100 AND 103",
		                            1, 1);
		const auto full_data = UInt(full.metrics, "([.variables[].data_bytes] | add // 0)", options.root);
		const auto local_data = UInt(local.metrics, "([.variables[].data_bytes] | add // 0)", options.root);
		const auto full_chunks = UInt(full.metrics, "([.variables[].decoded_chunks] | add // 0)", options.root);
		const auto local_chunks = UInt(local.metrics, "([.variables[].decoded_chunks] | add // 0)", options.root);
		Require(full.rows.size() == 1 && std::abs(std::stod(full.rows[0][0]) - expected_full_sum) < 0.01,
		        "G2 full scan sum differs from the independent official-reader value oracle");
		Require(local.rows.size() == 1 && std::abs(std::stod(local.rows[0][0]) - expected_local_sum) < 0.0001,
		        "G2 restricted scan sum differs from the independent official-reader value oracle");
		Require(full_data > local_data && full_chunks > local_chunks,
		        "G2 local semantic selection did not reduce both value bytes and decoded chunks");
		Require(StringMetric(local.metrics, ".selection_mode", options.root) == "axis_restricted",
		        "G2 local scenario did not report semantic axis restriction");

		const auto empty = RunQuery(options, "g2_empty", "SELECT value FROM " + perf + " WHERE member = -1", 1, 1);
		Require(empty.rows.empty(), "G2 empty value selection returned rows");
		Require(UInt(empty.metrics,
		             "([.variables[].index_bytes] | add // 0) + ([.variables[].data_bytes] | add // 0) + ([.variables[].decoded_chunks] | add // 0)",
		             options.root) == 0,
		        "G2 empty selection performed value index/data/decode work");

		const auto coordinate = RunQuery(options, "g2_coordinate",
		                                 "SELECT count(*) FROM " + perf +
		                                     " WHERE valid_time = TIMESTAMP '2026-01-01 00:00:00' AND member BETWEEN 100 AND 103",
		                                 1, 1);
		Require(coordinate.rows.size() == 1 && coordinate.rows[0][0] == "4",
		        "coordinate-only semantic selection returned an incorrect cardinality");
		Require(UInt(coordinate.metrics,
		             "([.variables[].index_bytes] | add // 0) + ([.variables[].data_bytes] | add // 0) + ([.variables[].decoded_chunks] | add // 0)",
		             options.root) == 0,
		        "coordinate-only query performed value index/data/decode work");

		const auto count = RunQuery(options, "g2_count", "SELECT count(*) FROM " + perf, 1, 1);
		Require(count.rows.size() == 1 && count.rows[0][0] == "131072", "no-value count returned the wrong cardinality");
		Require(UInt(count.metrics, "([.variables[].index_bytes] | add // 0) + ([.variables[].data_bytes] | add // 0) + ([.variables[].decoded_chunks] | add // 0)", options.root) == 0,
		        "no-value count performed value index/data/decode work");

		std::vector<double> timings[3];
		std::uint64_t workers = 0;
		std::uint64_t tasks = 0;
		std::string parallel_result;
		for (std::uint64_t thread_index = 0; thread_index < 3; thread_index++) {
			const auto threads = std::uint64_t(1) << thread_index;
			const auto limit = threads;
			for (std::uint64_t repetition = 0; repetition < 5; repetition++) {
				const auto scenario = "g4_t" + std::to_string(threads) + "_r" + std::to_string(repetition + 1);
				const auto run = RunQuery(options, scenario, "SELECT sum(value) FROM " + perf, threads, limit);
				Require(run.rows.size() == 1 && std::abs(std::stod(run.rows[0][0]) - expected_full_sum) < 0.01,
				        scenario + " result differs from the independent full-scan value oracle");
				timings[thread_index].push_back(run.elapsed_ms);
				if (thread_index == 1 && repetition == 4) {
					workers = UInt(run.metrics, ".active_workers", options.root);
					tasks = UInt(run.metrics, ".scan_tasks_claimed", options.root);
				}
			}
		}
		Require(workers >= 2 && tasks >= 2, "G4 scan did not demonstrate two active workers and claimed tasks");

		WriteReport(options.output / "summary.json", full_data, local_data, full_chunks, local_chunks,
		            workers, tasks, 131072, timings[0], timings[1], timings[2]);
		std::cout << "duckomo_dimensions_validation: G0/G1 oracle, G2 full/restricted/empty/coordinate/count, "
		             "and G4 1/2/4-thread five-run checks passed; two-worker median speedup: "
	          << (Median(timings[1]) < Median(timings[0]) ? "yes" : "no (recorded in summary.json)") << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "duckomo_dimensions_validation: " << error.what() << '\n';
		return 1;
	}
}
