#include "validation_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using duckomo_validation_support::CommandResult;
using duckomo_validation_support::JsonString;
using duckomo_validation_support::RunProcess;

namespace {

struct Options final {
	fs::path root = fs::current_path();
	fs::path manifest;
	fs::path duckdb;
	fs::path extension;
	fs::path spatial;
	fs::path runtime_matrix;
	fs::path evidence_root;
	fs::path httpfs;
	fs::path remote_env;
	fs::path server_log;
	fs::path output;
	std::vector<std::string> gates;
	bool self_check = false;
};

const std::vector<std::string> GATE_IDS{"P0", "P1", "P2", "P3", "P4", "P5", "P6"};

std::vector<std::string> ParseGates(const std::string &value) {
	std::vector<std::string> result;
	std::set<std::string> seen;
	std::istringstream input(value);
	std::string item;
	while (std::getline(input, item, ',')) {
		const auto first = item.find_first_not_of(" \t\r\n");
		const auto last = item.find_last_not_of(" \t\r\n");
		if (first == std::string::npos) throw std::invalid_argument("--gates contains an empty gate id");
		item = item.substr(first, last - first + 1);
		if (std::find(GATE_IDS.begin(), GATE_IDS.end(), item) == GATE_IDS.end())
			throw std::invalid_argument("unknown gate " + item + "; expected P0 through P6");
		if (!seen.insert(item).second) throw std::invalid_argument("duplicate gate " + item);
		result.push_back(item);
	}
	if (result.empty()) throw std::invalid_argument("--gates must name at least one gate");
	return result;
}

void Usage(std::ostream &out) {
	out << "Usage: duckomo_region_validation --manifest PATH --duckdb PATH --extension PATH "
	       "--spatial PATH --output EMPTY_DIR --gates P0,P1,...,P5 [--runtime-matrix PATH] [--root PATH]\n"
	       "P5 also accepts --httpfs PATH --remote-env PATH --server-log DIR for controlled HTTPFS fixtures.\n"
	       "P6: duckomo_region_validation --runtime-matrix PATH --evidence-root DIR --output EMPTY_DIR --gates P6 [--manifest PATH] [--root PATH]\n";
}

Options ParseOptions(int argc, char **argv) {
	Options options;
	std::set<std::string> seen;
	for (int i = 1; i < argc; ++i) {
		const std::string name = argv[i];
		if (name == "--help" || name == "-h") {
			Usage(std::cout);
			std::exit(0);
		}
		if (name == "--self-check") {
			if (argc != 2) throw std::invalid_argument("--self-check cannot be combined with other options");
			options.self_check = true;
			return options;
		}
		if (!seen.insert(name).second) throw std::invalid_argument("duplicate option " + name);
		if (i + 1 >= argc) throw std::invalid_argument("missing value after " + name);
		const fs::path value = argv[++i];
		if (name == "--root") options.root = value;
		else if (name == "--manifest") options.manifest = value;
		else if (name == "--duckdb") options.duckdb = value;
		else if (name == "--extension") options.extension = value;
		else if (name == "--spatial") options.spatial = value;
		else if (name == "--runtime-matrix") options.runtime_matrix = value;
		else if (name == "--evidence-root") options.evidence_root = value;
		else if (name == "--httpfs") options.httpfs = value;
		else if (name == "--remote-env") options.remote_env = value;
		else if (name == "--server-log") options.server_log = value;
		else if (name == "--output") options.output = value;
		else if (name == "--gates") options.gates = ParseGates(value.string());
		else throw std::invalid_argument("unknown option " + name);
	}
	if (options.root.is_relative()) options.root = fs::absolute(options.root);
	options.root = options.root.lexically_normal();
	auto resolve = [&](fs::path &path) {
		if (path.empty()) return;
		if (path.is_relative()) path = options.root / path;
		path = fs::absolute(path).lexically_normal();
	};
	resolve(options.manifest);
	resolve(options.duckdb);
	resolve(options.extension);
	resolve(options.spatial);
	resolve(options.runtime_matrix);
	resolve(options.evidence_root);
	resolve(options.httpfs);
	resolve(options.remote_env);
	resolve(options.server_log);
	resolve(options.output);
	const bool p6_only = options.gates.size() == 1 && options.gates.front() == "P6";
	if (std::find(options.gates.begin(), options.gates.end(), "P6") != options.gates.end() && !p6_only) {
		throw std::invalid_argument("P6 must be requested alone with --runtime-matrix and --evidence-root");
	}
	if (p6_only) {
		if (options.output.empty() || options.runtime_matrix.empty() || options.evidence_root.empty()) {
			throw std::invalid_argument("P6 requires --runtime-matrix, --evidence-root, and --output");
		}
		if (options.manifest.empty()) options.manifest = options.root / "test/data/regions/manifest.json";
		resolve(options.manifest);
		return options;
	}
	if (options.manifest.empty() || options.duckdb.empty() || options.extension.empty() ||
	    options.spatial.empty() || options.output.empty() || options.gates.empty()) {
		throw std::invalid_argument("--manifest, --duckdb, --extension, --spatial, --output, and --gates are required");
	}
	if (options.runtime_matrix.empty()) {
		const auto *override_path = std::getenv("DUCKOMO_REGION_RUNTIME_MATRIX");
		options.runtime_matrix = override_path && override_path[0] != '\0' ? fs::path(override_path)
		                                                                  : options.root / "test/data/regions/runtime-matrix.json";
	}
	resolve(options.runtime_matrix);
	return options;
}

bool IsEmptyDirectory(const fs::path &path) {
	return fs::is_directory(path) && fs::directory_iterator(path) == fs::directory_iterator();
}

std::string HashFile(const fs::path &path, const fs::path &root) {
	const auto result = RunProcess({"sha256sum", path.string()}, root);
	if (result.exit_code != 0) throw std::runtime_error("sha256sum failed for " + path.string());
	const auto end = result.output.find_first_of(" \t\r\n");
	if (end == std::string::npos) throw std::runtime_error("sha256sum returned malformed output");
	return result.output.substr(0, end);
}

std::string HashFileOrNull(const fs::path &path, const fs::path &root) {
	return fs::is_regular_file(path) ? JsonString(HashFile(path, root)) : "null";
}

std::string JsonArray(const std::vector<std::string> &values) {
	std::ostringstream out;
	out << '[';
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i) out << ',';
		out << JsonString(values[i]);
	}
	out << ']';
	return out.str();
}

struct CommandRecord final {
	std::vector<std::string> argv;
	CommandResult result;
	std::string output_file;
	std::string output_sha256;
};

struct GateRecord final {
	std::string id;
	std::string status = "not-run";
	std::string reason;
	std::vector<CommandRecord> commands;
	std::string oracle_sha256;
	std::string evidence_report;
	std::string evidence_report_sha256;
	std::size_t statistics_groups = 0;
	std::size_t statistics_mismatches = 0;
};

