#include "validation_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/utsname.h>
#include <tuple>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using duckomo_validation_support::CommandResult;
using duckomo_validation_support::RunProcess;
using duckomo_validation_support::SpatialEvidenceExpectation;
using duckomo_validation_support::ValidateSpatialMetricsEvidence;

namespace {

constexpr const char *PROJECTION_SHA256 = "fb36284bf8f932c82b171c7bce28b6e767b95321d1d986dc0a1dbd3b84d88a43";
constexpr const char *PROJECTION_REFERENCE_SHA256 = "00a0e2d2e27a2ecc4f59e74d1264830ba2bbfafa679ea2790fd4d7791b997aed";
constexpr const char *DOMAIN_SHA256 = "0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd";
constexpr std::uint64_t DOMAIN_BYTES = 5812040;
constexpr std::uint64_t ROWS = 83 * 127;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::string JsonEscape(const std::string &value) {
	static constexpr char HEX[] = "0123456789abcdef";
	std::string output = "\"";
	for (const auto character_raw : value) {
		const auto character = static_cast<unsigned char>(character_raw);
		switch (character) {
		case '"': output += "\\\""; break;
		case '\\': output += "\\\\"; break;
		case '\n': output += "\\n"; break;
		case '\r': output += "\\r"; break;
		case '\t': output += "\\t"; break;
		default:
			if (character < 0x20) {
				output += "\\u00";
				output.push_back(HEX[character >> 4]);
				output.push_back(HEX[character & 0x0f]);
			} else {
				output.push_back(static_cast<char>(character));
			}
		}
	}
	output.push_back('"');
	return output;
}

struct Options final {
	fs::path root;
	fs::path fixtures;
	fs::path output;
	fs::path duckdb;
	fs::path extension;
	fs::path domain_file;
};

Options ParseOptions(int argc, char **argv) {
	const auto executable = fs::weakly_canonical(fs::absolute(argv[0]));
	Options options;
	options.root = executable.parent_path().parent_path().parent_path().parent_path().parent_path();
	options.fixtures = options.root / "test/data";
	options.output = options.root / "build/evidence/spatial";
	options.duckdb = options.root / "build/release/duckdb";
	options.extension = options.root / "build/release/extension/duckomo/duckomo.duckdb_extension";
	for (int i = 1; i < argc; i++) {
		const std::string name = argv[i];
		if (name == "--help" || name == "-h") {
			std::cout << "Usage: duckomo_spatial_validation --root PATH --fixtures PATH --output PATH "
			             "--duckdb PATH --extension PATH --domain-file PATH\n";
			std::exit(0);
		}
		Require(i + 1 < argc, "missing value after " + name);
		const fs::path value = argv[++i];
		if (name == "--root") options.root = value;
		else if (name == "--fixtures") options.fixtures = value;
		else if (name == "--output") options.output = value;
		else if (name == "--duckdb") options.duckdb = value;
		else if (name == "--extension") options.extension = value;
		else if (name == "--domain-file") options.domain_file = value;
		else throw std::runtime_error("unknown argument " + name);
	}
	Require(!options.domain_file.empty(), "--domain-file is required; the verified real-domain sample cannot be replaced by a synthetic fixture");
	options.root = fs::weakly_canonical(fs::absolute(options.root));
	auto resolve = [&](fs::path &path) {
		if (path.is_relative()) path = fs::current_path() / path;
		path = fs::weakly_canonical(path);
	};
	resolve(options.fixtures);
	resolve(options.output);
	resolve(options.duckdb);
	resolve(options.extension);
	resolve(options.domain_file);
	return options;
}

std::string ReadText(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "cannot read file " + path.string());
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void WriteText(const fs::path &path, const std::string &content) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	Require(output.good(), "cannot write file " + path.string());
	output << content;
	Require(output.good(), "failed while writing file " + path.string());
}

std::string Sha256(const fs::path &path, const fs::path &root) {
	const auto result = RunProcess({"sha256sum", "--", path.string()}, root);
	Require(result.exit_code == 0, "sha256sum failed for " + path.string() + ": " + result.output);
	const auto end = result.output.find_first_of(" \t\r\n");
	Require(end == 64, "sha256sum returned a malformed digest for " + path.string());
	return result.output.substr(0, end);
}

std::string SqlLiteral(const std::string &text) {
	std::string result = "'";
	for (const auto character : text) {
		result.push_back(character);
		if (character == '\'') result.push_back('\'');
	}
	result.push_back('\'');
	return result;
}

std::vector<std::string> QueryCommand(const Options &options, const std::string &sql) {
	const auto execution_sql = "SET threads=1; LOAD " + SqlLiteral(options.extension.string()) + "; " + sql;
	return {options.duckdb.string(), "-unsigned", "-bail", "-no-stdin", "-csv", "-noheader", "-nullvalue", "NULL",
	        "-c", execution_sql, ":memory:"};
}