CommandRecord Run(const std::string &name, std::vector<std::string> argv, const fs::path &root,
	              const fs::path &output, std::size_t ordinal,
	              const std::map<std::string, std::string> &environment = {}) {
	CommandRecord record;
	record.argv = std::move(argv);
	record.result = RunProcess(record.argv, root, environment);
	record.output_file = name + "-" + std::to_string(ordinal) + ".stdout.txt";
	const auto output_path = output / record.output_file;
	{
		std::ofstream stream(output_path, std::ios::binary | std::ios::trunc);
		if (!stream.good()) throw std::runtime_error("cannot write validator command output");
		stream << record.result.output;
	}
	record.output_sha256 = HashFile(output_path, root);
	return record;
}

bool Passed(const std::vector<CommandRecord> &commands) {
	return std::all_of(commands.begin(), commands.end(), [](const auto &command) {
		return command.result.exit_code == 0;
	});
}

int WriteP6Summary(const Options &options) {
	if (!IsEmptyDirectory(options.output))
		throw std::invalid_argument("--output must name an existing empty directory");
	const auto report = options.output / "P6-summary.json";
	const auto script = options.root / "test/tools/region_evidence_test.py";
	const std::vector<std::string> argv{
	    "python3", script.string(), "--audit-p6", "--root", options.root.string(),
	    "--runtime-matrix", options.runtime_matrix.string(), "--evidence-root", options.evidence_root.string(),
	    "--manifest", options.manifest.string(), "--output", report.string()};
	const auto result = RunProcess(argv, options.root);
	const auto output_path = options.output / "P6-1.stdout.txt";
	{
		std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
		if (!output.good()) throw std::runtime_error("cannot write P6 audit command output");
		output << result.output;
	}
	const auto status = result.exit_code == 0 ? "pass" : result.exit_code == 2 ? "not-run" : "fail";
	const auto reason = result.exit_code == 0 ? std::string() : result.output.empty()
	                    ? "P6 evidence audit did not complete" : result.output;
	const auto command_output_sha = HashFile(output_path, options.root);
	const auto report_sha = fs::is_regular_file(report) ? HashFile(report, options.root) : std::string();
	{
		std::ofstream manifest(options.output / "manifest.json", std::ios::binary | std::ios::trunc);
		if (!manifest.good()) throw std::runtime_error("cannot write P6 run manifest");
		manifest << "{\n  \"schema_version\":1,\n  \"status\":" << JsonString(status)
		         << ",\n  \"requested_gates\":[\"P6\"],\n  \"runtime_matrix_path\":" << JsonString(options.runtime_matrix.string())
		         << ",\n  \"runtime_matrix_sha256\":" << HashFileOrNull(options.runtime_matrix, options.root)
		         << ",\n  \"evidence_root\":" << JsonString(options.evidence_root.string())
		         << ",\n  \"manifest_path\":" << JsonString(options.manifest.string())
		         << ",\n  \"manifest_sha256\":" << HashFileOrNull(options.manifest, options.root)
		         << ",\n  \"gates\":[{\"id\":\"P6\",\"status\":" << JsonString(status)
		         << ",\"reason\":" << JsonString(reason) << ",\"commands\":[{\"argv\":"
		         << JsonArray(argv) << ",\"exit_code\":" << result.exit_code
		         << ",\"output_path\":\"P6-1.stdout.txt\",\"output_sha256\":"
		         << JsonString(command_output_sha) << "}]";
		if (!report_sha.empty()) {
			manifest << ",\"evidence_report\":\"P6-summary.json\",\"evidence_report_sha256\":"
			         << JsonString(report_sha);
		}
		manifest << "}]}\n";
	}
	{
		std::ofstream final(options.output / "final.md", std::ios::binary | std::ios::trunc);
		if (!final.good()) throw std::runtime_error("cannot write P6 final report");
		final << "# Region P6 audit run\n\nStatus: **" << status << "**\n\n";
		if (!reason.empty()) final << reason << '\n';
	}
	std::cout << "Recorded P6 version-matrix audit, status=" << status << ", output=" << options.output << '\n';
	return result.exit_code == 0 ? 0 : result.exit_code == 2 ? 2 : 1;
}

void AddSqlCommand(GateRecord &gate, const Options &options, const fs::path &runner,
	               const std::string &test, std::size_t ordinal) {
	gate.commands.push_back(Run(gate.id, {runner.string(), "--test-dir", options.root.string(), test},
	                            options.root, options.output, ordinal,
	                            {{"DUCKOMO_TEST_EXTENSION", options.extension.string()},
	                             {"DUCKOMO_CORE_FUNCTIONS_EXTENSION",
	                              (options.extension.parent_path().parent_path().parent_path() /
	                               "extension/core_functions/core_functions.duckdb_extension").string()},
	                             {"DUCKOMO_SPATIAL_EXTENSION", options.spatial.string()}}));
}

std::vector<std::string> MissingInputs(const Options &options) {
	std::vector<std::string> missing;
	if (!fs::is_directory(options.root)) missing.emplace_back("repository root is missing");
	if (!fs::is_regular_file(options.manifest)) missing.emplace_back("region manifest is missing");
	if (!fs::is_regular_file(options.duckdb) || access(options.duckdb.c_str(), X_OK) != 0)
		missing.emplace_back("matching DuckDB executable is missing");
	if (!fs::is_regular_file(options.extension)) missing.emplace_back("DuckOMO extension is missing");
	if (!fs::is_regular_file(options.spatial)) missing.emplace_back("matching spatial extension is missing");
	return missing;
}

std::string ExpectedHash(const fs::path &manifest_path, const std::string &field) {
	std::ifstream input(manifest_path, std::ios::binary);
	if (!input.good()) return {};
	const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	const auto key = JsonString(field) + ":";
	auto position = contents.find(key);
	if (position == std::string::npos) return {};
	position = contents.find('"', position + key.size());
	if (position == std::string::npos) return {};
	const auto end = contents.find('"', position + 1);
	if (end == std::string::npos) return {};
	return contents.substr(position + 1, end - position - 1);
}

std::size_t CsvRows(const fs::path &path) {
	std::ifstream input(path);
	std::string line;
	std::size_t rows = 0;
	if (!std::getline(input, line)) return 0;
	while (std::getline(input, line)) ++rows;
	return rows;
}

std::string JsonObjectContents(const std::string &json, const std::string &name) {
	const auto marker = JsonString(name) + ":{";
	const auto start_marker = json.find(marker);
	if (start_marker == std::string::npos) throw std::runtime_error("resource probe JSON omits object " + name);
	const auto start = start_marker + marker.size();
	const auto end = json.find('}', start);
	if (end == std::string::npos) throw std::runtime_error("resource probe JSON has incomplete object " + name);
	return json.substr(start, end - start);
}

std::uint64_t JsonUnsigned(const std::string &object, const std::string &name) {
	const auto marker = JsonString(name) + ":";
	const auto start_marker = object.find(marker);
	if (start_marker == std::string::npos) throw std::runtime_error("resource probe JSON omits field " + name);
	const auto start = start_marker + marker.size();
	std::size_t end = start;
	while (end < object.size() && object[end] >= '0' && object[end] <= '9') ++end;
	if (end == start) throw std::runtime_error("resource probe JSON field is not an unsigned integer: " + name);
	return std::stoull(object.substr(start, end - start));
}