std::string JsonArray(const std::vector<std::string> &values) {
	std::ostringstream output;
	output << '[';
	for (std::size_t index = 0; index < values.size(); index++) {
		if (index != 0) output << ',';
		output << JsonEscape(values[index]);
	}
	output << ']';
	return output.str();
}

std::vector<std::string> SplitCsvLine(const std::string &line) {
	std::vector<std::string> result;
	std::string field;
	bool quoted = false;
	for (std::size_t i = 0; i < line.size(); i++) {
		const auto character = line[i];
		if (quoted) {
			if (character == '"' && i + 1 < line.size() && line[i + 1] == '"') {
				field.push_back('"');
				i++;
			} else if (character == '"') {
				quoted = false;
			} else {
				field.push_back(character);
			}
		} else if (character == '"' && field.empty()) {
			quoted = true;
		} else if (character == ',') {
			result.emplace_back(std::move(field));
			field.clear();
		} else {
			field.push_back(character);
		}
	}
	Require(!quoted, "DuckDB CSV output has an unterminated quoted field");
	result.emplace_back(std::move(field));
	return result;
}

std::vector<std::vector<std::string>> ParseCsv(const std::string &text, std::size_t columns) {
	std::vector<std::vector<std::string>> rows;
	std::istringstream input(text);
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		auto row = SplitCsvLine(line);
		Require(row.size() == columns, "DuckDB CSV output column count does not match the scenario contract");
		rows.emplace_back(std::move(row));
	}
	return rows;
}

double ParseDouble(const std::string &value, const std::string &context) {
	std::size_t used = 0;
	double parsed = 0;
	try {
		parsed = std::stod(value, &used);
	} catch (const std::exception &) {
		throw std::runtime_error(context + " is not a DOUBLE: '" + value + "'");
	}
	Require(used == value.size(), context + " contains trailing data");
	return parsed;
}

std::string Jq(const fs::path &file, const std::string &expression, const fs::path &root) {
	const auto result = RunProcess({"jq", "-er", expression, file.string()}, root);
	Require(result.exit_code == 0, "metrics validation failed for " + file.string() + ": " + result.output);
	auto output = result.output;
	while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
	return output;
}

std::uint64_t Metric(const fs::path &file, const std::string &expression, const fs::path &root) {
	const auto value = Jq(file, expression, root);
	Require(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos,
	        "metrics counter is not an unsigned integer: " + expression);
	return static_cast<std::uint64_t>(std::stoull(value));
}

std::string SpatialSource(const fs::path &fixture, bool domain = false) {
	if (domain) {
		return "read_om(" + SqlLiteral(fixture.string()) + ", domain := 'ncep_gfswave025')";
	}
	return "read_om(" + SqlLiteral(fixture.string()) +
	       ", dimensions := map(['humidity','pressure','temperature'], [['row','column'],['row','column'],['row','column']]), "
	       "grid := {'nx':127,'ny':83,'lat0':-41.0,'lon0':-126.0,'dlat':1.0,'dlon':2.0,'order':'separate'}, "
	       "spatial_axes := ['row','column'])";
}

struct ScenarioResult final {
	std::string name;
	std::string fixture_id;
	std::string fixture_sha;
	std::string sql;
	std::string comparison;
	std::vector<std::vector<std::string>> rows;
	fs::path metrics_file;
	CommandResult child;
	std::uint64_t candidate_rows = 0;
	std::string selection_mode;
};

ScenarioResult RunScenario(const Options &options, const std::string &name, const std::string &sql,
	                       std::size_t columns, const std::string &fixture_id, const std::string &fixture_sha,
	                       const std::string &comparison) {
	ScenarioResult result;
	result.name = name;
	result.fixture_id = fixture_id;
	result.fixture_sha = fixture_sha;
	result.sql = sql;
	result.comparison = comparison;
	result.metrics_file = options.output / (name + ".metrics.json");
	const auto command = QueryCommand(options, sql);
	const std::map<std::string, std::string> environment = {
	    {"DUCKOMO_METRICS_OUTPUT", result.metrics_file.string()}, {"DUCKOMO_SCENARIO", name},
	    {"DUCKOMO_FIXTURE_ID", fixture_id}, {"DUCKOMO_FIXTURE_SHA256", fixture_sha}};
	result.child = RunProcess(command, options.root, environment);
	Require(result.child.exit_code == 0,
	        "scenario " + name + " failed with exit " + std::to_string(result.child.exit_code) + ": " +
	            result.child.output.substr(0, 2000));
	Require(fs::is_regular_file(result.metrics_file), "scenario " + name + " did not produce its query metrics sidecar");
	const auto variable_contract = fixture_id == "projection"
	                                   ? "(.variables | has(\"/humidity\") and has(\"/pressure\") and has(\"/temperature\"))"
	                                   : "(.variables | type == \"object\" and length == 15)";
	const auto valid = Jq(result.metrics_file,
	                      ".schema_version == 2 and .status == \"success\" and .decode_count_complete == true and "
	                      ".fixture_id == " + JsonEscape(fixture_id) + " and .fixture_sha256 == " + JsonEscape(fixture_sha) +
	                      " and " + variable_contract +
	                      " and all(.variables[]; (.index_bytes | type == \"number\" and . >= 0) and "
	                      "(.index_requests | type == \"number\" and . >= 0) and "
	                      "(.data_bytes | type == \"number\" and . >= 0) and "
	                      "(.data_requests | type == \"number\" and . >= 0) and "
	                      "(.decoded_chunks | type == \"number\" and . >= 0) and .decode_count_complete == true)",
	                      options.root);
	Require(valid == "true", "scenario " + name + " metrics are incomplete or have the wrong fixture identity");
	result.selection_mode = Jq(result.metrics_file, ".selection_mode", options.root);
	result.candidate_rows = Metric(result.metrics_file, ".candidate_rows", options.root);
	result.rows = ParseCsv(result.child.output, columns);
	return result;
}