double JsonNumber(const std::string &object, const std::string &name) {
	const auto marker = JsonString(name) + ":";
	const auto start_marker = object.find(marker);
	if (start_marker == std::string::npos) throw std::runtime_error("resource probe JSON omits field " + name);
	return std::stod(object.substr(start_marker + marker.size()));
}

bool JsonBoolean(const std::string &object, const std::string &name) {
	const auto marker = JsonString(name) + ":";
	const auto start_marker = object.find(marker);
	if (start_marker == std::string::npos) throw std::runtime_error("resource probe JSON omits field " + name);
	const auto value = object.substr(start_marker + marker.size());
	if (value.rfind("true", 0) == 0) return true;
	if (value.rfind("false", 0) == 0) return false;
	throw std::runtime_error("resource probe JSON field is not boolean: " + name);
}

struct RegionResourceSample final {
	std::uint64_t point_count = 0;
	std::uint64_t input_wkb_bytes = 0;
	std::uint64_t input_vertex_count = 0;
	std::uint64_t owned_work_peak_bytes = 0;
	std::uint64_t owned_work_bound_bytes = 0;
	std::uint64_t matched_pairs = 0;
	std::uint64_t geometry_tested_pairs = 0;
	std::uint64_t value_data_bytes = 0;
	std::uint64_t value_decoded_chunks = 0;
	std::uint64_t startup_peak_rss_bytes = 0;
	std::uint64_t process_peak_rss_bytes = 0;
	std::uint64_t result_rows = 0;
	double result_sum = 0;
	double result_min = 0;
	double result_max = 0;
	bool complete = false;
	bool owned_complete = false;
	bool match_count_complete = false;
	std::string output_sha256;
};

RegionResourceSample ReadResourceSample(const CommandRecord &command, std::uint64_t point_count,
	                                    const fs::path &output_path, const fs::path &root) {
	if (command.result.exit_code != 0) throw std::runtime_error("isolated resource probe failed");
	const auto outcome = JsonObjectContents(command.result.output, "outcome");
	const auto regions = JsonObjectContents(command.result.output, "regions");
	const auto values = JsonObjectContents(command.result.output, "value_totals");
	const auto tasks = JsonObjectContents(command.result.output, "tasks");
	RegionResourceSample sample;
	sample.point_count = point_count;
	sample.input_wkb_bytes = JsonUnsigned(regions, "input_wkb_bytes");
	sample.input_vertex_count = JsonUnsigned(regions, "input_vertex_count");
	sample.owned_work_peak_bytes = JsonUnsigned(regions, "owned_work_peak_bytes");
	sample.owned_work_bound_bytes = JsonUnsigned(regions, "owned_work_bound_bytes");
	sample.matched_pairs = JsonUnsigned(regions, "matched_pairs_exact");
	sample.geometry_tested_pairs = JsonUnsigned(regions, "geometry_tested_pairs");
	sample.value_data_bytes = JsonUnsigned(values, "data_bytes");
	sample.value_decoded_chunks = JsonUnsigned(values, "decoded_chunks");
	sample.startup_peak_rss_bytes = JsonUnsigned(command.result.output, "startup_peak_rss_bytes");
	sample.process_peak_rss_bytes = command.result.peak_rss_bytes;
	sample.result_rows = JsonUnsigned(command.result.output, "result_rows");
	sample.result_sum = JsonNumber(command.result.output, "result_sum");
	sample.result_min = JsonNumber(command.result.output, "result_min");
	sample.result_max = JsonNumber(command.result.output, "result_max");
	sample.complete = JsonBoolean(outcome, "scan_complete");
	sample.owned_complete = JsonBoolean(JsonObjectContents(command.result.output, "memory"), "query_owned_complete");
	sample.match_count_complete = JsonBoolean(regions, "match_count_complete");
	if (JsonUnsigned(tasks, "max_active_workers") != 1) {
		throw std::runtime_error("isolated resource probe did not stay within one active worker");
	}
	if (sample.owned_work_peak_bytes > sample.owned_work_bound_bytes ||
	    !fs::is_regular_file(output_path)) {
		throw std::runtime_error("resource probe has incomplete owned-work evidence or missing output");
	}
	sample.output_sha256 = HashFile(output_path, root);
	return sample;
}

double CancellationResponseMilliseconds(const std::string &output) {
	const std::string marker = "cancellation response after first output was ";
	const auto start = output.find(marker);
	if (start == std::string::npos) throw std::runtime_error("region lifecycle output omits cancellation response time");
	const auto value_start = start + marker.size();
	const auto end = output.find(" ms", value_start);
	if (end == std::string::npos) throw std::runtime_error("region lifecycle cancellation response is malformed");
	const auto milliseconds = std::stod(output.substr(value_start, end - value_start));
	if (!(milliseconds >= 0)) throw std::runtime_error("region lifecycle cancellation response is not finite");
	return milliseconds;
}

void WriteP5NotRun(const Options &options, GateRecord &gate, const std::string &reason) {
	static const std::vector<std::string> targets{"ncep_gfswave025", "gem_rdps_10km", "gem_regional",
	                                               "aladin_central_europe_2km", "ecmwf_ifs"};
	const auto report = options.output / "P5-remote.json";
	std::ofstream stream(report, std::ios::binary | std::ios::trunc);
	if (!stream.good()) throw std::runtime_error("cannot write P5 not-run evidence report");
	stream << "{\n  \"schema_version\":1,\n  \"gate\":\"P5\",\n  \"status\":\"not-run\",\n  \"reason\":"
	       << JsonString(reason) << ",\n  \"sources\":[\"local\",\"http\",\"https\",\"s3\"],\n  \"targets\":[";
	for (std::size_t i = 0; i < targets.size(); ++i) {
		if (i) stream << ',';
		stream << "{\"grid_id\":" << JsonString(targets[i]) << ",\"status\":\"not-run\","
		       << "\"source_status\":{\"local\":\"not-run\",\"http\":\"not-run\","
		          "\"https\":\"not-run\",\"s3\":\"not-run\"}}";
	}
	stream << "],\n  \"service_logs\":null,\n  \"credentials_recorded\":false\n}\n";
	stream.close();
	if (!stream) throw std::runtime_error("failed to flush P5 not-run evidence report");
	gate.status = "not-run";
	gate.reason = reason;
	gate.evidence_report = fs::relative(report, options.output).string();
	gate.evidence_report_sha256 = HashFile(report, options.root);
}