void CompleteScenarioMetrics(const Options &options, const ScenarioResult &scenario) {
	struct utsname host {};
	std::string machine = "unknown";
	std::string system = "unknown";
	std::string release = "unknown";
	if (uname(&host) == 0) {
		machine = host.machine;
		system = host.sysname;
		release = host.release;
	}
	const auto command = JsonArray(QueryCommand(options, scenario.sql));
	std::ostringstream elapsed;
	elapsed << std::setprecision(17) << scenario.child.elapsed_ms;
	const auto result_rows = std::to_string(scenario.rows.size());
	const auto peak_rss = std::to_string(scenario.child.peak_rss_bytes);
	const auto reference = scenario.fixture_id == "projection"
	                          ? std::string("projection.temperature.reference.csv#") + PROJECTION_REFERENCE_SHA256 +
	                                ";independent_axis_formula_and_materialized_full_scan"
	                          : std::string("domain-manifest.json#") + DOMAIN_SHA256 + ";cardinality-only";
	const bool position_comparison = scenario.name == "full" || scenario.name == "restricted" ||
	                                 scenario.name == "coordinates" || scenario.name == "mixed" ||
	                                 scenario.name == "fallback";
	const bool null_comparison = position_comparison;
	const auto filter = ".result_rows = $rows | .comparison_passed = true | .comparison = $comparison | "
	                    ".reference_identity = $reference | .coordinate_tolerance = $coordinate_tolerance | "
	                    ".logical_positions_match = $positions | .null_positions_match = $nulls | "
	                    ".elapsed_ms = $elapsed | .peak_rss_bytes = $rss | .child_exit_code = 0 | .command = $command | "
	                    ".environment += {system:$system, release:$release, machine:$machine, threads:\"1\"} | "
	                    ".cache_policy += {os_page_cache:\"not cleared; comparisons use the same host conditions\"}";
	const auto completed = RunProcess({"jq", "-c", "--argjson", "rows", result_rows, "--arg", "comparison",
	                                   scenario.comparison, "--argjson", "elapsed", elapsed.str(), "--argjson", "rss",
	                                   peak_rss, "--argjson", "command", command, "--arg", "reference", reference,
	                                   "--argjson", "positions", position_comparison ? "true" : "null", "--argjson",
	                                   "nulls", null_comparison ? "true" : "null", "--argjson", "coordinate_tolerance",
	                                   "1e-9", "--arg", "system", system, "--arg",
	                                   "release", release, "--arg", "machine", machine, filter,
	                                   scenario.metrics_file.string()},
	                                  options.root);
	Require(completed.exit_code == 0, "cannot complete query evidence for " + scenario.name + ": " + completed.output);
	WriteText(scenario.metrics_file, completed.output);
	Require(Jq(scenario.metrics_file,
	           ".schema_version == 2 and .status == \"success\" and .comparison_passed == true and "
	           ".result_rows != null and .elapsed_ms != null and .peak_rss_bytes != null and "
	           ".reference_identity != \"\" and .coordinate_tolerance == 1e-9 and "
	           "(.command | type == \"array\" and length > 0)",
	           options.root) == "true",
	        "completed query evidence is missing result, comparison, process, or environment fields");
}

void VerifyCoordinatesAgainstFull(const ScenarioResult &full, const ScenarioResult &coordinates) {
	std::map<std::pair<double, double>, bool> full_positions;
	for (const auto &row : full.rows) {
		full_positions[{ParseDouble(row[2], "full latitude"), ParseDouble(row[3], "full longitude")}] = true;
	}
	Require(coordinates.rows.size() == 25, "coordinate-only query must contain 25 rows");
	for (const auto &row : coordinates.rows) {
		const auto latitude = ParseDouble(row[0], "coordinate-only latitude");
		const auto longitude = ParseDouble(row[1], "coordinate-only longitude");
		Require(latitude >= -2 && latitude <= 2 && longitude >= -4 && longitude <= 4,
		        "coordinate-only output violates its exact SQL predicate");
		Require(full_positions.find({latitude, longitude}) != full_positions.end(),
		        "coordinate-only output is absent from the materialized full coordinate set");
	}
}

void RequireAllValueCountersZero(const Options &options, const ScenarioResult &scenario) {
	for (const auto *path : {"/humidity", "/pressure", "/temperature"}) {
		const auto prefix = std::string(".variables[") + JsonEscape(path) + "].";
		for (const auto *field : {"index_bytes", "index_requests", "data_bytes", "data_requests", "decoded_chunks"}) {
			Require(Metric(scenario.metrics_file, prefix + field, options.root) == 0,
			        scenario.name + " must contain an explicit zero " + field + " for " + path);
		}
		Require(Jq(scenario.metrics_file, prefix + "decode_count_complete", options.root) == "true",
		        scenario.name + " must mark zero decode accounting complete for " + path);
	}
}

void WriteScenarioEvidence(const Options &options, const ScenarioResult &scenario) {
	std::ostringstream json;
	json << "{\"schema_version\":2,\"scenario\":" << JsonEscape(scenario.name)
	     << ",\"status\":\"success\",\"fixture_id\":" << JsonEscape(scenario.fixture_id)
	     << ",\"fixture_sha256\":" << JsonEscape(scenario.fixture_sha)
	     << ",\"sql\":" << JsonEscape(scenario.sql)
	     << ",\"selection_mode\":" << JsonEscape(scenario.selection_mode) << ",\"candidate_rows\":" << scenario.candidate_rows
	     << ",\"optimizer_empty\":" << (scenario.name == "optimizer_empty" ? "true" : "false")
	     << ",\"result_rows\":" << scenario.rows.size() << ",\"comparison_passed\":true,\"comparison\":"
	     << JsonEscape(scenario.comparison) << ",\"metrics_sidecar\":" << JsonEscape(scenario.metrics_file.filename().string())
	     << ",\"elapsed_ms\":" << std::setprecision(17) << scenario.child.elapsed_ms << ",\"peak_rss_bytes\":"
	     << scenario.child.peak_rss_bytes << ",\"child_exit_code\":" << scenario.child.exit_code << ",\"command\":[";
	const auto command = QueryCommand(options, scenario.sql);
	for (std::size_t index = 0; index < command.size(); index++) {
		if (index != 0) json << ',';
		json << JsonEscape(command[index]);
	}
	json << "]}\n";
	WriteText(options.output / (scenario.name + ".json"), json.str());
}

std::string QueryPlan(const Options &options, const std::string &sql) {
	const auto execution_sql = "LOAD " + SqlLiteral(options.extension.string()) + "; EXPLAIN " + sql;
	const std::vector<std::string> command = {options.duckdb.string(), "-unsigned", "-bail", "-no-stdin", "-csv", "-noheader",
	                                          "-c", execution_sql, ":memory:"};
	const auto result = RunProcess(command, options.root);
	Require(result.exit_code == 0, "EXPLAIN failed: " + result.output);
	return result.output;
}

void AnnotateOptimizerEmpty(const Options &options, ScenarioResult &scenario, const std::string &plan) {
	Require(plan.find("EMPTY_RESULT") != std::string::npos,
	        "optimizer_empty requires an EXPLAIN EMPTY_RESULT plan; missing sidecar is not evidence");
	Require(scenario.rows.size() == 1 && scenario.rows[0][0] == "0",
	        "optimizer_empty requires a complete successful zero-result value");
	Require(Metric(scenario.metrics_file, ".bind_metadata_bytes", options.root) > 0,
	        "optimizer_empty requires bind metadata evidence");
	const auto annotated = RunProcess(
	    {"jq", "-c", ".selection_mode = \"empty\" | .candidate_rows = 0 | .optimizer_empty = true",
	     scenario.metrics_file.string()},
	    options.root);
	Require(annotated.exit_code == 0, "cannot mark optimizer-eliminated scan evidence: " + annotated.output);
	WriteText(scenario.metrics_file, annotated.output);
	scenario.selection_mode = "empty";
	scenario.candidate_rows = 0;
	Require(Jq(scenario.metrics_file, ".optimizer_empty == true and .candidate_rows == 0 and .selection_mode == \"empty\"",
	           options.root) == "true",
	        "optimizer_empty sidecar annotation is incomplete");
}

std::vector<double> ReadIndependentValues(const fs::path &reference) {
	std::ifstream input(reference);
	Require(input.good(), "independent value reference is missing: " + reference.string());
	std::string line;
	std::getline(input, line);
	Require(line == "index,value", "temperature reference has an invalid CSV header");
	std::vector<double> values;
	std::uint64_t expected_index = 0;
	while (std::getline(input, line)) {
		const auto comma = line.find(',');
		Require(comma != std::string::npos, "temperature reference contains a malformed row");
		const auto index = static_cast<std::uint64_t>(std::stoull(line.substr(0, comma)));
		Require(index == expected_index, "temperature reference positions are not complete and ordered");
		values.push_back(ParseDouble(line.substr(comma + 1), "temperature reference value"));
		expected_index++;
	}
	return values;
}