int WriteRun(const Options &options) {
	if (!IsEmptyDirectory(options.output))
		throw std::invalid_argument("--output must name an existing empty directory");
	std::vector<GateRecord> records;
	for (const auto &id : options.gates) records.push_back({id});
	const auto missing = MissingInputs(options);
	const auto build = options.extension.parent_path().parent_path().parent_path();
	const auto runner = build / "test/unittest";
	const auto native = build / "test/native";
	std::vector<std::string> validation_inputs = missing;
	const auto statistics_oracle = options.root / "test/data/regions/regions-statistics.reference.csv";
	if (std::any_of(options.gates.begin(), options.gates.end(), [](const auto &gate) {
		    return gate == "P0" || gate == "P1" || gate == "P2" || gate == "P3" || gate == "P4" || gate == "P5";
	    })) {
		const auto &runtime_matrix = options.runtime_matrix;
		if (!fs::is_regular_file(runtime_matrix)) {
			validation_inputs.emplace_back("verified spatial runtime matrix is missing");
		} else if (fs::is_regular_file(options.duckdb) && fs::is_regular_file(options.spatial)) {
			const auto version_result = RunProcess({options.duckdb.string(), "--version"}, options.root);
			std::istringstream version_stream(version_result.output);
			std::string version;
			version_stream >> version;
			const auto expected_result = RunProcess(
			    {"python3", "scripts/region_runtime.py", runtime_matrix.string(), version}, options.root);
			const auto expected = expected_result.exit_code == 0 ? expected_result.output.substr(
			    0, expected_result.output.find_first_of("\r\n")) : std::string();
			if (version_result.exit_code != 0 || expected.empty()) {
				validation_inputs.emplace_back("matching spatial runtime identity is not verified");
			} else if (HashFile(options.spatial, options.root) != expected) {
				validation_inputs.emplace_back("spatial extension hash does not match the frozen runtime matrix");
			}
		}
	}
	const bool requests_p1 = std::find(options.gates.begin(), options.gates.end(), "P1") != options.gates.end();
	if (requests_p1) {
		if (!fs::is_regular_file(options.root / "test/data/regions/real-targets.json"))
			validation_inputs.emplace_back("frozen five-target region manifest is missing");
		if (!fs::is_regular_file(options.root / "test/data/regions/regions-membership.reference.csv"))
			validation_inputs.emplace_back("independent synthetic membership oracle is missing");
	}
	if (std::find(options.gates.begin(), options.gates.end(), "P2") != options.gates.end()) {
		if (!fs::is_regular_file(statistics_oracle)) validation_inputs.emplace_back("60-group statistics oracle is missing");
		if (!fs::is_regular_file(options.root / "test/data/regions/regions.om"))
			validation_inputs.emplace_back("synthetic region fixture is missing");
	}
	if (validation_inputs.empty()) {
		for (auto &gate : records) {
			if (gate.id == "P0") {
				const auto geometry = native / "region_geometry_test";
				if (!fs::is_regular_file(runner) || !fs::is_regular_file(geometry)) {
					gate.reason = "P0 requires the built SQL runner and region_geometry_test";
					continue;
				}
				gate.commands.push_back(Run(gate.id,
				    {geometry.string(), options.spatial.string(), HashFile(options.spatial, options.root)},
				    options.root, options.output, gate.commands.size() + 1));
				AddSqlCommand(gate, options, runner, "test/sql/read_om.test", gate.commands.size() + 1);
				AddSqlCommand(gate, options, runner, "test/sql/regions_spatial_dependency.test", gate.commands.size() + 1);
				AddSqlCommand(gate, options, runner, "test/sql/regions.test", gate.commands.size() + 1);
				gate.status = Passed(gate.commands) ? "pass" : "fail";
				if (gate.status == "fail") gate.reason = "one or more P0 commands failed";
			} else if (gate.id == "P1") {
				const auto input = native / "region_input_test";
				const auto scan = native / "region_scan_test";
				if (!fs::is_regular_file(runner) || !fs::is_regular_file(input) || !fs::is_regular_file(scan)) {
					gate.reason = "P1 requires the built SQL runner and region native tests";
					continue;
				}
				gate.commands.push_back(Run(gate.id, {input.string()}, options.root, options.output, 1));
				gate.commands.push_back(Run(gate.id, {scan.string(), options.spatial.string()},
				                            options.root, options.output, 2,
				                            {{"DUCKOMO_CORE_FUNCTIONS_EXTENSION",
				                             (build / "extension/core_functions/core_functions.duckdb_extension").string()}}));
				AddSqlCommand(gate, options, runner, "test/sql/regions.test", gate.commands.size() + 1);
				const auto real_reference_tool = options.root / "test/tools/region_real_reference.py";
				gate.commands.push_back(Run(gate.id, {"python3", real_reference_tool.string(), "--self-check"},
				                            options.root, options.output, gate.commands.size() + 1));
				if (!Passed(gate.commands)) {
					gate.status = "fail";
					gate.reason = "one or more synthetic P1 commands failed";
				} else {
					const auto real_output = options.output / "P1-real";
					gate.commands.push_back(Run(
					    gate.id,
					    {"python3", real_reference_tool.string(),
					     "--root", options.root.string(), "--manifest", options.manifest.string(),
					     "--duckdb", options.duckdb.string(), "--extension", options.extension.string(),
					     "--spatial", options.spatial.string(), "--output", real_output.string()},
					    options.root, options.output, gate.commands.size() + 1));
					const auto report = real_output / "summary.json";
					if (fs::is_regular_file(report)) {
						gate.evidence_report = fs::relative(report, options.output).string();
						gate.evidence_report_sha256 = HashFile(report, options.root);
					}
					const auto exit_code = gate.commands.back().result.exit_code;
					if (exit_code == 0) {
						gate.status = "pass";
						gate.reason.clear();
					} else if (exit_code == 2) {
						gate.status = "not-run";
						gate.reason = "a frozen real-target input is unavailable";
					} else {
						gate.status = "fail";
						gate.reason = "a real-target coordinate, value, metrics, or hash comparison failed";
					}
				}
			} else if (gate.id == "P2") {
				if (!fs::is_regular_file(runner)) {
					gate.reason = "P2 requires the built SQL runner";
					continue;
				}
				AddSqlCommand(gate, options, runner, "test/sql/regions.test", 1);
				gate.statistics_groups = CsvRows(statistics_oracle);
				gate.oracle_sha256 = HashFile(statistics_oracle, options.root);
				const auto expected_hash = ExpectedHash(options.manifest, "expected_group_statistics_sha256");
				if (!Passed(gate.commands)) {
					gate.status = "fail";
					gate.reason = "region SQL statistics test failed";
				} else if (gate.statistics_groups != 60 || expected_hash.empty() ||
				           expected_hash != gate.oracle_sha256) {
					gate.status = "fail";
					gate.reason = "the 60-group statistics oracle is missing, has the wrong size, or fails its manifest hash";
				} else {
					gate.status = "pass";
					gate.statistics_mismatches = 0;
				}
			} else if (gate.id == "P3") {
				const auto scan = native / "region_scan_test";
				const auto grid_selection = native / "grid_selection_test";
				const auto spatial_selection = native / "spatial_selection_test";
				const auto metrics_v3 = native / "scan_metrics_v3_test";
				const auto metrics_v4 = native / "scan_metrics_v4_test";
				const auto sample_manifest = options.root / "test/data/grids/sample-manifest.json";
				const auto resource_manifest = options.root / "test/data/regions/resource-memory-center.json";
				const auto small_fixture = options.root / "test/data/grids/memory-small.om";
				const auto large_fixture = options.root / "test/data/grids/memory-large.om";
				const auto region_wkb = options.root / "test/data/regions/resource-memory-center.wkb";
				std::string unavailable;
				for (const auto &path : {scan, grid_selection, spatial_selection, metrics_v3, metrics_v4,
				                          sample_manifest, resource_manifest, small_fixture, large_fixture, region_wkb}) {
					if (!fs::is_regular_file(path)) {
						unavailable = "P3 requires " + path.string();
						break;
					}
				}
				RegionResourceSample small;
				RegionResourceSample large;
				bool samples_parsed = false;
				bool sample_contract = false;
				std::string probe_error;
				if (!unavailable.empty()) {
					gate.reason = unavailable;
				} else {
					const std::string fixture_audit =
					    "import hashlib,json,pathlib,sys; m=json.load(open(sys.argv[1])); fs={x['id']:x for x in m['fixtures']}; "
					    "assert fs['memory_small']['variables'][0]['shape']==[256,256]; "
					    "assert fs['memory_large']['variables'][0]['shape']==[1024,1024]; "
					    "assert fs['memory_small']['variables'][0]['chunk_shape']==[32,32]==fs['memory_large']['variables'][0]['chunk_shape']; "
					    "assert 1024*1024 >= 10*256*256; "
					    "[(lambda p,r: (None if hashlib.sha256(p.read_bytes()).hexdigest()==r['sha256'] and p.stat().st_size==r['size_bytes'] else (_ for _ in ()).throw(AssertionError(str(p)))))(pathlib.Path(sys.argv[2]).parent/fs[i]['path'],fs[i]) for i in ('memory_small','memory_large')]; "
					    "w=json.load(open(sys.argv[3])); b=pathlib.Path(sys.argv[4]).read_bytes(); "
					    "assert len(b)==w['size_bytes'] and hashlib.sha256(b).hexdigest()==w['sha256'] and w['vertices']==5 and w['expected_region_record_pairs']==1; "
					    "print('P3 frozen fixture dimensions, chunks, WKB hash, and point-count ratio passed')";
					gate.commands.push_back(Run(
					    gate.id,
					    {"python3", "-c", fixture_audit, sample_manifest.string(), small_fixture.string(),
					     resource_manifest.string(), region_wkb.string()},
					    options.root, options.output, gate.commands.size() + 1));
					gate.commands.push_back(Run(gate.id, {scan.string(), options.spatial.string(), ""}, options.root,
					                            options.output, gate.commands.size() + 1));
					gate.commands.push_back(Run(gate.id, {grid_selection.string()}, options.root, options.output,
					                            gate.commands.size() + 1));
					gate.commands.push_back(Run(gate.id, {spatial_selection.string()}, options.root, options.output,
					                            gate.commands.size() + 1));
					gate.commands.push_back(Run(gate.id, {metrics_v3.string()}, options.root, options.output,
					                            gate.commands.size() + 1));
					gate.commands.push_back(Run(gate.id, {metrics_v4.string()}, options.root, options.output,
					                            gate.commands.size() + 1));
					AddSqlCommand(gate, options, runner, "test/sql/regions.test", gate.commands.size() + 1);
					const auto small_probe = gate.commands.size();
					gate.commands.push_back(Run(gate.id,
					    {scan.string(), options.spatial.string(), "", "--resource-probe", "small"},
					    options.root, options.output, gate.commands.size() + 1));
					const auto large_probe = gate.commands.size();
					gate.commands.push_back(Run(gate.id,
					    {scan.string(), options.spatial.string(), "", "--resource-probe", "large"},
					    options.root, options.output, gate.commands.size() + 1));
					try {
						small = ReadResourceSample(gate.commands[small_probe], 256ULL * 256ULL,
						                          options.output / gate.commands[small_probe].output_file, options.root);
						large = ReadResourceSample(gate.commands[large_probe], 1024ULL * 1024ULL,
						                          options.output / gate.commands[large_probe].output_file, options.root);
						samples_parsed = true;
					} catch (const std::exception &error) {
						probe_error = error.what();
					}
					if (samples_parsed) {
						const auto small_rss = std::max<std::uint64_t>(1, small.process_peak_rss_bytes);
						const auto small_work = std::max<std::uint64_t>(1, small.owned_work_peak_bytes);
						const auto work_ratio = static_cast<double>(large.owned_work_peak_bytes) / small_work;
						const auto rss_ratio = static_cast<double>(large.process_peak_rss_bytes) / small_rss;
						sample_contract = small.complete && large.complete && small.owned_complete &&
						    large.owned_complete && small.match_count_complete && large.match_count_complete &&
						    small.matched_pairs == 1 && large.matched_pairs == 1 &&
						    small.geometry_tested_pairs == 1 && large.geometry_tested_pairs == 1 &&
						    small.input_wkb_bytes == large.input_wkb_bytes &&
						    small.input_vertex_count == large.input_vertex_count &&
						    small.value_data_bytes > 0 && large.value_data_bytes > 0 &&
						    small.value_decoded_chunks > 0 && large.value_decoded_chunks > 0 &&
						    small.result_rows == large.result_rows && small.result_sum == large.result_sum &&
						    small.result_min == large.result_min && small.result_max == large.result_max;
						if (Passed(gate.commands) && sample_contract && work_ratio <= 2.0 && rss_ratio <= 2.0) {
							gate.status = "pass";
						} else {
							gate.status = "fail";
							gate.reason = !Passed(gate.commands) ? "a P3 zero-I/O, selector, SQL, metrics, or probe command failed" :
							              !sample_contract ? "resource probes did not preserve complete equal narrow results and metrics" :
							              work_ratio > 2.0 ? "16x point growth exceeded the 2x DuckOMO-owned work peak bound" :
							                                "isolated process peak RSS exceeded the 2x bound";
						}
					} else {
						gate.status = "fail";
						gate.reason = probe_error.empty() ? "a P3 resource or regression command failed" : probe_error;
					}
				}
				const auto report = options.output / "P3-resources.json";
				const auto sample_json = [](const RegionResourceSample &sample) {
					std::ostringstream out;
					out << "{\"point_count\":" << sample.point_count << ",\"result_rows\":" << sample.result_rows
					    << ",\"result_sum\":" << sample.result_sum << ",\"matched_pairs_exact\":"
					    << sample.matched_pairs << ",\"geometry_tested_pairs\":" << sample.geometry_tested_pairs
					    << ",\"input_wkb_bytes\":" << sample.input_wkb_bytes
					    << ",\"input_vertex_count\":" << sample.input_vertex_count
					    << ",\"value_data_bytes\":" << sample.value_data_bytes
					    << ",\"value_decoded_chunks\":" << sample.value_decoded_chunks
					    << ",\"owned_work_peak_bytes\":" << sample.owned_work_peak_bytes
					    << ",\"owned_work_bound_bytes\":" << sample.owned_work_bound_bytes
					    << ",\"startup_peak_rss_bytes\":" << sample.startup_peak_rss_bytes
					    << ",\"process_peak_rss_bytes\":" << sample.process_peak_rss_bytes
					    << ",\"complete\":" << (sample.complete ? "true" : "false")
					    << ",\"owned_complete\":" << (sample.owned_complete ? "true" : "false")
					    << ",\"output_sha256\":" << (sample.output_sha256.empty() ? "null" : JsonString(sample.output_sha256))
					    << '}';
					return out.str();
				};
				const auto memory_points_ratio = static_cast<double>(1024ULL * 1024ULL) / (256ULL * 256ULL);
				const auto work_ratio = samples_parsed
				                            ? static_cast<double>(large.owned_work_peak_bytes) /
				                                  std::max<std::uint64_t>(1, small.owned_work_peak_bytes)
				                            : 0.0;
				const auto rss_ratio = samples_parsed
				                           ? static_cast<double>(large.process_peak_rss_bytes) /
				                                 std::max<std::uint64_t>(1, small.process_peak_rss_bytes)
				                           : 0.0;
				std::ostringstream report_json;
				report_json << std::fixed << std::setprecision(6)
				            << "{\n  \"schema_version\":1,\n  \"gate\":\"P3\",\n  \"status\":"
				            << JsonString(gate.status) << ",\n  \"reason\":" << JsonString(gate.reason)
				            << ",\n  \"duckdb_version\":" << JsonString(RunProcess({options.duckdb.string(), "--version"}, options.root).output)
				            << ",\n  \"inputs\":{\"sample_manifest_sha256\":" << HashFileOrNull(sample_manifest, options.root)
				            << ",\"resource_wkb_manifest_sha256\":" << HashFileOrNull(resource_manifest, options.root)
				            << ",\"resource_wkb_sha256\":" << HashFileOrNull(region_wkb, options.root)
				            << ",\"region_scan_source_sha256\":"
				            << HashFileOrNull(options.root / "test/native/region_scan_test.cpp", options.root)
				            << ",\"memory_small\":{\"bytes\":";
				if (fs::is_regular_file(small_fixture)) report_json << fs::file_size(small_fixture);
				else report_json << "null";
				report_json << ",\"sha256\":" << HashFileOrNull(small_fixture, options.root)
				            << "},\"memory_large\":{\"bytes\":";
				if (fs::is_regular_file(large_fixture)) report_json << fs::file_size(large_fixture);
				else report_json << "null";
				report_json << ",\"sha256\":" << HashFileOrNull(large_fixture, options.root)
				            << "},\"duckomo_sha256\":" << HashFileOrNull(options.extension, options.root)
				            << ",\"spatial_sha256\":" << HashFileOrNull(options.spatial, options.root) << "},\n"
				            << "  \"fixture_contract\":{\"small_shape\":[256,256],\"large_shape\":[1024,1024],"
				               "\"shared_chunk_shape\":[32,32],\"point_count_ratio\":" << memory_points_ratio << "},\n"
				            << "  \"small\":" << (samples_parsed ? sample_json(small) : "null")
				            << ",\n  \"large\":" << (samples_parsed ? sample_json(large) : "null")
				            << ",\n  \"ratios\":{\"owned_work_peak\":" << work_ratio
				            << ",\"isolated_process_peak_rss\":" << rss_ratio << "},\n"
				            << "  \"subchecks\":{\"zero_io_filters_parallel_fallback_and_legacy_metrics\":"
				            << (Passed(gate.commands) ? "true" : "false") << ",\"equal_narrow_result\":"
				            << (samples_parsed && small.result_rows == large.result_rows && small.result_sum == large.result_sum ? "true" : "false")
				            << ",\"owned_work_ratio_at_most_2\":" << (samples_parsed && work_ratio <= 2.0 ? "true" : "false")
				            << ",\"process_rss_ratio_at_most_2\":" << (samples_parsed && rss_ratio <= 2.0 ? "true" : "false")
			            << ",\"p4_resource_parallel_subset\":"
			            << (samples_parsed && Passed(gate.commands) && sample_contract && work_ratio <= 2.0 && rss_ratio <= 2.0
			                    ? "true"
			                    : "false")
			            << ",\"p4_lifecycle\":\"not-run\"},\n  \"limitations\":{\"spatial_internal_peak_bytes\":null,"
				               "\"spatial_internal_memory_complete\":false,\"rss_scope\":\"isolated process; DuckDB and extension dependency memory is not separable\","
				               "\"startup_peak_rss_is_recorded\":true}\n}\n";
				std::ofstream report_stream(report, std::ios::binary | std::ios::trunc);
				if (!report_stream.good()) throw std::runtime_error("cannot write P3 resource evidence report");
				report_stream << report_json.str();
				report_stream.close();
				if (!report_stream) throw std::runtime_error("failed to flush P3 resource evidence report");
				gate.evidence_report = fs::relative(report, options.output).string();
				gate.evidence_report_sha256 = HashFile(report, options.root);
			} else if (gate.id == "P4") {
				const auto input = native / "region_input_test";
				const auto geometry = native / "region_geometry_test";
				const auto scan = native / "region_scan_test";
				const auto metrics_v3 = native / "scan_metrics_v3_test";
				const auto metrics_v4 = native / "scan_metrics_v4_test";
				const auto core_functions = build / "extension/core_functions/core_functions.duckdb_extension";
				const auto resource_manifest = options.root / "test/data/regions/resource-memory-center.json";
				const auto small_fixture = options.root / "test/data/grids/memory-small.om";
				const auto large_fixture = options.root / "test/data/grids/memory-large.om";
				const auto region_wkb = options.root / "test/data/regions/resource-memory-center.wkb";
				std::string unavailable;
				for (const auto &path : {input, geometry, scan, metrics_v3, metrics_v4, core_functions,
				                          resource_manifest, small_fixture, large_fixture, region_wkb}) {
					if (!fs::is_regular_file(path)) {
						unavailable = "P4 requires " + path.string();
						break;
					}
				}
				RegionResourceSample small;
				RegionResourceSample large;
				double cancellation_ms = -1;
				bool sample_contract = false;
				std::string probe_error;
				if (!unavailable.empty()) {
					gate.reason = unavailable;
				} else {
					gate.commands.push_back(Run(gate.id, {input.string()}, options.root, options.output, 1));
					gate.commands.push_back(Run(gate.id,
					    {geometry.string(), options.spatial.string(), HashFile(options.spatial, options.root)},
					    options.root, options.output, 2));
					gate.commands.push_back(Run(gate.id,
					    {scan.string(), options.spatial.string(), core_functions.string()}, options.root,
					    options.output, 3));
					gate.commands.push_back(Run(gate.id, {metrics_v3.string()}, options.root, options.output, 4));
					gate.commands.push_back(Run(gate.id, {metrics_v4.string()}, options.root, options.output, 5));
					AddSqlCommand(gate, options, runner, "test/sql/read_om.test", gate.commands.size() + 1);
					AddSqlCommand(gate, options, runner, "test/sql/regions_spatial_dependency.test", gate.commands.size() + 1);
					AddSqlCommand(gate, options, runner, "test/sql/regions.test", gate.commands.size() + 1);
					const auto small_probe = gate.commands.size();
					gate.commands.push_back(Run(gate.id,
					    {scan.string(), options.spatial.string(), core_functions.string(), "--resource-probe", "small"},
					    options.root, options.output, gate.commands.size() + 1));
					const auto large_probe = gate.commands.size();
					gate.commands.push_back(Run(gate.id,
					    {scan.string(), options.spatial.string(), core_functions.string(), "--resource-probe", "large"},
					    options.root, options.output, gate.commands.size() + 1));
					try {
						cancellation_ms = CancellationResponseMilliseconds(gate.commands[2].result.output);
						small = ReadResourceSample(gate.commands[small_probe], 256ULL * 256ULL,
						                          options.output / gate.commands[small_probe].output_file, options.root);
						large = ReadResourceSample(gate.commands[large_probe], 1024ULL * 1024ULL,
						                          options.output / gate.commands[large_probe].output_file, options.root);
						sample_contract = small.complete && large.complete && small.owned_complete &&
						    large.owned_complete && small.match_count_complete && large.match_count_complete &&
						    small.matched_pairs == 1 && large.matched_pairs == 1 &&
						    small.input_wkb_bytes == large.input_wkb_bytes &&
						    small.input_vertex_count == large.input_vertex_count &&
						    small.value_data_bytes > 0 && large.value_data_bytes > 0 &&
						    small.value_decoded_chunks > 0 && large.value_decoded_chunks > 0 &&
						    small.result_rows == large.result_rows && small.result_sum == large.result_sum &&
						    small.result_min == large.result_min && small.result_max == large.result_max;
					} catch (const std::exception &error) {
						probe_error = error.what();
					}
				const auto work_ratio = static_cast<double>(large.owned_work_peak_bytes) /
				                        std::max<std::uint64_t>(1, small.owned_work_peak_bytes);
				const auto rss_ratio = static_cast<double>(large.process_peak_rss_bytes) /
				                       std::max<std::uint64_t>(1, small.process_peak_rss_bytes);
				const bool lifecycle_output = gate.commands.size() >= 3 &&
				    gate.commands[2].result.output.find("short-read/decoder/object-change failure recovery") != std::string::npos &&
				    gate.commands[2].result.output.find("multi-connection lifecycle passed") != std::string::npos;
				if (Passed(gate.commands) && lifecycle_output && sample_contract && work_ratio <= 2.0 && rss_ratio <= 2.0) {
					gate.status = "pass";
				} else {
					gate.status = "fail";
					gate.reason = !Passed(gate.commands) ? "a P4 boundary, dependency, lifecycle, SQL, metrics, or resource command failed" :
					              !lifecycle_output ? "region lifecycle assertions did not complete" :
					              !sample_contract ? (probe_error.empty() ? "resource probes did not preserve complete equal narrow results and metrics" : probe_error) :
					              work_ratio > 2.0 ? "16x point growth exceeded the 2x DuckOMO-owned work peak bound" :
					                                "isolated process peak RSS exceeded the 2x bound";
				}
				const auto report = options.output / "P4-lifecycle.json";
				std::ofstream report_stream(report, std::ios::binary | std::ios::trunc);
				if (!report_stream.good()) throw std::runtime_error("cannot write P4 lifecycle evidence report");
				report_stream << std::fixed << std::setprecision(6)
				              << "{\n  \"schema_version\":1,\n  \"gate\":\"P4\",\n  \"status\":"
				              << JsonString(gate.status) << ",\n  \"reason\":" << JsonString(gate.reason)
				              << ",\n  \"cancellation_response_ms\":";
				if (cancellation_ms >= 0) report_stream << cancellation_ms;
				else report_stream << "null";
				report_stream << ",\n  \"resources\":{\"point_count_ratio\":16.0,\"owned_work_peak_ratio\":"
				              << work_ratio << ",\"isolated_process_peak_rss_ratio\":" << rss_ratio
				              << ",\"owned_complete\":"
				              << (small.owned_complete && large.owned_complete ? "true" : "false")
				              << ",\"exact_complete\":"
				              << (small.match_count_complete && large.match_count_complete ? "true" : "false")
				              << "},\n  \"subchecks\":{\"input_limits\":"
				              << (gate.commands.size() > 0 && gate.commands[0].result.exit_code == 0 ? "true" : "false")
				              << ",\"geometry_adapter_and_state\":"
				              << (gate.commands.size() > 1 && gate.commands[1].result.exit_code == 0 ? "true" : "false")
				              << ",\"cancel_shortread_decoder_objectchange_limit_multiscan_multiconnection_recovery\":"
				              << (lifecycle_output ? "true" : "false")
				              << ",\"dependency_isolation\":"
				              << (gate.commands.size() > 6 && gate.commands[6].result.exit_code == 0 ? "true" : "false")
				              << ",\"ordinary_read_regression\":"
				              << (gate.commands.size() > 5 && gate.commands[5].result.exit_code == 0 ? "true" : "false")
				              << ",\"regions_sql_boundaries\":"
				              << (gate.commands.size() > 7 && gate.commands[7].result.exit_code == 0 ? "true" : "false")
				              << "},\n  \"limitations\":{\"spatial_internal_peak_bytes\":null,"
				                 "\"spatial_internal_memory_complete\":false,"
				                 "\"cancellation_is_checked_around_each_spatial_call\":true,"
				                 "\"single_spatial_call_has_fixed_interrupt_bound\":false}\n}\n";
				report_stream.close();
				if (!report_stream) throw std::runtime_error("failed to flush P4 lifecycle evidence report");
				gate.evidence_report = fs::relative(report, options.output).string();
				gate.evidence_report_sha256 = HashFile(report, options.root);
				}
			} else if (gate.id == "P5") {
				const auto region_manifest = options.root / "test/data/regions/real-targets.json";
				const auto runner = options.root / "scripts/validate-official-httpfs.py";
				std::vector<std::string> unavailable;
				if (!fs::is_regular_file(options.httpfs)) unavailable.emplace_back("official HTTPFS artifact");
				if (!fs::is_regular_file(options.remote_env)) unavailable.emplace_back("generated remote run.env");
				if (!fs::is_directory(options.server_log)) unavailable.emplace_back("controlled service log directory");
				if (!fs::is_regular_file(region_manifest)) unavailable.emplace_back("frozen five-target manifest");
				if (!fs::is_regular_file(runner)) unavailable.emplace_back("official HTTPFS validator");
				if (!unavailable.empty()) {
					std::ostringstream reason;
					reason << "P5 not-run; missing ";
					for (std::size_t i = 0; i < unavailable.size(); ++i) {
						if (i) reason << ", ";
						reason << unavailable[i];
					}
					WriteP5NotRun(options, gate, reason.str());
				} else {
					const auto p5_output = options.gates.size() == 1 && options.gates.front() == "P5"
				                           ? options.output
				                           : options.output / "P5";
					gate.commands.push_back(Run(
					    gate.id,
					    {"python3", runner.string(), "--root", options.root.string(), "--duckdb", options.duckdb.string(),
					     "--extension", options.extension.string(), "--httpfs", options.httpfs.string(),
					     "--spatial", options.spatial.string(), "--region-manifest", region_manifest.string(),
					     "--remote-env", options.remote_env.string(), "--server-log", options.server_log.string(),
					     "--output", p5_output.string()},
					    options.root, options.output, 1));
					const auto report = p5_output / "summary.json";
					if (!fs::is_regular_file(report)) {
						gate.status = "fail";
						gate.reason = "official HTTPFS P5 runner did not produce a summary";
					} else {
						gate.evidence_report = fs::relative(report, options.output).string();
						gate.evidence_report_sha256 = HashFile(report, options.root);
						const auto exit_code = gate.commands.back().result.exit_code;
						if (exit_code == 0) {
							gate.status = "pass";
						} else if (exit_code == 2) {
							gate.status = "not-run";
							gate.reason = "official HTTPFS P5 runner found an unavailable controlled source; see report";
						} else {
							gate.status = "fail";
							gate.reason = "a P5 result, metrics, remote byte, or service audit assertion failed";
						}
					}
				}
			} else {
				gate.reason = "this gate requires the remote or matrix validator not yet available";
		}
		}
	} else {
		const auto reason = validation_inputs.front();
		for (auto &gate : records) gate.reason = reason;
	}
	const bool any_fail = std::any_of(records.begin(), records.end(), [](const auto &gate) {
		return gate.status == "fail";
	});
	const bool any_not_run = std::any_of(records.begin(), records.end(), [](const auto &gate) {
		return gate.status == "not-run";
	});
	const std::string overall = any_fail ? "fail" : (any_not_run ? "not-run" : "pass");
	{
		std::ofstream manifest(options.output / "manifest.json", std::ios::binary | std::ios::trunc);
		if (!manifest.good()) throw std::runtime_error("cannot write region validation manifest");
		manifest << "{\n  \"schema_version\":1,\n  \"status\":" << JsonString(overall)
		         << ",\n  \"manifest_path\":" << JsonString(options.manifest.string())
		         << ",\n  \"manifest_sha256\":";
		if (fs::is_regular_file(options.manifest)) manifest << JsonString(HashFile(options.manifest, options.root));
		else manifest << "null";
		const auto duckdb_version = RunProcess({options.duckdb.string(), "--version"}, options.root);
		std::istringstream duckdb_version_stream(duckdb_version.output);
		std::string runtime_version;
		duckdb_version_stream >> runtime_version;
		manifest << ",\n  \"runtime_version\":" << JsonString(runtime_version)
		         << ",\n  \"duckdb_path\":" << JsonString(options.duckdb.string())
		         << ",\n  \"duckdb_sha256\":";
		if (fs::is_regular_file(options.duckdb)) manifest << JsonString(HashFile(options.duckdb, options.root));
		else manifest << "null";
		manifest << ",\n  \"duckomo_path\":" << JsonString(options.extension.string())
		         << ",\n  \"duckomo_sha256\":";
		if (fs::is_regular_file(options.extension)) manifest << JsonString(HashFile(options.extension, options.root));
		else manifest << "null";
		manifest << ",\n  \"spatial_path\":" << JsonString(options.spatial.string())
		         << ",\n  \"spatial_sha256\":";
		if (fs::is_regular_file(options.spatial)) manifest << JsonString(HashFile(options.spatial, options.root));
		else manifest << "null";
		manifest << ",\n  \"requested_gates\":" << JsonArray(options.gates) << ",\n  \"gates\":[";
		for (std::size_t i = 0; i < records.size(); ++i) {
			if (i) manifest << ',';
			const auto &gate = records[i];
			manifest << "{\"id\":" << JsonString(gate.id) << ",\"status\":" << JsonString(gate.status)
			         << ",\"reason\":" << JsonString(gate.reason) << ",\"commands\":[";
			for (std::size_t j = 0; j < gate.commands.size(); ++j) {
				if (j) manifest << ',';
				const auto &command = gate.commands[j];
				manifest << "{\"argv\":" << JsonArray(command.argv)
				         << ",\"exit_code\":" << command.result.exit_code
				         << ",\"elapsed_ms\":" << command.result.elapsed_ms
				         << ",\"peak_rss_bytes\":" << command.result.peak_rss_bytes
				         << ",\"output_path\":" << JsonString(command.output_file)
				         << ",\"output_sha256\":" << JsonString(command.output_sha256) << '}';
			}
			manifest << ']';
			if (gate.id == "P2") {
				manifest << ",\"statistics_groups\":" << gate.statistics_groups
				         << ",\"statistics_mismatches\":";
				if (gate.status == "pass") manifest << gate.statistics_mismatches;
				else manifest << "null";
				manifest << ",\"statistics_oracle_sha256\":"
				         << (gate.oracle_sha256.empty() ? "null" : JsonString(gate.oracle_sha256));
			}
			if (!gate.evidence_report.empty()) {
				manifest << ",\"evidence_report\":" << JsonString(gate.evidence_report)
				         << ",\"evidence_report_sha256\":" << JsonString(gate.evidence_report_sha256);
			}
			manifest << '}';
		}
		manifest << "]\n}\n";
	}
	{
		std::ofstream final(options.output / "final.md", std::ios::binary | std::ios::trunc);
		final << "# Region validation run\n\nStatus: **" << overall << "**\n\n";
		for (const auto &gate : records)
			final << "- " << gate.id << ": " << gate.status << (gate.reason.empty() ? "" : " — " + gate.reason) << '\n';
		if (!validation_inputs.empty()) final << "\nMissing input: " << validation_inputs.front() << '\n';
	}
	std::cout << "Recorded " << records.size() << " region gate(s), status=" << overall
	          << ", output=" << options.output << '\n';
	return any_fail ? 1 : (any_not_run ? 2 : 0);
}

void SelfCheck() {
	if (ParseGates("P0, P2") != std::vector<std::string>{"P0", "P2"})
		throw std::runtime_error("gate parser failed");
	bool rejected = false;
	try { (void)ParseGates("P0,P0"); } catch (const std::invalid_argument &) { rejected = true; }
	if (!rejected || JsonString("a\nb") != "\"a\\nb\"")
		throw std::runtime_error("validator self-check failed");
}

} // namespace

int main(int argc, char **argv) {
	try {
		const auto options = ParseOptions(argc, argv);
		if (options.self_check) {
			SelfCheck();
			std::cout << "region validation CLI self-check passed\n";
			return 0;
		}
		if (options.gates.size() == 1 && options.gates.front() == "P6") return WriteP6Summary(options);
		return WriteRun(options);
	} catch (const std::exception &error) {
		std::cerr << "duckomo_region_validation: " << error.what() << '\n';
		return 2;
	}
}