void VerifyFullReference(const ScenarioResult &full, const std::vector<double> &reference) {
	Require(full.rows.size() == ROWS && reference.size() == ROWS, "full scan and official value reference must contain 10,541 rows");
	for (std::size_t index = 0; index < full.rows.size(); index++) {
		const auto source_index = static_cast<std::uint64_t>(std::stoull(full.rows[index][0]));
		Require(source_index == index, "full scan did not preserve source order at row " + std::to_string(index));
		const auto value = ParseDouble(full.rows[index][1], "full-scan temperature");
		Require(value == reference[index], "full-scan temperature differs from official reader reference at row " +
		                                     std::to_string(index));
		const auto y = static_cast<double>(index / 127);
		const auto x = static_cast<double>(index % 127);
		const auto latitude = -41.0 + y;
		double longitude = std::fmod((-126.0 + x * 2.0) + 180.0, 360.0);
		if (longitude < 0) longitude += 360.0;
		longitude -= 180.0;
		Require(std::abs(ParseDouble(full.rows[index][2], "full-scan latitude") - latitude) <= 1e-9,
		        "full-scan latitude differs from the independent axis formula at row " + std::to_string(index));
		Require(std::abs(ParseDouble(full.rows[index][3], "full-scan longitude") - longitude) <= 1e-9,
		        "full-scan longitude differs from the independent axis formula at row " + std::to_string(index));
	}
}

void VerifyRestrictedAgainstMaterialized(const ScenarioResult &full, const ScenarioResult &restricted) {
	std::map<std::pair<double, double>, double> baseline;
	for (const auto &row : full.rows) {
		const auto latitude = ParseDouble(row[2], "baseline latitude");
		const auto longitude = ParseDouble(row[3], "baseline longitude");
		if (latitude >= -2.0 && latitude <= 2.0 && longitude >= -4.0 && longitude <= 4.0) {
			baseline.emplace(std::make_pair(latitude, longitude), ParseDouble(row[1], "baseline temperature"));
		}
	}
	Require(restricted.rows.size() == 25 && baseline.size() == 25,
	        "restricted scan and materialized baseline must both contain the fixed 25-position window");
	for (const auto &row : restricted.rows) {
		const auto temperature = ParseDouble(row[0], "restricted temperature");
		const auto latitude = ParseDouble(row[1], "restricted latitude");
		const auto longitude = ParseDouble(row[2], "restricted longitude");
		const auto match = baseline.find({latitude, longitude});
		Require(match != baseline.end() && match->second == temperature,
		        "restricted row differs from its materialized source position");
	}
}

void VerifyFallbackAgainstMaterialized(const ScenarioResult &full, const ScenarioResult &fallback) {
	using RowKey = std::tuple<double, double, double>;
	std::map<RowKey, std::size_t> expected;
	std::map<RowKey, std::size_t> actual;
	for (const auto &row : full.rows) {
		const auto temperature = ParseDouble(row[1], "fallback baseline temperature");
		const auto latitude = ParseDouble(row[2], "fallback baseline latitude");
		const auto longitude = ParseDouble(row[3], "fallback baseline longitude");
		if (longitude >= 124 || longitude <= -124) expected[{latitude, longitude, temperature}]++;
	}
	for (const auto &row : fallback.rows) {
		actual[{ParseDouble(row[1], "fallback latitude"), ParseDouble(row[2], "fallback longitude"),
		        ParseDouble(row[0], "fallback temperature")}]++;
	}
	Require(expected.size() == 332 && expected == actual,
	        "seam OR fallback must preserve the exact multiset from materialized full temperature output");
}

void VerifyMixedAgainstMaterialized(const ScenarioResult &full_mixed, const ScenarioResult &mixed) {
	using RowKey = std::tuple<double, double, double>;
	std::map<RowKey, std::size_t> expected;
	std::map<RowKey, std::size_t> actual;
	for (const auto &row : full_mixed.rows) {
		const auto humidity = ParseDouble(row[0], "mixed baseline humidity");
		const auto temperature = ParseDouble(row[1], "mixed baseline temperature");
		const auto latitude = ParseDouble(row[2], "mixed baseline latitude");
		const auto longitude = ParseDouble(row[3], "mixed baseline longitude");
		if (humidity == 96 && latitude >= -41 && latitude <= -37 && longitude >= 50 && longitude <= 70) {
			expected[{latitude, longitude, temperature}]++;
		}
	}
	for (const auto &row : mixed.rows) {
		actual[{ParseDouble(row[1], "mixed latitude"), ParseDouble(row[2], "mixed longitude"),
		        ParseDouble(row[0], "mixed temperature")}]++;
	}
	Require(expected.size() == 1 && expected == actual,
	        "mixed spatial/value filter must preserve the exact materialized source-row multiset");
}

CommandResult RunDomainOracle(const Options &options, const std::string &domain_sha) {
	const auto executable = options.root / "build/release/test/native/domain_reference_test";
	const auto references = options.root / "build/evidence/spatial/reference";
	Require(fs::is_regular_file(executable), "domain reference verifier is missing: " + executable.string());
	const auto command = std::vector<std::string>{executable.string(), options.domain_file.string(),
	                                              (options.fixtures / "domain-manifest.json").string(),
	                                              references.string()};
	const auto result = RunProcess(command, options.root);
	Require(result.exit_code == 0,
	        "domain explicit/registry full oracle comparison failed: " + result.output.substr(0, 2000));
	struct utsname host {};
	std::string machine = "unknown";
	if (uname(&host) == 0) machine = host.machine;
	std::ostringstream json;
	json << "{\"status\":\"success\",\"sample_sha256\":" << JsonEscape(domain_sha)
	     << ",\"rows_per_variable\":1038240,\"official_variables\":15,\"coordinate_tolerance\":1e-9"
	     << ",\"explicit_grid_matches_registered_domain\":true,\"both_match_official_values_and_independent_coordinates\":true"
	     << ",\"elapsed_ms\":" << std::setprecision(17) << result.elapsed_ms
	     << ",\"peak_rss_bytes\":" << result.peak_rss_bytes << ",\"machine\":" << JsonEscape(machine)
	     << ",\"child_exit_code\":" << result.exit_code << ",\"command\":" << JsonArray(command) << "}\n";
	WriteText(options.output / "domain_reference.json", json.str());
	return result;
}

void VerifyDomainIdentity(const Options &options, std::string &domain_sha) {
	Require(fs::is_regular_file(options.domain_file), "required real-domain file is missing: " + options.domain_file.string());
	Require(fs::file_size(options.domain_file) == DOMAIN_BYTES, "real-domain sample byte length differs from the pinned identity");
	domain_sha = Sha256(options.domain_file, options.root);
	Require(domain_sha == DOMAIN_SHA256, "real-domain sample SHA-256 differs from the pinned identity");
	const auto manifest = options.fixtures / "domain-manifest.json";
	Require(fs::is_regular_file(manifest), "real-domain identity manifest is missing");
	const auto manifest_text = ReadText(manifest);
	Require(manifest_text.find(DOMAIN_SHA256) != std::string::npos &&
	            manifest_text.find("\"bytes\": 5812040") != std::string::npos &&
	            manifest_text.find("\"domain\": \"ncep_gfswave025\"") != std::string::npos,
	        "real-domain manifest does not declare the pinned source identity");
}

} // namespace

int main(int argc, char **argv) {
	try {
		const auto options = ParseOptions(argc, argv);
		Require(fs::is_regular_file(options.duckdb), "release DuckDB CLI is missing: " + options.duckdb.string());
		Require(fs::is_regular_file(options.extension), "release extension is missing: " + options.extension.string());
		Require(fs::is_regular_file(options.fixtures / "manifest.json"), "synthetic fixture manifest is missing");
		const auto projection = options.fixtures / "projection.om";
		Require(Sha256(projection, options.root) == PROJECTION_SHA256, "projection fixture SHA-256 differs from its pinned identity");
		const auto temperature_reference = options.fixtures / "projection.temperature.reference.csv";
		Require(Sha256(temperature_reference, options.root) == PROJECTION_REFERENCE_SHA256,
		        "independent temperature reference SHA-256 differs from its pinned identity");
		const auto reference_values = ReadIndependentValues(temperature_reference);
		std::string domain_sha;
		VerifyDomainIdentity(options, domain_sha);
		fs::create_directories(options.output);

		const auto source = SpatialSource(projection);
		const auto full = RunScenario(options, "full",
		                              "SELECT row_number() OVER () - 1, temperature, lat, lon FROM " + source,
		                              4, "projection", PROJECTION_SHA256, "official_temperature_reference_and_axis_formula");
		Require(full.rows.size() == ROWS && full.selection_mode == "full" && full.candidate_rows == ROWS,
		        "full scenario has the wrong row count or selection metadata");
		VerifyFullReference(full, reference_values);

		const auto mixed_full = RunScenario(options, "mixed_baseline",
		                                   "SELECT humidity, temperature, lat, lon FROM " + source,
		                                   4, "projection", PROJECTION_SHA256,
		                                   "complete_materialized_baseline_for_mixed_value_and_spatial_filter");
		Require(mixed_full.rows.size() == ROWS && mixed_full.selection_mode == "full",
		        "mixed filter materialized baseline must consume the complete unfiltered relation");

		const auto restricted = RunScenario(
		    options, "restricted",
		    "SELECT temperature, lat, lon FROM " + source +
		        " WHERE lat BETWEEN -2 AND 2 AND lon BETWEEN -4 AND 4",
		    3, "projection", PROJECTION_SHA256, "exact_difference_from_materialized_full_output");
		Require(restricted.selection_mode == "restricted" && restricted.candidate_rows == 25,
		        "restricted scenario did not publish the expected 25 candidate rows");
		VerifyRestrictedAgainstMaterialized(full, restricted);
		const auto full_data_bytes = Metric(full.metrics_file, ".variables[\"/temperature\"].data_bytes", options.root);
		const auto restricted_data_bytes = Metric(restricted.metrics_file, ".variables[\"/temperature\"].data_bytes", options.root);
		const auto full_chunks = Metric(full.metrics_file, ".variables[\"/temperature\"].decoded_chunks", options.root);
		const auto restricted_chunks = Metric(restricted.metrics_file, ".variables[\"/temperature\"].decoded_chunks", options.root);
		Require(restricted_data_bytes < full_data_bytes && restricted_chunks < full_chunks,
		        "restricted temperature data bytes and decoded chunks must both be strictly below full scan");

		const auto empty = RunScenario(options, "empty", "SELECT temperature FROM " + source + " WHERE lat > 90",
		                              1, "projection", PROJECTION_SHA256, "empty_spatial_selection");
		Require(empty.rows.empty() && empty.selection_mode == "empty" && empty.candidate_rows == 0,
		        "empty scenario did not finish with zero rows and empty selection metadata");
		RequireAllValueCountersZero(options, empty);

		const auto coordinates = RunScenario(
		    options, "coordinates", "SELECT lat, lon FROM " + source +
		                              " WHERE lat BETWEEN -2 AND 2 AND lon BETWEEN -4 AND 4",
		    2, "projection", PROJECTION_SHA256, "independent_formula_and_materialized_coordinate_window");
		Require(coordinates.rows.size() == 25 && coordinates.selection_mode == "restricted",
		        "coordinate-only scenario returned an unexpected position set");
		VerifyCoordinatesAgainstFull(full, coordinates);
		RequireAllValueCountersZero(options, coordinates);

		const auto count = RunScenario(
		    options, "count", "SELECT count(*) FROM " + source +
	                            " WHERE lat BETWEEN -2 AND 2 AND lon BETWEEN -4 AND 4",
		    1, "projection", PROJECTION_SHA256, "selected_cardinality_without_value_dependencies");
		Require(count.rows.size() == 1 && count.rows[0][0] == "25" && count.selection_mode == "restricted",
		        "spatial count scenario returned an incorrect cardinality");
		RequireAllValueCountersZero(options, count);

		const auto mixed = RunScenario(
		    options, "mixed",
		    "SELECT temperature, lat, lon FROM " + source +
		        " WHERE humidity = 96 AND lat BETWEEN -41 AND -37 AND lon BETWEEN 50 AND 70",
		    3, "projection", PROJECTION_SHA256, "exact_multiset_difference_from_materialized_mixed_baseline");
		Require(mixed.selection_mode == "restricted" && mixed.rows.size() == 1,
		        "mixed spatial/value scenario must narrow the scan and return its pinned row");
		VerifyMixedAgainstMaterialized(mixed_full, mixed);
		for (const auto *path : {"/temperature", "/humidity"}) {
			const auto prefix = std::string(".variables[") + JsonEscape(path) + "].";
			Require(Metric(mixed.metrics_file, prefix + "data_bytes", options.root) > 0 &&
				        Metric(mixed.metrics_file, prefix + "decoded_chunks", options.root) > 0,
			        std::string("mixed query must read its ") + path + " dependency");
		}
		const auto pressure_prefix = std::string(".variables[\"/pressure\"].");
		for (const auto *field : {"index_bytes", "index_requests", "data_bytes", "data_requests", "decoded_chunks"}) {
			Require(Metric(mixed.metrics_file, pressure_prefix + field, options.root) == 0,
			        std::string("mixed query must not touch unrelated pressure ") + field);
		}

		const auto fallback = RunScenario(
		    options, "fallback",
		    "SELECT temperature, lat, lon FROM " + source +
		        " WHERE lon >= 124 OR lon <= -124",
		    3, "projection", PROJECTION_SHA256, "seam_or_exact_multiset_difference_from_materialized_full_output");
		Require(fallback.selection_mode == "fallback" && fallback.candidate_rows == ROWS,
		        "seam OR must retain all full-scan candidates and report fallback mode");
		VerifyFallbackAgainstMaterialized(full, fallback);

		const auto optimizer_sql = "SELECT count(*) FROM " + source + " WHERE FALSE";
		const auto optimizer_plan = QueryPlan(options, optimizer_sql);
		auto optimizer_empty = RunScenario(options, "optimizer_empty", optimizer_sql, 1, "projection", PROJECTION_SHA256,
		                                   "optimizer_constant_false_with_bind_metadata");
		AnnotateOptimizerEmpty(options, optimizer_empty, optimizer_plan);
		const auto bind_metadata = Metric(optimizer_empty.metrics_file, ".bind_metadata_bytes", options.root);

		const auto domain_source = SpatialSource(options.domain_file, true);
		const auto domain_count = RunScenario(options, "domain_count",
		                                      "SELECT count(*) FROM " + domain_source, 1, "ncep_gfswave025", DOMAIN_SHA256,
		                                      "pinned_real_domain_binding_and_full_logical_cardinality");
		Require(domain_count.rows.size() == 1 && domain_count.rows[0][0] == "1038240",
		        "real domain sample does not produce its pinned 1,038,240 row count");
		RunDomainOracle(options, domain_sha);

		const ScenarioResult *completed_scenarios[] = {&full, &mixed_full, &restricted, &empty, &coordinates,
	                                              &count, &mixed, &fallback, &optimizer_empty, &domain_count};
		for (const auto *scenario : completed_scenarios) {
			CompleteScenarioMetrics(options, *scenario);
		}
		for (const auto *scenario : completed_scenarios) {
			WriteScenarioEvidence(options, *scenario);
		}
		for (const auto *scenario : completed_scenarios) {
			SpatialEvidenceExpectation expectation;
			expectation.fixture_id = scenario->fixture_id;
			expectation.fixture_sha256 = scenario->fixture_sha;
			expectation.selection_mode = scenario->name == "optimizer_empty" ? "optimizer_empty" : scenario->selection_mode;
			expectation.require_zero_value_reads = scenario->name == "empty" || scenario->name == "coordinates" ||
			                                      scenario->name == "count" || scenario->name == "optimizer_empty" ||
			                                      scenario->name == "domain_count";
			expectation.optimizer_plan_is_empty_result = scenario->name == "optimizer_empty" &&
			                                             optimizer_plan.find("EMPTY_RESULT") != std::string::npos;
			expectation.query_result_succeeded = scenario->name == "optimizer_empty" &&
			                                    optimizer_empty.rows.size() == 1 && optimizer_empty.rows[0][0] == "0";
			Require(ValidateSpatialMetricsEvidence(scenario->metrics_file, expectation, options.root),
			        "evidence integrity validation rejected scenario " + scenario->name);
		}
		WriteText(options.output / "optimizer_empty.plan.txt", optimizer_plan);
		struct utsname host {};
		std::string machine = "unknown";
		if (uname(&host) == 0) machine = host.machine;
		std::ostringstream summary;
		summary << "{\"schema_version\":1,\"status\":\"success\",\"fixture_id\":\"projection\",\"fixture_sha256\":"
		        << JsonEscape(PROJECTION_SHA256) << ",\"domain_file\":" << JsonEscape(options.domain_file.string())
		        << ",\"domain_file_sha256\":" << JsonEscape(domain_sha)
		        << ",\"domain_rows\":1038240,\"required_scenarios\":[\"full\",\"mixed_baseline\",\"restricted\",\"empty\",\"coordinates\",\"count\",\"mixed\",\"fallback\",\"optimizer_empty\",\"domain_count\"]"
		        << ",\"full_temperature_data_bytes\":" << full_data_bytes
		        << ",\"restricted_temperature_data_bytes\":" << restricted_data_bytes
		        << ",\"full_temperature_decoded_chunks\":" << full_chunks
		        << ",\"restricted_temperature_decoded_chunks\":" << restricted_chunks
		        << ",\"optimizer_empty_bind_metadata_bytes\":" << bind_metadata
		        << ",\"mixed_rows\":" << mixed.rows.size()
		        << ",\"mixed_temperature_data_bytes\":" << Metric(mixed.metrics_file, ".variables[\"/temperature\"].data_bytes", options.root)
		        << ",\"mixed_humidity_data_bytes\":" << Metric(mixed.metrics_file, ".variables[\"/humidity\"].data_bytes", options.root)
		        << ",\"mixed_pressure_data_bytes\":" << Metric(mixed.metrics_file, ".variables[\"/pressure\"].data_bytes", options.root)
		        << ",\"fallback_rows\":" << fallback.rows.size()
		        << ",\"fallback_temperature_data_bytes\":" << Metric(fallback.metrics_file, ".variables[\"/temperature\"].data_bytes", options.root)
		        << ",\"strict_data_bytes_reduction\":true,\"strict_decoded_chunks_reduction\":true"
		        << ",\"optimizer_empty_plan_contains_empty_result\":"
	        << (optimizer_plan.find("EMPTY_RESULT") != std::string::npos ? "true" : "false")
		        << ",\"machine\":" << JsonEscape(machine) << ",\"build\":\"release\",\"threads\":1"
		        << ",\"application_cache\":\"fresh DuckDB process per scenario\",\"os_page_cache\":\"not cleared\"}\n";
		WriteText(options.output / "summary.json", summary.str());
		std::cout << "duckomo_spatial_validation: full/restricted/empty/coordinates/count/mixed/fallback/domain scenarios passed; "
		          << "temperature data bytes " << full_data_bytes << " -> " << restricted_data_bytes << ", decoded chunks "
		          << full_chunks << " -> " << restricted_chunks << "; evidence=" << (options.output / "summary.json") << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "duckomo_spatial_validation: " << error.what() << '\n';
		return 1;
	}
}
