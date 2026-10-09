#include "validation_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using duckomo_validation_support::GridGateResult;
using duckomo_validation_support::CommandResult;
using duckomo_validation_support::AuditGridGateResults;
using duckomo_validation_support::JsonEscape;
using duckomo_validation_support::JsonString;
using duckomo_validation_support::ParseGridValidationCases;
using duckomo_validation_support::RunProcess;

namespace {

struct Options final {
	fs::path root;
	fs::path manifest;
	fs::path duckdb;
	fs::path extension;
	fs::path httpfs;
	fs::path matrix;
	fs::path matrix_evidence_root;
	fs::path output;
	fs::path s3_setup;
	fs::path server_log;
	std::string cases;
	std::string http_base;
	std::string https_base;
	std::string s3_base;
	bool self_check = false;
};

void PrintUsage(std::ostream &out) {
	out << "Usage: duckomo_grid_validation --root PATH --manifest PATH --cases H0[,H1,...] "
	       "--duckdb PATH --extension PATH --output EMPTY_DIR [options]\n"
	       "Options: --httpfs PATH --matrix PATH --http-base URL --https-base URL --s3-base URI "
	       "--matrix-evidence-root PATH --s3-setup PATH --server-log PATH --self-check\n"
	       "Cases are H0 through H9. Requested gates that are unavailable or lack inputs are recorded as not-run "
	       "and return a non-zero exit code.\n";
}

Options ParseOptions(int argc, char **argv) {
	Options options;
	std::set<std::string> seen;
	for (int i = 1; i < argc; i++) {
		const std::string name = argv[i];
		if (name == "--help" || name == "-h") {
			PrintUsage(std::cout);
			std::exit(0);
		}
		if (name == "--self-check") {
			if (argc != 2) throw std::invalid_argument("--self-check cannot be combined with other options");
			options.self_check = true;
			continue;
		}
		if (!seen.insert(name).second) throw std::invalid_argument("duplicate option " + name);
		if (i + 1 >= argc) throw std::invalid_argument("missing value after " + name);
		const std::string value = argv[++i];
		if (name == "--root") options.root = value;
		else if (name == "--manifest") options.manifest = value;
		else if (name == "--cases") options.cases = value;
		else if (name == "--duckdb") options.duckdb = value;
		else if (name == "--extension") options.extension = value;
		else if (name == "--httpfs") options.httpfs = value;
		else if (name == "--matrix") options.matrix = value;
		else if (name == "--matrix-evidence-root") options.matrix_evidence_root = value;
		else if (name == "--output") options.output = value;
		else if (name == "--http-base") options.http_base = value;
		else if (name == "--https-base") options.https_base = value;
		else if (name == "--s3-base") options.s3_base = value;
		else if (name == "--s3-setup") options.s3_setup = value;
		else if (name == "--server-log") options.server_log = value;
		else throw std::invalid_argument("unknown option " + name);
	}
	if (options.self_check) return options;
	if (options.root.empty() || options.manifest.empty() || options.cases.empty() || options.duckdb.empty() ||
	    options.extension.empty() || options.output.empty()) {
		throw std::invalid_argument("--root, --manifest, --cases, --duckdb, --extension, and --output are required");
	}
	options.root = fs::absolute(options.root).lexically_normal();
	auto resolve = [&](fs::path &path) {
		if (!path.empty() && path.is_relative()) path = options.root / path;
		if (!path.empty()) path = fs::absolute(path).lexically_normal();
	};
	resolve(options.manifest);
	resolve(options.duckdb);
	resolve(options.extension);
	resolve(options.httpfs);
	if (options.matrix.empty()) options.matrix = options.root / "test/data/grids/version-matrix.json";
	resolve(options.matrix);
	resolve(options.matrix_evidence_root);
	resolve(options.output);
	resolve(options.s3_setup);
	resolve(options.server_log);
	if (options.httpfs.empty()) {
		const auto official_httpfs = options.duckdb.parent_path() / "httpfs.duckdb_extension";
		if (fs::is_regular_file(official_httpfs)) options.httpfs = official_httpfs;
	}
	if (options.matrix_evidence_root.empty())
		options.matrix_evidence_root = options.root / "specs/004-multi-grid-selection/evidence";
	(void)ParseGridValidationCases(options.cases);
	return options;
}

std::string UtcNow() {
	const auto now = std::chrono::system_clock::now();
	const auto value = std::chrono::system_clock::to_time_t(now);
	std::tm utc{};
	gmtime_r(&value, &utc);
	std::ostringstream result;
	result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
	return result.str();
}

std::optional<std::string> Sha256(const fs::path &path, const fs::path &root) {
	if (!fs::is_regular_file(path)) return std::nullopt;
	const auto result = RunProcess({"sha256sum", path.string()}, root);
	if (result.exit_code != 0) return std::nullopt;
	const auto end = result.output.find_first_of(" \t\r\n");
	if (end == std::string::npos) return std::nullopt;
	return result.output.substr(0, end);
}

std::string FileRecord(const std::string &name, const fs::path &path, const fs::path &root, bool executable = false) {
	const bool exists = fs::is_regular_file(path);
	std::ostringstream json;
	json << JsonString(name) << ":{\"path\":" << JsonString(path.string()) << ",\"exists\":"
	     << (exists ? "true" : "false") << ",\"executable\":"
	     << ((exists && (!executable || access(path.c_str(), X_OK) == 0)) ? "true" : "false")
	     << ",\"sha256\":";
	const auto hash = Sha256(path, root);
	json << (hash ? JsonString(*hash) : "null") << "}";
	return json.str();
}

bool HasRemoteAuditLogs(const fs::path &directory) {
	return fs::is_directory(directory) && fs::is_regular_file(directory / "http.jsonl") &&
	       fs::is_regular_file(directory / "s3.jsonl");
}

std::string ArtifactRecord(const fs::path &path, const fs::path &root) {
	std::ostringstream json;
	json << "{\"path\":" << JsonString(path.string()) << ",\"sha256\":";
	const auto hash = Sha256(path, root);
	json << (hash ? JsonString(*hash) : "null") << "}";
	return json.str();
}

std::string RedactedArguments(int argc, char **argv) {
	static const std::set<std::string> REDACTED_VALUE_OPTIONS{
	    "--http-base", "--https-base", "--s3-base", "--s3-setup", "--server-log"};
	std::ostringstream json;
	json << "[";
	bool first = true;
	bool redact_next = false;
	for (int i = 0; i < argc; i++) {
		if (!first) json << ",";
		first = false;
		const std::string value = argv[i];
		json << JsonString(redact_next ? "<redacted>" : value);
		redact_next = REDACTED_VALUE_OPTIONS.count(value) != 0;
	}
	json << "]";
	return json.str();
}

std::vector<std::string> MissingInputs(const Options &options, const std::vector<std::string> &cases) {
	std::vector<std::string> missing;
	if (!fs::is_directory(options.root)) missing.push_back("--root directory is missing");
	if (!fs::is_regular_file(options.manifest)) missing.push_back("--manifest file is missing");
	if (!fs::is_regular_file(options.duckdb) || access(options.duckdb.c_str(), X_OK) != 0)
		missing.push_back("--duckdb executable is missing");
	if (!fs::is_regular_file(options.extension)) missing.push_back("--extension file is missing");
	const auto has_case = [&](const std::string &id) {
		return std::find(cases.begin(), cases.end(), id) != cases.end();
	};
	if (has_case("H6")) {
		if (!fs::is_regular_file(options.httpfs)) missing.push_back("H6 requires an existing --httpfs file");
		if (options.http_base.empty() || options.https_base.empty() || options.s3_base.empty())
			missing.push_back("H6 requires --http-base, --https-base, and --s3-base");
		if (options.s3_setup.empty() || !fs::is_regular_file(options.s3_setup))
			missing.push_back("H6 requires an existing --s3-setup file");
		if (options.server_log.empty() || !HasRemoteAuditLogs(options.server_log))
			missing.push_back("H6 requires --server-log to name the setup-remote-fixtures log directory "
			                  "containing http.jsonl and s3.jsonl");
	}
	if (has_case("H8")) {
		if (!fs::is_regular_file(options.httpfs)) missing.push_back("H8 requires an existing --httpfs file");
		if (!fs::is_regular_file(options.matrix)) missing.push_back("H8 requires an existing --matrix file");
	}
	return missing;
}

struct H3LocalChecks final {
	bool executed = false;
	bool passed = false;
	std::string reason;
	fs::path native_test;
	fs::path sql_test;
	fs::path source_native_test;
	fs::path source_sql_test;
	fs::path grid_info_sql_test;
	fs::path public_source_validator;
	fs::path public_source_directory;
	fs::path public_source_report;
	fs::path core_functions;
	fs::path metrics_file;
	CommandResult native_result;
	CommandResult sql_result;
	CommandResult source_native_result;
	CommandResult source_sql_result;
	CommandResult grid_info_sql_result;
	CommandResult public_source_result;
	CommandResult metrics_validation;
	std::vector<std::string> public_source_arguments;
	std::string metrics_assertion;
};

struct H6LocalChecks final {
	bool executed = false;
	bool passed = false;
	std::string reason;
	fs::path directory;
	fs::path remote_session_test;
	fs::path stdout_file;
	CommandResult result;
};

H3LocalChecks RunH3LocalChecks(const Options &options) {
	H3LocalChecks result;
	const auto test_directory = options.duckdb.parent_path() / "test";
	result.native_test = test_directory / "native/grid_zero_io_test";
	result.sql_test = options.root / "test/sql/grid_zero_io.test";
	result.source_native_test = test_directory / "native/source_identity_test";
	result.source_sql_test = options.root / "test/sql/source_identity.test";
	result.grid_info_sql_test = options.root / "test/sql/grid_info.test";
	result.public_source_validator = options.root / "test/tools/grid_spatial_reference.py";
	const auto unittest = test_directory / "unittest";
	if (!fs::is_regular_file(result.native_test) || access(result.native_test.c_str(), X_OK) != 0 ||
	    !fs::is_regular_file(result.source_native_test) || access(result.source_native_test.c_str(), X_OK) != 0 ||
	    !fs::is_regular_file(result.sql_test) || !fs::is_regular_file(result.source_sql_test) ||
	    !fs::is_regular_file(result.grid_info_sql_test) || !fs::is_regular_file(unittest) ||
	    access(unittest.c_str(), X_OK) != 0) {
		result.reason = "H3 local checks require the zero-I/O and source-identity native binaries, their SQLLogicTest "
		                "files, and the matching test/unittest";
		return result;
	}
	for (const auto &directory : {options.duckdb.parent_path() / "repository",
	                              options.duckdb.parent_path() / "extension"}) {
		if (!fs::is_directory(directory)) continue;
		for (const auto &entry : fs::recursive_directory_iterator(directory)) {
			if (entry.is_regular_file() && entry.path().filename() == "core_functions.duckdb_extension") {
				result.core_functions = fs::absolute(entry.path());
				break;
			}
		}
		if (!result.core_functions.empty()) break;
	}
	if (result.core_functions.empty()) {
		result.reason = "H3 native check requires the matching build's core_functions extension";
		return result;
	}
	const auto evidence_directory = options.output / "h3-local";
	fs::create_directories(evidence_directory);
	result.public_source_directory = evidence_directory / "public-source";
	result.public_source_report = result.public_source_directory / "public-source-report.json";
	if (!fs::is_regular_file(result.public_source_validator) || !fs::is_regular_file(options.duckdb) ||
	    access(options.duckdb.c_str(), X_OK) != 0 || !fs::is_regular_file(options.extension)) {
		result.reason = "H3 public source/info subcheck requires the validator and matching DuckDB CLI/extension pair";
		return result;
	}
	result.metrics_file = evidence_directory / "zero-io-metrics.json";
	result.executed = true;
	result.native_result = RunProcess({result.native_test.string()}, options.root,
	                                  {{"DUCKOMO_CORE_FUNCTIONS_EXTENSION", result.core_functions.string()},
	                                   {"DUCKOMO_GRID_ZERO_IO_EVIDENCE", result.metrics_file.string()}});
	result.sql_result = RunProcess({unittest.string(), result.sql_test.string()}, options.root);
	result.source_native_result = RunProcess({result.source_native_test.string()}, options.root);
	result.source_sql_result = RunProcess({unittest.string(), result.source_sql_test.string()}, options.root);
	result.grid_info_sql_result = RunProcess({unittest.string(), result.grid_info_sql_test.string()}, options.root);
	result.public_source_arguments = {
	    "python3", result.public_source_validator.string(), "--validate-public-sources",
	    "--all-public-source-positions", "--public-spatial-selection", "--duckdb",
	    options.duckdb.string(), "--extension", options.extension.string(), "--root", options.root.string(),
	    "--output-dir", result.public_source_directory.string()};
	result.public_source_result = RunProcess(result.public_source_arguments, options.root);
	result.metrics_assertion =
	    "def zero: .index_bytes == 0 and .index_requests == 0 and .data_bytes == 0 and "
	    ".data_requests == 0 and .decoded_chunks == 0 and .decode_complete == true; "
	    "all(.scenarios[].metrics; .schema_version == 4 and "
	    ".outcome.status == \"success\" and .outcome.terminal_published == true and "
	    ".coordinate_preparation.complete == true and "
	    "(.coordinate_preparation.evaluations | type == \"number\")) and "
	    "([.scenarios[] | select(.name == \"interleaved_value_filter\")] | length == 1) and "
	    "all(.scenarios[] | select(.name != \"interleaved_value_filter\") | .metrics.reads.variables[]; zero) and "
	    "(first(.scenarios[] | select(.name == \"interleaved_value_filter\")) | "
	    "(.metrics.reads.variables[\"/interleaved/temperature\"] as $t | "
	    "$t.data_bytes > 0 and $t.decoded_chunks > 0 and "
	    "(.metrics.reads.variables[\"/interleaved/humidity\"] | zero))) and "
	    "([.scenarios[].name] | unique | length == 18)";
	result.metrics_validation = RunProcess({"jq", "-er", result.metrics_assertion, result.metrics_file.string()}, options.root);
	result.passed = result.native_result.exit_code == 0 && result.sql_result.exit_code == 0 &&
	                result.source_native_result.exit_code == 0 && result.source_sql_result.exit_code == 0 &&
	                result.grid_info_sql_result.exit_code == 0 && result.metrics_validation.exit_code == 0 &&
	                result.public_source_result.exit_code == 0 && fs::is_regular_file(result.public_source_report);
	result.reason = result.passed ?
	                    "synthetic local zero-I/O/source-identity checks, all spatial positions, and bounded spatial subsets for three hash-pinned public projected samples passed at valid_time index 0; "
	                    "independent OM axis mapping and full H3 remain not-run" :
	                    "one or more synthetic or public local zero-I/O/source-identity checks failed";
	return result;
}

H6LocalChecks RunH6LocalChecks(const Options &options) {
	H6LocalChecks result;
	result.directory = options.output / "h6-local";
	result.remote_session_test = options.duckdb.parent_path() / "test/native/remote_session_test";
	result.stdout_file = result.directory / "remote-session.stdout.txt";
	if (!fs::is_regular_file(result.remote_session_test) ||
	    access(result.remote_session_test.c_str(), X_OK) != 0) {
		result.reason = "H6 local/loopback subchecks require the matching build's remote_session_test binary";
		return result;
	}
	fs::create_directories(result.directory);
	result.result = RunProcess({result.remote_session_test.string()}, options.root,
	                           {{"DUCKOMO_H6_EVIDENCE_DIR", result.directory.string()}});
	result.executed = true;
	{
		std::ofstream output(result.stdout_file, std::ios::binary | std::ios::trunc);
		if (!output.good()) throw std::runtime_error("cannot write H6 local/loopback runner output");
		output << result.result.output;
		if (!output.good()) throw std::runtime_error("cannot write H6 local/loopback runner output");
	}
	result.passed = result.result.exit_code == 0;
	result.reason = result.passed
	    ? "standard file Interface handle/short-read/permission/cancellation/recovery tests passed; actual official HTTPFS transport gates are recorded separately"
	    : "standard file Interface tests failed";
	return result;
}

struct H7LocalChecks final {
	bool inputs_ready = false;
	bool executed = false;
	bool passed = false;
	std::string reason;
	fs::path directory;
	fs::path validator;
	fs::path oracle;
	fs::path sample_manifest;
	fs::path source_manifest;
	fs::path coordinate_reference;
	fs::path projected_fixture;
	fs::path gaussian_fixture;
	fs::path query_directory;
	fs::path summary;
	fs::path stdout_file;
	std::vector<std::string> arguments;
	CommandResult result;
};

struct H8MatrixChecks final {
	bool executed = false;
	fs::path directory;
	fs::path script;
	fs::path summary;
	fs::path stdout_file;
	std::vector<std::string> arguments;
	CommandResult result;
	std::string reason;
};

H8MatrixChecks RunH8MatrixChecks(const Options &options) {
	H8MatrixChecks result;
	result.directory = options.output / "h8-version-matrix";
	result.script = options.root / "scripts/audit-grid-version-matrix.py";
	result.summary = result.directory / "h8-summary.json";
	result.stdout_file = result.directory / "runner.stdout.txt";
	const auto &identity = duckdb::duckomo::BUILD_IDENTITY;
	if (!fs::is_regular_file(result.script) || !fs::is_regular_file(options.matrix) ||
	    !fs::is_regular_file(options.manifest) || !fs::is_regular_file(options.duckdb) ||
	    !fs::is_regular_file(options.extension) || !fs::is_regular_file(options.httpfs) ||
	    identity.pair_id == nullptr || identity.pair_id[0] == '\0' || identity.build_id == nullptr ||
	    identity.build_id[0] == '\0') {
		result.reason = "H8 requires the official runtime matrix, current matrix build identity, and DuckDB/DuckOMO/HTTPFS artifacts";
		return result;
	}
	fs::create_directories(result.directory);
	result.arguments = {"python3", result.script.string(), "--root", options.root.string(), "--matrix",
	                    options.matrix.string(), "--sample-manifest", options.manifest.string(), "--evidence-root",
	                    options.matrix_evidence_root.string(), "--current-pair-id", identity.pair_id,
	                    "--current-build-id", identity.build_id, "--duckdb", options.duckdb.string(), "--extension",
	                    options.extension.string(), "--httpfs", options.httpfs.string(), "--output",
	                    result.summary.string()};
	result.result = RunProcess(result.arguments, options.root);
	result.executed = true;
	if (result.result.exit_code == 0) {
		result.reason = "all three official versions have audited same-input H0-H7 records and matching build identities";
	} else if (result.result.exit_code == 2) {
		result.reason = "official build identities passed; complete same-input H0-H7 gate evidence is not available";
	} else {
		result.reason = "H8 matrix identity/evidence audit failed; see h8-summary.json and runner output";
	}
	return result;
}

H7LocalChecks RunH7LocalChecks(const Options &options) {
	H7LocalChecks result;
	result.directory = options.output / "h7-local";
	result.query_directory = result.directory / "synthetic";
	result.validator = options.root / "test/tools/grid_spatial_reference.py";
	result.oracle = options.root / "test/data/grids/spatial-relations.json";
	result.sample_manifest = options.manifest;
	result.source_manifest = options.root / "test/data/grids/source-manifest.json";
	result.coordinate_reference = options.root / "test/data/grids/coordinate-reference.json";
	result.projected_fixture = options.root / "test/data/raw.om";
	result.gaussian_fixture = options.root / "test/data/grids/spatial-relations-gaussian.om";
	result.summary = result.query_directory / "h7-synthetic-report.json";
	result.stdout_file = result.directory / "runner.stdout.txt";
	if (!fs::is_regular_file(result.validator) || !fs::is_regular_file(result.oracle) ||
	    !fs::is_regular_file(result.sample_manifest) || !fs::is_regular_file(result.source_manifest) ||
	    !fs::is_regular_file(result.coordinate_reference) ||
	    !fs::is_regular_file(result.projected_fixture) || !fs::is_regular_file(result.gaussian_fixture) ||
	    !fs::is_regular_file(options.duckdb) || access(options.duckdb.c_str(), X_OK) != 0 ||
	    !fs::is_regular_file(options.extension)) {
		result.reason = "H7 synthetic source/relation checks require the independent oracle, four-case fixtures, and a "
		                "matching DuckDB CLI/extension pair";
		return result;
	}
	result.inputs_ready = true;
	fs::create_directories(result.directory);
	result.arguments = {"python3", result.validator.string(), "--validate", "--duckdb", options.duckdb.string(),
	                    "--extension", options.extension.string(), "--root", options.root.string(),
	                    "--output-dir", result.query_directory.string()};
	result.result = RunProcess(result.arguments, options.root);
	result.executed = true;
	result.passed = result.result.exit_code == 0 && fs::is_regular_file(result.summary);
	result.reason = result.passed
	                    ? "four synthetic grid types passed full relation/source-position and zero-value-read checks; "
	                      "three public projected samples passed explicit/domain identity, source/info zero-read, and first-four pinned-coordinate checks; "
	                      "full OM axis mapping, required Gaussian samples, and public remote source evidence remain not-run"
	                    : "one or more synthetic H7 source/relation checks failed; see h7-local/synthetic evidence";
	return result;
}

std::string CommandRecord(const std::vector<std::string> &arguments, const CommandResult &result) {
	std::ostringstream json;
	json << "{\"argv\":[";
	for (std::size_t index = 0; index < arguments.size(); index++) {
		if (index) json << ',';
		json << JsonString(arguments[index]);
	}
	json << "],\"exit_code\":" << result.exit_code << ",\"elapsed_ms\":" << std::fixed
	     << std::setprecision(3) << result.elapsed_ms << ",\"peak_rss_bytes\":" << result.peak_rss_bytes << '}';
	return json.str();
}

bool IsDirectoryEmpty(const fs::path &path) {
	return fs::directory_iterator(path) == fs::directory_iterator();
}

struct H0QueryGeneration final {
	bool inputs_ready = false;
	bool generation_passed = false;
	bool binding_ran = false;
	bool passed = false;
	std::string reason;
	fs::path directory;
	fs::path first_sql;
	fs::path second_sql;
	fs::path first_header;
	fs::path second_header;
	fs::path checked_sql;
	fs::path checked_header;
	fs::path generator;
	fs::path binding_script;
	fs::path definitions;
	fs::path vectors;
	CommandResult first_result;
	CommandResult second_result;
	CommandResult check_result;
	CommandResult binding_result;
	std::vector<std::string> first_arguments;
	std::vector<std::string> second_arguments;
	std::vector<std::string> check_arguments;
	std::vector<std::string> binding_arguments;
	std::optional<std::string> first_hash;
	std::optional<std::string> second_hash;
	std::optional<std::string> checked_hash;
};

H0QueryGeneration RunH0QueryGeneration(const Options &options) {
	H0QueryGeneration result;
	result.directory = options.output / "h0-query-regeneration";
	result.generator = options.root / "scripts/generate-grid-registry.py";
	result.binding_script = options.root / "scripts/validate-grid-sample-queries.py";
	result.definitions = options.root / "test/data/grids/definitions.json";
	result.vectors = options.root / "test/data/grids/canonical-vectors.json";
	result.checked_sql = options.root / "test/data/grids/sample-queries.sql";
	result.checked_header = options.root / "src/include/duckomo/generated_grid_registry.hpp";
	if (!fs::is_regular_file(result.generator) || !fs::is_regular_file(result.binding_script) ||
	    !fs::is_regular_file(result.definitions) ||
	    !fs::is_regular_file(result.vectors) || !fs::is_regular_file(result.checked_sql) ||
	    !fs::is_regular_file(result.checked_header) || !fs::is_regular_file(options.manifest)) {
		result.reason = "H0 registry/sample-query audit requires both generators, frozen definitions, vectors, sample "
		                "manifest, and checked-in generated outputs";
		return result;
	}
	result.inputs_ready = true;
	fs::create_directories(result.directory);
	result.first_sql = result.directory / "first-sample-queries.sql";
	result.second_sql = result.directory / "second-sample-queries.sql";
	result.first_header = result.directory / "first-generated-grid-registry.hpp";
	result.second_header = result.directory / "second-generated-grid-registry.hpp";
	const auto make_arguments = [&](const fs::path &header, const fs::path &sql) {
		return std::vector<std::string>{"python3", result.generator.string(), "--input", result.definitions.string(),
		                                "--vectors", result.vectors.string(), "--output", header.string(),
		                                "--sample-manifest", options.manifest.string(),
		                                "--sample-queries-output", sql.string()};
	};
	result.first_arguments = make_arguments(result.first_header, result.first_sql);
	result.second_arguments = make_arguments(result.second_header, result.second_sql);
	result.first_result = RunProcess(result.first_arguments, options.root);
	result.second_result = RunProcess(result.second_arguments, options.root);
	result.check_arguments = {"python3", result.generator.string(), "--input", result.definitions.string(), "--vectors",
	                          result.vectors.string(), "--output", result.checked_header.string(), "--sample-manifest",
	                          options.manifest.string(), "--sample-queries-output", result.checked_sql.string(),
	                          "--check"};
	result.check_result = RunProcess(result.check_arguments, options.root);
	result.first_hash = Sha256(result.first_sql, options.root);
	result.second_hash = Sha256(result.second_sql, options.root);
	result.checked_hash = Sha256(result.checked_sql, options.root);
	result.generation_passed = result.first_result.exit_code == 0 && result.second_result.exit_code == 0 &&
	                           result.check_result.exit_code == 0 && result.first_hash && result.second_hash &&
	                           result.checked_hash && *result.first_hash == *result.second_hash &&
	                           *result.first_hash == *result.checked_hash;
	if (result.generation_passed) {
		result.binding_arguments = {"python3", result.binding_script.string(), "--duckdb", options.duckdb.string(),
		                            "--extension", options.extension.string(), "--queries", result.checked_sql.string()};
		result.binding_result = RunProcess(result.binding_arguments, options.root);
		result.binding_ran = true;
	}
	result.passed = result.generation_passed && result.binding_ran && result.binding_result.exit_code == 0;
	if (!result.generation_passed) {
		result.reason = "sample-query regeneration or checked-in output audit failed; see command records";
	} else if (!result.binding_ran || result.binding_result.exit_code != 0) {
		result.reason = "generated sample views did not bind with the supplied DuckDB CLI/extension pair; see binding output";
	} else {
		result.reason = "two sample-query generations match byte-for-byte, checked-in outputs pass --check, and all "
		                "generated sample views bind with the supplied DuckDB CLI/extension pair; full H0 coordinate/value "
		                "oracle remains not-run";
	}
	return result;
}

struct H1ReferenceChecks final {
	bool inputs_ready = false;
	bool executed = false;
	bool partial_passed = false;
	std::string reason;
	fs::path directory;
	fs::path comparator;
	fs::path coordinate_reference;
	fs::path value_reference;
	fs::path summary;
	fs::path stdout_file;
	std::vector<std::string> arguments;
	CommandResult result;
};

H1ReferenceChecks RunH1ReferenceChecks(const Options &options) {
	H1ReferenceChecks result;
	result.directory = options.output / "h1-reference-checks";
	result.comparator = options.root / "scripts/compare-grid-sample-references.py";
	result.coordinate_reference = options.root / "test/data/grids/coordinate-reference.json";
	result.value_reference = options.root / "test/data/grids/value-reference-manifest.json";
	result.summary = result.directory / "h1-reference-comparison.json";
	result.stdout_file = result.directory / "runner.stdout.txt";
	if (!fs::is_regular_file(result.comparator) || !fs::is_regular_file(result.coordinate_reference) ||
	    !fs::is_regular_file(result.value_reference) || !fs::is_regular_file(options.manifest) ||
	    !fs::is_regular_file(options.duckdb) || !fs::is_regular_file(options.extension)) {
		result.reason = "H1 comparison requires the reference comparator, frozen sample/reference manifests, and a "
		                "matching DuckDB CLI/extension pair";
		return result;
	}
	result.inputs_ready = true;
	fs::create_directories(result.directory);
	result.arguments = {"python3", result.comparator.string(), "--root", options.root.string(), "--sample-manifest",
	                    options.manifest.string(), "--coordinate-reference", result.coordinate_reference.string(),
	                    "--value-reference", result.value_reference.string(), "--duckdb", options.duckdb.string(),
	                    "--extension", options.extension.string(), "--output-dir", result.directory.string()};
	result.result = RunProcess(result.arguments, options.root);
	result.executed = true;
	result.partial_passed = result.result.exit_code == 0 && fs::is_regular_file(result.summary);
	if (result.partial_passed) {
		result.reason = "all currently referenced real projected samples passed complete coordinate and Float32 value "
		                "comparisons; missing Gaussian targets and source-axis proof keep full H1 not-run";
	} else if (result.result.exit_code == 2) {
		result.reason = "no target has both frozen coordinate and official value references; H1 remains not-run";
	} else {
		result.reason = "one or more available coordinate/value reference comparisons failed; see H1 manifest";
	}
	return result;
}

struct H1PublicSourceChecks final {
	bool inputs_ready = false;
	bool executed = false;
	bool passed = false;
	std::string reason;
	fs::path directory;
	fs::path validator;
	fs::path report;
	fs::path stdout_file;
	std::vector<std::string> arguments;
	CommandResult result;
};

H1PublicSourceChecks RunH1PublicSourceChecks(const Options &options) {
	H1PublicSourceChecks result;
	result.directory = options.output / "h1-reference-checks/public-source";
	result.validator = options.root / "test/tools/grid_spatial_reference.py";
	result.report = result.directory / "public-source-report.json";
	result.stdout_file = result.directory / "runner.stdout.txt";
	fs::create_directories(result.directory);
	if (!fs::is_regular_file(result.validator) || !fs::is_regular_file(options.manifest) ||
	    !fs::is_regular_file(options.root / "test/data/grids/sample-queries.sql") ||
	    !fs::is_regular_file(options.root / "test/data/grids/coordinate-reference.json") ||
	    !fs::is_regular_file(options.duckdb) || access(options.duckdb.c_str(), X_OK) != 0 ||
	    !fs::is_regular_file(options.extension)) {
		result.reason = "H1 public source/info checks require the validator, frozen sample/query/coordinate manifests, "
		                "and matching DuckDB CLI/extension pair";
		return result;
	}
	result.inputs_ready = true;
	result.arguments = {"python3", result.validator.string(), "--validate-public-sources",
	                    "--all-public-source-positions", "--public-spatial-selection", "--duckdb",
	                    options.duckdb.string(), "--extension", options.extension.string(), "--root",
	                    options.root.string(), "--output-dir", result.directory.string()};
	result.result = RunProcess(result.arguments, options.root);
	result.executed = true;
	result.passed = result.result.exit_code == 0 && fs::is_regular_file(result.report);
	result.reason = result.passed
	                    ? "all three hash-pinned public projected samples passed full source/info zero-value-read, "
	                      "explicit/domain identity SQL, full spatial coordinate, and bounded spatial identity subchecks "
	                      "at validator-declared valid_time index zero; full H1 remains separately gated"
	                    : "H1 public source/info validation failed or did not write its report; inspect the runner output";
	return result;
}

struct LocalCommand final {
	std::string id;
	std::vector<std::string> arguments;
	CommandResult result;
};

struct LocalGridChecks final {
	bool inputs_ready = false;
	bool selection_passed = false;
	bool parallel_passed = false;
	bool capacity_passed = false;
	bool memory_passed = false;
	bool memory_inputs_ready = false;
	bool preparation_cancel_passed = false;
	std::string reason;
	fs::path directory;
	fs::path core_functions;
	std::vector<LocalCommand> commands;
};

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		result.push_back(character);
		if (character == '\'') result.push_back('\'');
	}
	result.push_back('\'');
	return result;
}

fs::path FindCoreFunctions(const Options &options) {
	for (const auto &directory : {options.duckdb.parent_path() / "repository",
	                              options.duckdb.parent_path() / "extension"}) {
		if (!fs::is_directory(directory)) continue;
		for (const auto &entry : fs::recursive_directory_iterator(directory)) {
			if (entry.is_regular_file() && entry.path().filename() == "core_functions.duckdb_extension") {
				return fs::absolute(entry.path());
			}
		}
	}
	return {};
}

struct WorkPeakCase final {
	std::string id;
	std::string grid_type;
	std::string small_path;
	std::string large_path;
	std::uint64_t small_points;
	std::uint64_t large_points;
};

std::string GaussianRowsSql(bool large) {
	std::ostringstream rows;
	if (!large) {
		rows << "[{'latitude': 60.0, 'point_count': 128, 'longitude_origin': 0.0, 'longitude_step': 1.0},"
		        " {'latitude': -60.0, 'point_count': 128, 'longitude_origin': 0.0, 'longitude_step': 1.0}]";
		return rows.str();
	}
	static const std::array<int, 16> LATITUDES{{90, 80, 70, 60, 50, 40, 30, 20, 10, 0, -10, -20, -30, -40, -60, -90}};
	rows << "[";
	for (std::size_t index = 0; index < LATITUDES.size(); index++) {
		if (index) rows << ", ";
		const auto count = LATITUDES[index] == 60 ? 128 : 256;
		rows << "{'latitude': " << LATITUDES[index] << ".0, 'point_count': " << count
		     << ", 'longitude_origin': 0.0, 'longitude_step': 1.0}";
	}
	rows << "]";
	return rows.str();
}

std::string WorkPeakRead(const Options &options, const WorkPeakCase &work_case, bool large) {
	const auto relative_path = large ? work_case.large_path : work_case.small_path;
	const auto path = (options.root / "test/data/grids" / relative_path).string();
	if (work_case.grid_type == "reduced_gaussian") {
		return "read_om(" + SqlLiteral(path) + ", dimensions := map(['gaussian_work/value'], [['point']]), grid := "
		       "{'version':1,'type':'reduced_gaussian','numeric_policy':'float64_v1',"
		       "'earth':{'model':'wgs84','semi_major_m':6378137.0,'inverse_flattening':298.257223563},"
		       "'layout':{'order':'row_major'},'parameters':{'n':" + (large ? "8" : "1") +
		       ",'latitude_rule':'explicit_v1','rows':" + GaussianRowsSql(large) + ",'subset_segments':NULL}}, "
		       "spatial_axes := ['point'])";
	}
	const std::uint64_t side = large ? 1024 : 256;
	const auto origin = std::to_string(-static_cast<double>(side / 2) * 1000.0);
	const std::string layout = "'layout':{'nx':" + std::to_string(side) + ",'ny':" + std::to_string(side) +
	                          ",'order':'separate'},";
	std::string geometry;
	if (work_case.grid_type == "rotated_latlon") {
		const double spacing = large ? 0.125 : 0.25;
		const auto rotated_origin = large ? -64.0 : -32.0;
		geometry = "'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1',"
		           "'earth':{'model':'sphere','radius_m':6371229.0}," + layout +
		           "'parameters':{'x0':" + std::to_string(rotated_origin) +
		           ",'y0':" + std::to_string(rotated_origin) + ",'dx':" + std::to_string(spacing) +
		           ",'dy':" + std::to_string(spacing) +
		           ",'north_pole_latitude':90.0,'north_pole_longitude':0.0,'rotation':0.0}";
	} else if (work_case.grid_type == "lambert_conformal_conic") {
		geometry = "'version':1,'type':'lambert_conformal_conic','numeric_policy':'float64_v1',"
		           "'earth':{'model':'sphere','radius_m':6371229.0}," + layout +
		           "'parameters':{'x0':" + origin + ",'y0':" + origin +
		           ",'dx':1000.0,'dy':1000.0,'central_meridian':10.0,'latitude_of_origin':46.244,"
		           "'standard_parallel_1':40.0,'standard_parallel_2':50.0}";
	} else if (work_case.grid_type == "stereographic") {
		geometry = "'version':1,'type':'stereographic','numeric_policy':'float64_v1',"
		           "'earth':{'model':'sphere','radius_m':6371229.0}," + layout +
		           "'parameters':{'x0':" + origin + ",'y0':" + origin +
		           ",'dx':1000.0,'dy':1000.0,'central_meridian':10.0,'latitude_of_origin':45.0,'scale_factor':1.0}";
	} else {
		throw std::invalid_argument("unknown work-peak grid type " + work_case.grid_type);
	}
	return "read_om(" + SqlLiteral(path) + ", dimensions := map(['value'], [['latitude_axis','longitude_axis']]), "
	       "grid := {" + geometry + "}, spatial_axes := ['latitude_axis','longitude_axis'])";
}

std::string WorkPeakPredicate(const std::string &grid_type) {
	if (grid_type == "rotated_latlon") {
		return "latitude BETWEEN -0.000001 AND 0.000001 AND longitude BETWEEN -0.000001 AND 0.000001";
	}
	if (grid_type == "lambert_conformal_conic") {
		return "latitude BETWEEN 46.243 AND 46.245 AND longitude BETWEEN 9.999 AND 10.001";
	}
	if (grid_type == "stereographic") {
		return "latitude BETWEEN 44.999 AND 45.001 AND longitude BETWEEN 9.999 AND 10.001";
	}
	if (grid_type == "reduced_gaussian") return "latitude = 60 AND longitude BETWEEN 0 AND 2";
	throw std::invalid_argument("unknown work-peak predicate grid type " + grid_type);
}

void WriteWorkPeakSql(const Options &options, const WorkPeakCase &work_case, const fs::path &sql_path,
	                  const fs::path &small_metrics, const fs::path &large_metrics) {
	std::ofstream sql(sql_path, std::ios::binary | std::ios::trunc);
	if (!sql.good()) throw std::runtime_error("cannot create work-peak SQL script " + sql_path.string());
	sql << "LOAD " << SqlLiteral(options.extension.string()) << ";\n"
	       "SET threads=1;\n"
	       "SET duckomo_max_threads=1;\n"
	       ""
	       "";
	const auto predicate = WorkPeakPredicate(work_case.grid_type);
	for (const auto large : {false, true}) {
		const auto suffix = large ? "large" : "small";
		const auto table = "work_" + std::string(suffix);
		const auto metrics = large ? large_metrics : small_metrics;
		const std::string value_column = work_case.grid_type == "reduced_gaussian" ? "\"gaussian_work/value\"" : "value";
		sql << "CREATE TEMP TABLE " << table << " AS SELECT " << value_column << " AS value FROM "
		    << WorkPeakRead(options, work_case, large) << " WHERE " << predicate << ";\n\n"
		    << "COPY (SELECT (SELECT count(*)::UBIGINT FROM " << table
		    << ") AS result_rows, (SELECT sum(value)::DOUBLE FROM " << table
		    << ") AS result_sum, (SELECT min(value)::DOUBLE FROM " << table << ") AS result_min, "
		    << "(SELECT max(value)::DOUBLE FROM " << table
		    << ") AS result_max, metrics::JSON AS metrics FROM duckomo_last_scan_metrics() "
		       "ORDER BY scan_id DESC LIMIT 1) TO "
		    << SqlLiteral(metrics.string()) << " (FORMAT JSON, ARRAY false);\n\n";
	}
	if (!sql.good()) throw std::runtime_error("cannot write work-peak SQL script " + sql_path.string());
}

LocalGridChecks RunLocalGridChecks(const Options &options) {
	LocalGridChecks checks;
	checks.directory = options.output / "us2-local";
	checks.core_functions = FindCoreFunctions(options);
	const auto test_directory = options.duckdb.parent_path() / "test";
	const auto unittest = test_directory / "unittest";
	const auto grid_selection_native = test_directory / "native/grid_selection_test";
	const auto parallel_native = test_directory / "native/parallel_scan_test";
	const auto lifecycle_native = test_directory / "native/spatial_lifecycle_test";
	const auto capacity_native = test_directory / "native/reader_capacity_test";
	const std::array<fs::path, 10> required_paths{{
	    unittest, grid_selection_native, parallel_native, lifecycle_native, capacity_native,
	    options.root / "test/sql/grid_selection.test", options.root / "test/data/projection.om",
	    options.root / "test/data/grids/x-fastest.om", options.root / "test/data/grids/memory-small.om",
	    options.root / "test/data/grids/memory-large.om"}};
	for (const auto &path : required_paths) {
		if (!fs::is_regular_file(path)) {
			checks.reason = "US2 local checks require " + path.string();
			return checks;
		}
	}
	if (access(unittest.c_str(), X_OK) != 0 || access(grid_selection_native.c_str(), X_OK) != 0 ||
	    access(parallel_native.c_str(), X_OK) != 0 || access(lifecycle_native.c_str(), X_OK) != 0 ||
	    access(capacity_native.c_str(), X_OK) != 0) {
		checks.reason = "US2 local checks require executable matching native tests and test/unittest";
		return checks;
	}
	if (checks.core_functions.empty()) {
		checks.reason = "US2 local checks require core_functions from the same DuckDB build";
		return checks;
	}
	checks.inputs_ready = true;
	fs::create_directories(checks.directory);
	const auto run = [&](const std::string &id, const std::vector<std::string> &arguments,
	                     const std::map<std::string, std::string> &environment = {}) {
		LocalCommand command;
		command.id = id;
		command.arguments = arguments;
		command.result = RunProcess(arguments, options.root, environment);
		std::ofstream output(checks.directory / (id + ".stdout.txt"), std::ios::binary | std::ios::trunc);
		if (!output.good()) throw std::runtime_error("cannot write US2 local command output for " + id);
		output << command.result.output;
		if (!output.good()) throw std::runtime_error("cannot write US2 local command output for " + id);
		checks.commands.push_back(std::move(command));
		return checks.commands.back().result.exit_code == 0;
	};
	const auto unittest_path = unittest.string();
	const auto extension_environment = std::map<std::string, std::string>{
	    {"DUCKOMO_CORE_FUNCTIONS_EXTENSION", checks.core_functions.string()},
	    {"DUCKOMO_EXTENSION_PATH", options.extension.string()}};
	const auto selection_native_ok = run("grid-selection-native", {grid_selection_native.string()});
	const auto selection_sql_ok = run("grid-selection-sqllogictest",
	                                  {unittest_path, (options.root / "test/sql/grid_selection.test").string()});
	checks.selection_passed = selection_native_ok && selection_sql_ok;
	checks.preparation_cancel_passed = selection_native_ok;
	const auto parallel_ok = run("parallel-scan-native", {parallel_native.string()}, extension_environment);
	const auto lifecycle_ok = run("spatial-lifecycle-native", {lifecycle_native.string()}, extension_environment);
	checks.parallel_passed = parallel_ok && lifecycle_ok;
	checks.capacity_passed = run("reader-capacity-native", {capacity_native.string()});

	const std::array<WorkPeakCase, 4> work_cases{{
	    {"rotated_latlon", "rotated_latlon", "memory-small.om", "memory-large.om", 256ULL * 256ULL,
	     1024ULL * 1024ULL},
	    {"lambert_conformal_conic", "lambert_conformal_conic", "memory-small.om", "memory-large.om",
	     256ULL * 256ULL, 1024ULL * 1024ULL},
	    {"stereographic", "stereographic", "memory-small.om", "memory-large.om", 256ULL * 256ULL,
	     1024ULL * 1024ULL},
	    {"reduced_gaussian", "reduced_gaussian", "gaussian-work-small.om", "gaussian-work-large.om", 256, 3968}}};
	checks.memory_inputs_ready = fs::is_regular_file(options.root / "test/data/grids/gaussian-work-small.om") &&
	                            fs::is_regular_file(options.root / "test/data/grids/gaussian-work-large.om");
	checks.memory_passed = checks.memory_inputs_ready;
	const std::string fixture_manifest_filter =
	    ".fixtures as $f | "
	    "($f[] | select(.id == \"memory_small\")).variables[0] as $ps | "
	    "($f[] | select(.id == \"memory_large\")).variables[0] as $pl | "
	    "($f[] | select(.id == \"gaussian_work_small\")).variables[0] as $gs | "
	    "($f[] | select(.id == \"gaussian_work_large\")).variables[0] as $gl | "
	    "$ps.shape == [256,256] and $pl.shape == [1024,1024] and $ps.chunk_shape == [32,32] and "
	    "$pl.chunk_shape == $ps.chunk_shape and ($pl.shape[0] * $pl.shape[1]) >= "
	    "10 * ($ps.shape[0] * $ps.shape[1]) and $gs.shape == [256] and $gl.shape == [3968] and "
	    "$gs.chunk_shape == [32] and $gl.chunk_shape == $gs.chunk_shape and $gl.shape[0] >= 10 * $gs.shape[0]";
	const auto fixture_manifest_ok = checks.memory_inputs_ready
	                                     ? run("work-peak-fixture-manifest",
	                                           {"jq", "-er", fixture_manifest_filter, options.manifest.string()})
	                                     : false;
	for (const auto &work_case : work_cases) {
		if (!checks.memory_inputs_ready) break;
		if (work_case.large_points < 10 * work_case.small_points) {
			checks.memory_passed = false;
			continue;
		}
		const auto case_directory = checks.directory / ("work-peak-" + work_case.id);
		fs::create_directories(case_directory);
		const auto sql_path = case_directory / "work-peak.sql";
		const auto small_metrics = case_directory / "small.json";
		const auto large_metrics = case_directory / "large.json";
		WriteWorkPeakSql(options, work_case, sql_path, small_metrics, large_metrics);
		const auto sql_ok = run("work-peak-" + work_case.id,
		                        {options.duckdb.string(), "-unsigned", "-batch", "-csv", ":memory:", "-f",
		                         sql_path.string()});
		const auto jq_filter =
		    "def valid($r): $r.metrics.schema_version == 4 and $r.metrics.outcome.status == \"success\" and "
		    "$r.metrics.outcome.scan_complete == true and $r.metrics.selection.mode == \"restricted\" and "
		    "$r.metrics.selection.residual_filter_retained == true and "
		    "$r.metrics.coordinate_preparation.complete == true and "
		    "$r.metrics.memory.query_owned_complete == true and "
		    "($r.metrics.memory.query_owned_peak_bytes | type == \"number\") and "
		    "($r.metrics.memory.query_owned_upper_bound_peak_bytes | type == \"number\") and "
		    "$r.metrics.memory.query_owned_peak_bytes <= $r.metrics.memory.query_owned_upper_bound_peak_bytes and "
		    "$r.metrics.legacy_v3.cache.enabled == false and $r.metrics.legacy_v3.cache.capacity_bytes == 0 and "
		    "$r.metrics.legacy_v3.tasks.max_active_workers == 1 and "
		    "all($r.metrics.memory.components[]; (.peak_owned_capacity_bytes | type == \"number\") and "
		    "(.upper_bound_peak_bytes | type == \"number\") and .peak_owned_capacity_bytes <= .upper_bound_peak_bytes); "
		    "def work_peak($r): $r.metrics.memory.components as $m | "
		    "([\"global_control\",\"selector\",\"task_positions\",\"batch_segments\",\"decoder\","
		    "\"coalescing\",\"transport_control\"] | map($m[.].peak_owned_capacity_bytes) | add); "
		    "$small[0] as $s | $large[0] as $l | "
		    "if valid($s) and valid($l) and $large_points >= 10 * $small_points and "
		    "$s.result_rows == $l.result_rows and "
		    "$s.result_sum == $l.result_sum and $s.result_min == $l.result_min and $s.result_max == $l.result_max and "
		    "($l.result_rows | type == \"number\" and . > 0) and "
		    "$s.result_rows <= ($small_points / 10) and $l.result_rows <= ($large_points / 10) and "
		    "work_peak($l) <= 2 * ([1, work_peak($s)] | max) then "
		    "{small_rows:$s.result_rows,large_rows:$l.result_rows,result_sum:$l.result_sum,"
		    "result_min:$l.result_min,result_max:$l.result_max,small_work_peak_bytes:work_peak($s),"
		    "large_work_peak_bytes:work_peak($l),work_peak_ratio:(work_peak($l) / ([1,work_peak($s)] | max)),"
		    "small_components:$s.metrics.memory.components,large_components:$l.metrics.memory.components} "
		    "else error(\"missing/incomplete bound ledger, changed narrow result size, or work peak exceeded 2x\") end";
		const auto memory_validation = run(
		    "work-peak-" + work_case.id + "-metrics",
		    {"jq", "-n", "-er", "--slurpfile", "small", small_metrics.string(), "--slurpfile", "large",
	     large_metrics.string(), "--argjson", "small_points", std::to_string(work_case.small_points), "--argjson",
	     "large_points", std::to_string(work_case.large_points), jq_filter},
		    {});
		checks.memory_passed = checks.memory_passed && sql_ok && memory_validation &&
		                       fs::is_regular_file(small_metrics) && fs::is_regular_file(large_metrics);
	}
	checks.memory_passed = checks.memory_passed && fixture_manifest_ok;
	if (checks.selection_passed && checks.parallel_passed && checks.capacity_passed && checks.memory_passed) {
		checks.reason = "synthetic local selection, parallel/lifecycle, reader capacity, and four-grid work-peak checks passed";
	} else if (!checks.memory_inputs_ready && checks.selection_passed && checks.parallel_passed && checks.capacity_passed) {
		checks.reason = "synthetic H2/H4 and reader-capacity checks are ready; four-grid H5 work-peak inputs are not generated";
	} else {
		checks.reason = "one or more US2/H4/H5 local checks failed; see us2-local command outputs";
	}
	return checks;
}

struct GridRunSummary final {
	std::string status;
	int exit_code;
	bool any_passed;
};

GridRunSummary SummarizeGates(const std::vector<GridGateResult> &gates) {
	const bool any_failed = std::any_of(gates.begin(), gates.end(), [](const auto &gate) {
		return gate.status == "fail" || (gate.status != "pass" && gate.status != "not-run");
	});
	const bool any_not_run = std::any_of(gates.begin(), gates.end(), [](const auto &gate) {
		return gate.status == "not-run";
	});
	const bool any_passed = std::any_of(gates.begin(), gates.end(), [](const auto &gate) {
		return gate.status == "pass";
	});
	return {any_failed ? "fail" : any_not_run ? "not-run" : "pass",
	        any_failed ? 1 : any_not_run ? 2 : 0, any_passed};
}

std::string SnapshotPath(const fs::path &path, const fs::path &root) {
	const auto relative = path.lexically_relative(root);
	return relative.empty() ? path.string() : relative.generic_string();
}

int WriteRun(const Options &options, int argc, char **argv) {
	const auto cases = ParseGridValidationCases(options.cases);
	const auto missing = MissingInputs(options, cases);
	const auto local_missing = MissingInputs(options, {"H2"});
	if (fs::exists(options.output)) {
		if (!fs::is_directory(options.output)) throw std::runtime_error("--output must be a directory");
		if (!IsDirectoryEmpty(options.output)) throw std::runtime_error("--output must name an empty evidence directory");
	} else {
		fs::create_directories(options.output);
	}
	const auto root = fs::is_directory(options.root) ? options.root : fs::current_path();
	const auto write_file = [&](const fs::path &path, const std::string &contents) {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output.good()) throw std::runtime_error("cannot create evidence file " + path.string());
		output << contents;
		if (!output.good()) throw std::runtime_error("cannot write evidence file " + path.string());
	};
	const auto frozen_inputs = std::vector<std::pair<std::string, fs::path>>{
	    {"sample_manifest", options.manifest},
	    {"definitions", options.root / "test/data/grids/definitions.json"},
	    {"source_manifest", options.root / "test/data/grids/source-manifest.json"},
	    {"coordinate_reference", options.root / "test/data/grids/coordinate-reference.json"},
	    {"region_reference", options.root / "test/data/grids/region-reference.json"},
	    {"value_reference", options.root / "test/data/grids/value-reference-manifest.json"},
	    {"canonical_vectors", options.root / "test/data/grids/canonical-vectors.json"},
	    {"spatial_relations", options.root / "test/data/grids/spatial-relations.json"},
	    {"sample_queries", options.root / "test/data/grids/sample-queries.sql"},
	    {"version_matrix", options.matrix}};
	const auto snapshot_path = options.output / "frozen-input-snapshot.json";
	bool snapshot_complete = true;
	std::ostringstream snapshot;
	snapshot << "{\n  \"schema_version\":1,\n  \"files\":[";
	for (std::size_t index = 0; index < frozen_inputs.size(); index++) {
		const auto &[name, path] = frozen_inputs[index];
		const auto digest = Sha256(path, root);
		if (!digest) snapshot_complete = false;
		if (index) snapshot << ",";
		snapshot << "\n    {\"name\":" << JsonString(name) << ",\"path\":"
		         << JsonString(SnapshotPath(path, root)) << ",\"sha256\":"
		         << (digest ? JsonString(*digest) : "null") << "}";
	}
	snapshot << "\n  ],\n  \"complete\":" << (snapshot_complete ? "true" : "false") << "\n}\n";
	write_file(snapshot_path, snapshot.str());
	const auto frozen_input_set_sha256 = snapshot_complete ? Sha256(snapshot_path, root) : std::nullopt;
	std::vector<GridGateResult> gates;
	std::optional<H0QueryGeneration> h0_query_generation;
	std::optional<H1ReferenceChecks> h1_reference_checks;
	std::optional<H1PublicSourceChecks> h1_public_source_checks;
	std::optional<H3LocalChecks> h3_local;
	std::optional<H6LocalChecks> h6_local;
	std::optional<LocalGridChecks> local_grid;
	std::optional<H7LocalChecks> h7_local;
	std::optional<H8MatrixChecks> h8_matrix;
	if (std::find(cases.begin(), cases.end(), "H0") != cases.end()) {
		h0_query_generation = RunH0QueryGeneration(options);
		const auto evidence_directory = options.output / "h0-query-regeneration";
		fs::create_directories(evidence_directory);
		if (h0_query_generation->inputs_ready) {
			write_file(evidence_directory / "first.stdout.txt", h0_query_generation->first_result.output);
			write_file(evidence_directory / "second.stdout.txt", h0_query_generation->second_result.output);
			write_file(evidence_directory / "check.stdout.txt", h0_query_generation->check_result.output);
			if (h0_query_generation->binding_ran) {
				write_file(evidence_directory / "binding.stdout.txt", h0_query_generation->binding_result.output);
			}
		}
		std::ostringstream h0_manifest;
		h0_manifest << "{\n  \"schema_version\":1,\n  \"gate\":\"H0\",\n"
		            << "  \"query_generation_status\":"
		            << JsonString(!h0_query_generation->inputs_ready ? "not-run" :
		                          h0_query_generation->generation_passed ? "pass" : "fail")
		            << ",\n  \"sample_view_binding_status\":"
		            << JsonString(!h0_query_generation->binding_ran ? "not-run" :
		                          h0_query_generation->binding_result.exit_code == 0 ? "pass" : "fail")
		            << ",\n  \"full_gate_status\":\"not-run\",\n"
		            << "  \"reason\":" << JsonString(h0_query_generation->reason) << ",\n"
		            << "  \"sample_manifest_sha256\":"
		            << JsonString(Sha256(options.manifest, root).value_or("")) << ",\n"
		            << "  \"inputs\":{" << FileRecord("generator", h0_query_generation->generator, root) << ","
		            << FileRecord("definitions", h0_query_generation->definitions, root) << ","
		            << FileRecord("canonical_vectors", h0_query_generation->vectors, root) << ","
		            << FileRecord("sample_manifest", options.manifest, root) << ","
		            << FileRecord("sample_view_binding_script", h0_query_generation->binding_script, root) << ","
		            << FileRecord("checked_in_registry", h0_query_generation->checked_header, root) << ","
		            << FileRecord("checked_in_sample_queries", h0_query_generation->checked_sql, root) << "},\n"
		            << "  \"outputs\":{";
		if (h0_query_generation->inputs_ready) {
			h0_manifest << FileRecord("first_queries", h0_query_generation->first_sql, root) << ","
			            << FileRecord("second_queries", h0_query_generation->second_sql, root) << ","
			            << FileRecord("first_header", h0_query_generation->first_header, root) << ","
			            << FileRecord("second_header", h0_query_generation->second_header, root) << ","
			            << FileRecord("first_stdout", evidence_directory / "first.stdout.txt", root) << ","
			            << FileRecord("second_stdout", evidence_directory / "second.stdout.txt", root) << ","
			            << FileRecord("check_stdout", evidence_directory / "check.stdout.txt", root) << ","
			            << FileRecord("binding_stdout", evidence_directory / "binding.stdout.txt", root) << "},\n"
			            << "  \"query_hashes\":{\"first\":"
			            << (h0_query_generation->first_hash ? JsonString(*h0_query_generation->first_hash) : "null")
			            << ",\"second\":"
			            << (h0_query_generation->second_hash ? JsonString(*h0_query_generation->second_hash) : "null")
			            << ",\"checked_in\":"
			            << (h0_query_generation->checked_hash ? JsonString(*h0_query_generation->checked_hash) : "null")
			            << "},\n  \"commands\":{\"first_generation\":"
			            << CommandRecord(h0_query_generation->first_arguments, h0_query_generation->first_result)
			            << ",\"second_generation\":"
			            << CommandRecord(h0_query_generation->second_arguments, h0_query_generation->second_result)
			            << ",\"checked_in_audit\":"
			            << CommandRecord(h0_query_generation->check_arguments, h0_query_generation->check_result)
			            << ",\"sample_view_binding\":"
			            << CommandRecord(h0_query_generation->binding_arguments, h0_query_generation->binding_result)
			            << "}\n}\n";
		} else {
			h0_manifest << "},\n  \"query_hashes\":null,\n  \"commands\":{}\n}\n";
		}
		write_file(evidence_directory / "manifest.json", h0_manifest.str());
		write_file(evidence_directory / "final.md",
		           "# H0 registry/query regeneration audit\n\nQuery generation: **" +
		               std::string(!h0_query_generation->inputs_ready ? "not-run" :
		                           h0_query_generation->generation_passed ? "pass" : "fail") +
		               "**.\n\nGenerated sample-view binding: **" +
		               std::string(!h0_query_generation->binding_ran ? "not-run" :
		                           h0_query_generation->binding_result.exit_code == 0 ? "pass" : "fail") +
		               "**.\n\nFull H0 coordinate/value oracle: **not-run**. The frozen input currently lacks required "
		               "N160/N320/N320-region producer objects and independent coordinate references. ECMWF HRES O1280 "
		               "is retained as supplemental value-only evidence.\n\nReason: " + h0_query_generation->reason + "\n");
	}
	if (std::find(cases.begin(), cases.end(), "H1") != cases.end() && missing.empty()) {
		h1_reference_checks = RunH1ReferenceChecks(options);
		h1_public_source_checks = RunH1PublicSourceChecks(options);
		const auto evidence_directory = options.output / "h1-reference-checks";
		fs::create_directories(evidence_directory);
		if (h1_reference_checks->executed) {
			write_file(h1_reference_checks->stdout_file, h1_reference_checks->result.output);
		}
		if (h1_public_source_checks->executed) {
			write_file(h1_public_source_checks->stdout_file, h1_public_source_checks->result.output);
		}
		std::ostringstream public_source_manifest;
		public_source_manifest << "{\n  \"schema_version\":1,\n  \"gate\":\"H1/T070 public source-info subcheck\",\n"
		                       << "  \"status\":"
		                       << JsonString(!h1_public_source_checks->inputs_ready ? "not-run" :
		                                     h1_public_source_checks->passed ? "pass" : "fail")
		                       << ",\n  \"full_h1_status\":\"not-run\",\n  \"reason\":"
		                       << JsonString(h1_public_source_checks->reason) << ",\n  \"inputs\":{"
		                       << FileRecord("validator", h1_public_source_checks->validator, root) << ","
		                       << FileRecord("sample_manifest", options.manifest, root) << ","
		                       << FileRecord("sample_queries", options.root / "test/data/grids/sample-queries.sql", root)
		                       << ","
		                       << FileRecord("coordinate_reference", options.root / "test/data/grids/coordinate-reference.json", root)
		                       << ","
		                       << FileRecord("duckdb", options.duckdb, root, true) << ","
		                       << FileRecord("extension", options.extension, root) << "},\n  \"outputs\":{"
		                       << FileRecord("report", h1_public_source_checks->report, root) << ","
		                       << FileRecord("runner_stdout", h1_public_source_checks->stdout_file, root) << "},\n"
		                       << "  \"command\":"
		                       << CommandRecord(h1_public_source_checks->arguments, h1_public_source_checks->result)
		                       << "\n}\n";
		write_file(h1_public_source_checks->directory / "manifest.json", public_source_manifest.str());
		std::ostringstream h1_manifest;
		h1_manifest << "{\n  \"schema_version\":1,\n  \"gate\":\"H1\",\n"
		            << "  \"partial_reference_status\":"
		            << JsonString(!h1_reference_checks->executed ? "not-run" :
		                          h1_reference_checks->partial_passed ? "pass" :
		                          h1_reference_checks->result.exit_code == 2 ? "not-run" : "fail")
		            << ",\n  \"full_gate_status\":\"not-run\",\n  \"reason\":"
		            << JsonString(h1_reference_checks->reason) << ",\n  \"inputs\":{"
		            << FileRecord("comparator", h1_reference_checks->comparator, root) << ","
		            << FileRecord("sample_manifest", options.manifest, root) << ","
		            << FileRecord("coordinate_reference", h1_reference_checks->coordinate_reference, root) << ","
		            << FileRecord("value_reference", h1_reference_checks->value_reference, root) << ","
		            << FileRecord("duckdb", options.duckdb, root, true) << ","
		            << FileRecord("extension", options.extension, root) << "},\n  \"outputs\":{"
		            << FileRecord("comparison_summary", h1_reference_checks->summary, root) << ","
		            << FileRecord("runner_stdout", h1_reference_checks->stdout_file, root) << "},\n"
		            << "  \"command\":"
		            << CommandRecord(h1_reference_checks->arguments, h1_reference_checks->result) << ",\n"
		            << "  \"public_source_subcheck\":{\"manifest\":"
		            << ArtifactRecord(h1_public_source_checks->directory / "manifest.json", root)
		            << ",\"report\":" << ArtifactRecord(h1_public_source_checks->report, root)
		            << ",\"command\":"
		            << CommandRecord(h1_public_source_checks->arguments, h1_public_source_checks->result) << "}\n}\n";
		write_file(evidence_directory / "manifest.json", h1_manifest.str());
		write_file(evidence_directory / "final.md",
		           "# H1 independent coordinate/value subchecks\n\nFull H1: **not-run**.\n\n"
			       "Available sample comparison: **" +
	               std::string(h1_reference_checks->partial_passed ? "pass" : "not-run/fail") +
	               "**.\n\nPublic source/info subcheck: **" +
	               std::string(!h1_public_source_checks->inputs_ready ? "not-run" :
	                           h1_public_source_checks->passed ? "pass" : "fail") +
	               "**.\n\nReference comparison reason: " + h1_reference_checks->reason +
	               "\n\nPublic source/info reason: " + h1_public_source_checks->reason + "\n");
	}
	const bool needs_local_grid = std::any_of(cases.begin(), cases.end(), [](const auto &id) {
		return id == "H2" || id == "H4" || id == "H5";
	});
	if (needs_local_grid && local_missing.empty()) {
		local_grid = RunLocalGridChecks(options);
		const auto evidence_directory = options.output / "us2-local";
		fs::create_directories(evidence_directory);
		std::ostringstream local_manifest;
		local_manifest << "{\n  \"schema_version\":1,\n  \"scope\":\"synthetic_local_only\",\n"
		               << "  \"full_gate_status\":\"not-run\",\n  \"inputs_ready\":"
		               << (local_grid->inputs_ready ? "true" : "false") << ",\n  \"reason\":"
		               << JsonString(local_grid->reason) << ",\n  \"local_checks\":{\n"
		               << "    \"H2_materialized_selection\":"
		               << JsonString(local_grid->inputs_ready ? local_grid->selection_passed ? "pass" : "fail" : "not-run")
		               << ",\n    \"H4_parallel_lifecycle\":"
		               << JsonString(local_grid->inputs_ready ? local_grid->parallel_passed ? "pass" : "fail" : "not-run")
		               << ",\n    \"H5_reader_capacity\":"
		               << JsonString(local_grid->inputs_ready ? local_grid->capacity_passed ? "pass" : "fail" : "not-run")
			               << ",\n    \"H5_four_grid_work_peak\":"
		               << JsonString(local_grid->inputs_ready
	                                   ? local_grid->memory_inputs_ready ? local_grid->memory_passed ? "pass" : "fail"
	                                                                    : "not-run"
	                                   : "not-run")
			               << ",\n    \"H5_preparation_cancellation\":"
			               << JsonString(local_grid->inputs_ready ? local_grid->preparation_cancel_passed ? "pass" : "fail" : "not-run")
			               << "\n  },\n  \"core_functions\":"
		               << (local_grid->core_functions.empty() ? "null" : JsonString(local_grid->core_functions.string()))
		               << ",\n  \"commands\":[";
		for (std::size_t index = 0; index < local_grid->commands.size(); index++) {
			if (index) local_manifest << ",";
			local_manifest << "\n    {\"id\":" << JsonString(local_grid->commands[index].id)
			               << ",\"record\":"
			               << CommandRecord(local_grid->commands[index].arguments, local_grid->commands[index].result)
			               << ",\"stdout\":"
			               << JsonString("us2-local/" + local_grid->commands[index].id + ".stdout.txt") << "}";
		}
		if (!local_grid->commands.empty()) local_manifest << "\n  ";
		local_manifest << "],\n  \"inputs\":{" << FileRecord("sample_manifest", options.manifest, root) << ","
		               << FileRecord("selection_sql", options.root / "test/sql/grid_selection.test", root) << ","
		               << FileRecord("memory_small_projected", options.root / "test/data/grids/memory-small.om", root) << ","
		               << FileRecord("memory_large_projected", options.root / "test/data/grids/memory-large.om", root) << ","
		               << FileRecord("memory_small_gaussian", options.root / "test/data/grids/gaussian-work-small.om", root)
		               << ","
		               << FileRecord("memory_large_gaussian", options.root / "test/data/grids/gaussian-work-large.om", root)
		               << "},\n  \"work_peak_contract\":{\"minimum_spatial_point_growth\":10,"
		                  "\"maximum_work_peak_ratio\":2,\"threads\":1,\"cache_enabled\":false,"
		                  "\"cache_capacity_bytes\":0,\"compared_components\":["
		                  "\"global_control\",\"selector\",\"task_positions\",\"batch_segments\","
		                  "\"decoder\",\"coalescing\",\"transport_control\"],"
		                  "\"reported_separately\":[\"definitions\",\"coordinate_buffers\",\"shared_cache\"]}}\n";
		write_file(evidence_directory / "manifest.json", local_manifest.str());
		write_file(evidence_directory / "final.md",
		           "# Local grid implementation checks\n\nScope: synthetic local checks only.\n\n"
		           "The complete H2/H4/H5 gates remain not-run until their real-sample and remote subcases are recorded.\n\n"
		           "Reason: " + local_grid->reason + "\n");
	}
	if (std::find(cases.begin(), cases.end(), "H6") != cases.end()) {
		h6_local = RunH6LocalChecks(options);
		const auto evidence_directory = options.output / "h6-local";
		fs::create_directories(evidence_directory);
		std::ostringstream local_manifest;
		local_manifest << "{\n  \"schema_version\":1,\n  \"gate\":\"H6\",\n"
		               << "  \"local_synthetic_status\":"
		               << JsonString(!h6_local->executed ? "not-run" : h6_local->passed ? "pass" : "fail")
		               << ",\n  \"full_gate_status\":\"not-run\",\n  \"reason\":"
		               << JsonString(h6_local->reason) << ",\n  \"remote_inputs\":{\"http_base_provided\":"
		               << (!options.http_base.empty() ? "true" : "false") << ",\"https_base_provided\":"
		               << (!options.https_base.empty() ? "true" : "false") << ",\"s3_base_provided\":"
		               << (!options.s3_base.empty() ? "true" : "false") << ",\"s3_setup_exists\":"
		               << (fs::is_regular_file(options.s3_setup) ? "true" : "false")
		               << ",\"server_audit_logs_valid\":"
		               << (HasRemoteAuditLogs(options.server_log) ? "true" : "false") << "},\n"
		                  "  \"full_gate_blockers\":[\"controlled HTTPS and signed-S3 H6 runs are not recorded\","
		                  "\"required N160/N320/N320-region producer objects and Gaussian region offsets are missing\","
		                  "\"003 G3/G5/G6, complete G4, and independent G7 prerequisites remain open\"],\n"
		               << "  \"inputs\":{" << FileRecord("sample_manifest", options.manifest, root) << ","
		               << FileRecord("remote_session_test", h6_local->remote_session_test, root, true) << ","
		               << FileRecord("raw_fixture", options.root / "test/data/raw.om", root) << ","
		               << FileRecord("replacement_fixture", options.root / "test/data/dimensions_axis_order.om", root)
	               << ","
		               << FileRecord("interleaved_fixture", options.root / "test/data/grids/interleaved.om", root)
		               << "},\n  \"outputs\":{";
		for (const auto *name : {"source-position-comparison.json", "source-position-local.csv",
		                         "source-position-http.csv", "source-position-local.metrics.json",
		                         "source-position-http.metrics.json", "attempt-stress.json",
		                         "cancelled-query.metrics.json", "cancelled-query-release.json", "protocol-cache.json",
		                         "remote-session.stdout.txt"}) {
			if (name != std::string("source-position-comparison.json")) local_manifest << ",";
			local_manifest << FileRecord(name, evidence_directory / name, root);
		}
		local_manifest << "},\n  \"command\":"
		               << (h6_local->executed
	                       ? CommandRecord({h6_local->remote_session_test.string()}, h6_local->result)
	                       : "null")
		               << "\n}\n";
		write_file(evidence_directory / "manifest.json", local_manifest.str());
		write_file(evidence_directory / "final.md",
		           "# H6 local and loopback subchecks\n\nSynthetic local/HTTP checks: **" +
		               std::string(!h6_local->executed ? "not-run" : h6_local->passed ? "pass" : "fail") +
		               "**. Full H6: **not-run**.\n\n" + h6_local->reason +
		               "\n\nFull H6 requires controlled HTTPS/S3, producer-backed Gaussian offsets, and the 003 remote prerequisites.\n");
	}
	if (std::find(cases.begin(), cases.end(), "H7") != cases.end() && missing.empty()) {
		h7_local = RunH7LocalChecks(options);
		const auto evidence_directory = options.output / "h7-local";
		fs::create_directories(evidence_directory);
		if (h7_local->executed) write_file(h7_local->stdout_file, h7_local->result.output);
		std::ostringstream local_manifest;
		local_manifest << "{\n  \"schema_version\":1,\n  \"gate\":\"H7\",\n"
		               << "  \"local_synthetic_status\":"
		               << JsonString(!h7_local->inputs_ready ? "not-run" : h7_local->passed ? "pass" : "fail")
		               << ",\n  \"full_gate_status\":\"not-run\",\n  \"reason\":"
		               << JsonString(h7_local->reason) << ",\n  \"inputs\":{"
		               << FileRecord("validator", h7_local->validator, root) << ","
		               << FileRecord("independent_spatial_oracle", h7_local->oracle, root) << ","
		               << FileRecord("projected_fixture", h7_local->projected_fixture, root) << ","
		               << FileRecord("gaussian_fixture", h7_local->gaussian_fixture, root) << ","
		               << FileRecord("duckdb", options.duckdb, root, true) << ","
		               << FileRecord("extension", options.extension, root) << ","
		               << FileRecord("sample_manifest", h7_local->sample_manifest, root) << ","
		               << FileRecord("source_manifest", h7_local->source_manifest, root) << ","
		               << FileRecord("coordinate_reference", h7_local->coordinate_reference, root)
		               << "},\n  \"outputs\":{";
		if (h7_local->inputs_ready) {
			local_manifest << FileRecord("synthetic_report", h7_local->summary, root) << ","
			               << FileRecord("query_sql", h7_local->query_directory / "h7-synthetic.sql", root) << ","
			               << FileRecord("runner_stdout", h7_local->stdout_file, root) << ","
			               << FileRecord("duckdb_stdout", h7_local->query_directory / "runner.stdout.txt", root) << ","
			               << FileRecord("duckdb_stderr", h7_local->query_directory / "runner.stderr.txt", root);
			for (const auto *case_id : {"rotated_latlon_small", "lambert_small", "stereographic_small",
			                            "reduced_gaussian_small"}) {
				const std::string prefix = case_id;
				for (const auto *suffix : {"source.csv", "source-metrics.csv", "info.csv", "info-metrics.csv"}) {
					local_manifest << "," << FileRecord(prefix + "_" + suffix,
					                                      h7_local->query_directory / (prefix + "." + suffix), root);
				}
			}
			for (const auto *sample_id : {"gem_rdps_10km", "gem_regional", "aladin_central_europe_2km"}) {
				for (const auto *suffix : {"explicit-source.csv", "explicit-source-metrics.csv", "domain-source.csv",
				                           "domain-source-metrics.csv", "explicit-info.csv", "explicit-info-metrics.csv",
				                           "domain-info.csv", "domain-info-metrics.csv"}) {
					local_manifest << "," << FileRecord(std::string(sample_id) + "_" + suffix,
					                                      h7_local->query_directory / (std::string(sample_id) + "." + suffix),
					                                      root);
				}
			}
		}
		local_manifest << "},\n  \"command\":"
		               << CommandRecord(h7_local->arguments, h7_local->result) << "\n}\n";
		write_file(evidence_directory / "manifest.json", local_manifest.str());
		write_file(evidence_directory / "final.md",
		           "# H7 source identity and spatial relation checks\n\nSynthetic local checks: **" +
		               std::string(!h7_local->inputs_ready ? "not-run" : h7_local->passed ? "pass" : "fail") +
		               "**. Full H7: **not-run**.\n\n" + h7_local->reason + "\n");
	}
	if (std::find(cases.begin(), cases.end(), "H8") != cases.end() &&
	    MissingInputs(options, {"H8"}).empty()) {
		h8_matrix = RunH8MatrixChecks(options);
		if (h8_matrix->executed) write_file(h8_matrix->stdout_file, h8_matrix->result.output);
	}
	for (const auto &id : cases) {
		if (id == "H0") {
			if (!h0_query_generation || !h0_query_generation->inputs_ready) {
				gates.push_back({id, "not-run", h0_query_generation ? h0_query_generation->reason :
				                 "H0 query generation was not scheduled"});
			} else if (!h0_query_generation->passed) {
				gates.push_back({id, "fail", h0_query_generation->reason});
			} else {
				gates.push_back({id, "not-run", "query generation and sample-view binding passed; independent "
				                      "coordinate/value H0 evidence is missing"});
			}
			continue;
		}
		if (id == "H1") {
			if (!h1_reference_checks || !h1_reference_checks->inputs_ready ||
			    !h1_reference_checks->executed || h1_reference_checks->result.exit_code == 2) {
				gates.push_back({id, "not-run", h1_reference_checks ? h1_reference_checks->reason :
				                 "H1 reference comparison was not scheduled because validation inputs are missing"});
			} else if (!h1_reference_checks->partial_passed) {
				gates.push_back({id, "fail", h1_reference_checks->reason});
			} else {
				gates.push_back({id, "not-run", h1_reference_checks->reason});
			}
			continue;
		}
		if (id == "H2" || id == "H4" || id == "H5") {
			if (!local_missing.empty()) {
				gates.push_back({id, "not-run", "validation inputs are missing"});
				continue;
			}
			if (!local_grid || !local_grid->inputs_ready) {
				gates.push_back({id, "not-run", local_grid ? local_grid->reason : "local checks did not run"});
				continue;
			}
			const bool local_passed = id == "H2" ? local_grid->selection_passed
			                         : id == "H4" ? local_grid->parallel_passed
			                         : local_grid->capacity_passed && local_grid->memory_passed &&
			                               local_grid->preparation_cancel_passed;
			if (id == "H5" && !local_grid->memory_inputs_ready) {
				gates.push_back({id, "not-run", "H5 local capacity ran, but the four-grid work-peak fixtures are not generated"});
				continue;
			}
			if (!local_passed) {
				gates.push_back({id, "fail", "a requested synthetic local subcheck failed; see us2-local evidence"});
			} else if (id == "H5") {
				gates.push_back({id, "not-run", "H5 local capacity and four-grid work-peak checks passed; controlled baseline-remote H5 inputs and server audit required by T055 are absent"});
			} else {
				gates.push_back({id, "not-run", "synthetic local subchecks passed; full gate still requires its independent and real-sample evidence"});
			}
			continue;
		}
		if (id == "H3" && missing.empty()) {
			h3_local = RunH3LocalChecks(options);
			if (h3_local->executed) {
				const auto evidence_directory = options.output / "h3-local";
				fs::create_directories(evidence_directory);
				write_file(evidence_directory / "native.stdout.txt", h3_local->native_result.output);
				write_file(evidence_directory / "sqllogictest.stdout.txt", h3_local->sql_result.output);
				write_file(evidence_directory / "source-identity-native.stdout.txt",
				           h3_local->source_native_result.output);
				write_file(evidence_directory / "source-identity-sqllogictest.stdout.txt",
				           h3_local->source_sql_result.output);
				write_file(evidence_directory / "grid-info-sqllogictest.stdout.txt",
				           h3_local->grid_info_sql_result.output);
				write_file(evidence_directory / "public-source.stdout.txt", h3_local->public_source_result.output);
				write_file(evidence_directory / "metrics-validation.stdout.txt", h3_local->metrics_validation.output);
				const auto manifest_hash = Sha256(options.manifest, root);
				const auto native_command = CommandRecord({h3_local->native_test.string()}, h3_local->native_result);
				const auto sql_command = CommandRecord(
				    {(options.duckdb.parent_path() / "test/unittest").string(), h3_local->sql_test.string()},
				    h3_local->sql_result);
				const auto source_native_command =
				    CommandRecord({h3_local->source_native_test.string()}, h3_local->source_native_result);
				const auto source_sql_command = CommandRecord(
				    {(options.duckdb.parent_path() / "test/unittest").string(), h3_local->source_sql_test.string()},
				    h3_local->source_sql_result);
				const auto grid_info_sql_command = CommandRecord(
				    {(options.duckdb.parent_path() / "test/unittest").string(), h3_local->grid_info_sql_test.string()},
				    h3_local->grid_info_sql_result);
				const auto metrics_command = CommandRecord(
				    {"jq", "-er", h3_local->metrics_assertion, h3_local->metrics_file.string()},
				    h3_local->metrics_validation);
				const auto public_source_command =
				    CommandRecord(h3_local->public_source_arguments, h3_local->public_source_result);
				const auto native_input = FileRecord("zero_io_native", h3_local->native_test, root, true);
				const auto sql_input = FileRecord("zero_io_sql", h3_local->sql_test, root);
				const auto source_native_input = FileRecord("source_identity_native", h3_local->source_native_test,
				                                             root, true);
				const auto source_sql_input = FileRecord("source_identity_sql", h3_local->source_sql_test, root);
				const auto grid_info_sql_input = FileRecord("grid_info_sql", h3_local->grid_info_sql_test, root);
				const auto unittest_input = FileRecord("unittest", options.duckdb.parent_path() / "test/unittest", root,
			                                       true);
				const auto fixture_input = FileRecord("raw_fixture", options.root / "test/data/raw.om", root);
				const auto sample_manifest_input = FileRecord("sample_manifest", options.manifest, root);
				const auto core_functions_input = FileRecord("core_functions", h3_local->core_functions, root);
				const auto public_source_validator_input =
				    FileRecord("public_source_validator", h3_local->public_source_validator, root);
				const auto source_manifest_input =
				    FileRecord("source_manifest", options.root / "test/data/grids/source-manifest.json", root);
				const auto coordinate_reference_input =
				    FileRecord("coordinate_reference", options.root / "test/data/grids/coordinate-reference.json", root);
				const auto duckdb_cli_input = FileRecord("duckdb_cli", options.duckdb, root, true);
				const auto extension_input = FileRecord("duckomo_extension", options.extension, root);
				const auto metrics_output = FileRecord("zero_io_metrics", h3_local->metrics_file, root);
				const auto public_source_report_output =
				    FileRecord("public_source_report", h3_local->public_source_report, root);
				const auto public_source_sql_output =
				    FileRecord("public_source_sql", h3_local->public_source_directory / "public-source.sql", root);
				const auto public_source_stdout_output =
				    FileRecord("public_source_stdout", h3_local->public_source_directory / "runner.stdout.txt", root);
				const auto public_source_stderr_output =
				    FileRecord("public_source_stderr", h3_local->public_source_directory / "runner.stderr.txt", root);
				const auto public_source_time_probe_sql_output =
				    FileRecord("public_source_time_probe_sql",
				               h3_local->public_source_directory / "public-source-time-probe.sql", root);
				const auto public_source_time_probe_stdout_output =
				    FileRecord("public_source_time_probe_stdout",
				               h3_local->public_source_directory / "time-probe.stdout.txt", root);
				const auto public_source_time_probe_stderr_output =
				    FileRecord("public_source_time_probe_stderr",
				               h3_local->public_source_directory / "time-probe.stderr.txt", root);
				const auto public_source_command_stdout =
				    FileRecord("public_source_command_stdout", evidence_directory / "public-source.stdout.txt", root);
				const auto native_stdout = FileRecord("zero_io_native_stdout", evidence_directory / "native.stdout.txt", root);
				const auto sql_stdout = FileRecord("zero_io_sql_stdout", evidence_directory / "sqllogictest.stdout.txt", root);
				const auto source_native_stdout = FileRecord("source_identity_native_stdout",
				                                             evidence_directory / "source-identity-native.stdout.txt", root);
				const auto source_sql_stdout = FileRecord("source_identity_sql_stdout",
				                                         evidence_directory / "source-identity-sqllogictest.stdout.txt", root);
				const auto grid_info_sql_stdout = FileRecord("grid_info_sql_stdout",
				                                            evidence_directory / "grid-info-sqllogictest.stdout.txt", root);
				const auto metrics_stdout = FileRecord("metrics_validation_stdout",
				                                      evidence_directory / "metrics-validation.stdout.txt", root);
				std::ostringstream local_manifest;
				local_manifest << "{\n  \"schema_version\":1,\n  \"gate\":\"H3\",\n"
				               << "  \"local_synthetic_status\":" << JsonString(h3_local->passed ? "pass" : "fail")
				               << ",\n  \"full_gate_status\":\"not-run\",\n"
				               << "  \"reason\":" << JsonString(h3_local->reason) << ",\n"
				               << "  \"sample_manifest_sha256\":" << JsonString(manifest_hash.value_or("")) << ",\n"
				               << "  \"coverage\":[\"rotated_latlon\",\"lambert_conformal_conic\","
				               << "\"stereographic\",\"reduced_gaussian\",\"interleaved_non_spatial_axes\","
				               << "\"large_value_chunk\",\"three_public_projected_samples\","
				               << "\"bounded_public_spatial_selection_vs_unfiltered_full_plane\"],\n"
				               << "  \"source_info_cases_included\":true,\n"
				               << "  \"source_info_scope\":\"synthetic_local_and_all_spatial_positions_plus_bounded_spatial_subsets_at_valid_time_index_zero_for_three_hash_pinned_public_projected_samples\",\n"
				               << "  \"metrics_file\":\"h3-local/zero-io-metrics.json\",\n"
				               << "  \"inputs\":{" << native_input << "," << sql_input << ","
				               << source_native_input << "," << source_sql_input << "," << grid_info_sql_input << ","
				               << unittest_input << ","
				               << fixture_input << "," << sample_manifest_input << "," << core_functions_input << ","
				               << public_source_validator_input << "," << source_manifest_input << ","
				               << coordinate_reference_input << "," << duckdb_cli_input << "," << extension_input << "},\n"
				               << "  \"outputs\":{" << metrics_output << "," << native_stdout << "," << sql_stdout << ","
				               << source_native_stdout << "," << source_sql_stdout << "," << grid_info_sql_stdout << ","
				               << metrics_stdout << "," << public_source_report_output << "," << public_source_sql_output << ","
				               << public_source_stdout_output << "," << public_source_stderr_output << ","
				               << public_source_command_stdout << "," << public_source_time_probe_sql_output << ","
				               << public_source_time_probe_stdout_output << "," << public_source_time_probe_stderr_output;
				for (const auto *sample_id : {"gem_rdps_10km", "gem_regional", "aladin_central_europe_2km"}) {
					for (const auto *suffix : {"explicit-source.csv", "explicit-source-metrics.csv",
										           "domain-source.csv", "domain-source-metrics.csv", "explicit-info.csv",
										           "explicit-info-metrics.csv", "domain-info.csv", "domain-info-metrics.csv",
										           "source-identity.csv", "source-identity-metrics.csv", "first-valid-time.csv",
										           "first-valid-time-metrics.csv", "explicit-full-source.csv.gz",
										           "explicit-full-source-metrics.csv", "domain-full-source.csv.gz",
										           "domain-full-source-metrics.csv", "explicit-spatial-source.csv",
										           "explicit-spatial-source-metrics.csv", "domain-spatial-source.csv",
										           "domain-spatial-source-metrics.csv", "spatial-source-identity.csv",
										           "spatial-source-identity-metrics.csv"}) {
						local_manifest << "," << FileRecord(
						    std::string("public_") + sample_id + "_" + suffix,
						    h3_local->public_source_directory / (std::string(sample_id) + "." + suffix), root);
					}
				}
				local_manifest << "},\n  \"commands\":{\"native\":" << native_command << ",\"sql\":"
				               << sql_command << ",\"source_identity_native\":" << source_native_command
				               << ",\"source_identity_sql\":" << source_sql_command
				               << ",\"grid_info_sql\":" << grid_info_sql_command
				               << ",\"metrics_validation\":" << metrics_command
				               << ",\"public_source_validation\":" << public_source_command << "}\n}\n";
				write_file(evidence_directory / "manifest.json", local_manifest.str());
			}
			if (h3_local->executed && !h3_local->passed) {
				gates.push_back({id, "fail", h3_local->reason});
			} else {
				gates.push_back({id, "not-run", h3_local->reason.empty() ?
				                  "H3 requires source/info subcases after US5 before the full gate can pass" :
				                  h3_local->reason});
			}
			continue;
		}
		if (id == "H6") {
			if (!h6_local || !h6_local->executed) {
				gates.push_back({id, "not-run", h6_local ? h6_local->reason :
				                 "H6 local/loopback checks were not scheduled"});
			} else if (!h6_local->passed) {
				gates.push_back({id, "fail", h6_local->reason});
			} else {
				std::ostringstream reason;
				reason << h6_local->reason;
				const auto h6_missing = MissingInputs(options, {"H6"});
				if (!h6_missing.empty()) {
					reason << "; missing controlled-service inputs: ";
					for (std::size_t index = 0; index < h6_missing.size(); index++) {
						if (index) reason << ", ";
						reason << h6_missing[index];
					}
				}
				gates.push_back({id, "not-run", reason.str()});
			}
			continue;
		}
		if (id == "H7") {
			if (!h7_local || !h7_local->inputs_ready || !h7_local->executed) {
				gates.push_back({id, "not-run", h7_local ? h7_local->reason :
				                 "H7 local source/relation checks were not scheduled because validation inputs are missing"});
			} else if (!h7_local->passed) {
				gates.push_back({id, "fail", h7_local->reason});
			} else {
				gates.push_back({id, "not-run", h7_local->reason});
			}
			continue;
		}
		if (id == "H8") {
			if (!h8_matrix || !h8_matrix->executed) {
				gates.push_back({id, "not-run", h8_matrix ? h8_matrix->reason :
				                 "H8 matrix identity audit was not scheduled because required inputs are missing"});
			} else if (h8_matrix->result.exit_code == 0) {
				gates.push_back({id, "pass", h8_matrix->reason});
			} else if (h8_matrix->result.exit_code == 2) {
				gates.push_back({id, "not-run", h8_matrix->reason});
			} else {
				gates.push_back({id, "fail", h8_matrix->reason});
			}
			continue;
		}
		std::ostringstream reason;
		reason << "gate execution is not implemented in this runner yet";
		if (!missing.empty()) {
			reason << "; missing inputs: ";
			for (std::size_t i = 0; i < missing.size(); i++) {
				if (i) reason << ", ";
				reason << missing[i];
			}
		}
		gates.push_back({id, "not-run", reason.str()});
	}
	const auto gate_status_errors = AuditGridGateResults(gates, cases);
	auto summary = SummarizeGates(gates);
	if (!gate_status_errors.empty()) {
		summary.status = "fail";
		summary.exit_code = 1;
		summary.any_passed = false;
	}
	std::ostringstream manifest;
	const auto run_status = summary.status == "pass" ? "success" : summary.status;
	manifest << "{\n  \"schema_version\":1,\n  \"status\":" << JsonString(run_status)
	         << ",\n  \"created_at_utc\":"
	         << JsonString(UtcNow()) << ",\n  \"requested_cases\":[";
	for (std::size_t i = 0; i < cases.size(); i++) {
		if (i) manifest << ",";
		manifest << JsonString(cases[i]);
	}
	manifest << "],\n  \"requested_gates\":[";
	for (std::size_t i = 0; i < cases.size(); i++) {
		if (i) manifest << ",";
		manifest << JsonString(cases[i]);
	}
	manifest << "],\n  \"command_redacted\":" << RedactedArguments(argc, argv)
	         << ",\n  \"inputs\":{\n    " << FileRecord("sample_manifest", options.manifest, root) << ",\n    "
	         << FileRecord("checked_in_sample_queries", options.root / "test/data/grids/sample-queries.sql", root)
	         << ",\n    "
	         << FileRecord("evidence_contract_test", options.root / "test/tools/grid_evidence_test.py", root)
	         << ",\n    "
	         << FileRecord("duckdb", options.duckdb, root, true) << ",\n    "
	         << FileRecord("extension", options.extension, root) << ",\n    "
	         << FileRecord("httpfs", options.httpfs, root) << ",\n    "
	         << FileRecord("matrix", options.matrix, root) << ",\n    "
	         << FileRecord("h8_matrix_auditor", options.root / "scripts/audit-grid-version-matrix.py", root)
	         << "\n  },\n  \"frozen_input_snapshot\":" << ArtifactRecord(snapshot_path, root)
	         << ",\n  \"frozen_input_set_sha256\":"
	         << (frozen_input_set_sha256 ? JsonString(*frozen_input_set_sha256) : "null")
	         << ",\n  \"remote_inputs_present\":{\"http_base\":"
	         << (!options.http_base.empty() ? "true" : "false") << ",\"https_base\":"
	         << (!options.https_base.empty() ? "true" : "false") << ",\"s3_base\":"
	         << (!options.s3_base.empty() ? "true" : "false") << "},\n  \"gates\":[";
	for (std::size_t i = 0; i < gates.size(); i++) {
		if (i) manifest << ",";
		manifest << "{\"id\":" << JsonString(gates[i].gate_id) << ",\"status\":"
		         << JsonString(gates[i].status) << ",\"reason\":" << JsonString(gates[i].reason);
		if (gates[i].gate_id == "H8" && h8_matrix && h8_matrix->executed &&
		    h8_matrix->result.exit_code == 0) {
			manifest << ",\"input_snapshot\":{\"frozen_at_utc\":" << JsonString(UtcNow())
			         << ",\"frozen_input_set_sha256\":"
			         << (frozen_input_set_sha256 ? JsonString(*frozen_input_set_sha256) : "null") << "}"
			         << ",\"commands\":[{\"argv\":[";
			for (std::size_t argument = 0; argument < h8_matrix->arguments.size(); argument++) {
				if (argument) manifest << ",";
				manifest << JsonString(h8_matrix->arguments[argument]);
			}
			manifest << "],\"exit_code\":" << h8_matrix->result.exit_code << "}]"
			         << ",\"artifacts\":[" << ArtifactRecord(h8_matrix->summary, root) << "]";
		}
		manifest << "}";
	}
	manifest << "]";
	if (h1_public_source_checks && h1_public_source_checks->executed) {
		manifest << ",\n  \"h1_public_source_audit\":{\"manifest\":"
		         << ArtifactRecord(h1_public_source_checks->directory / "manifest.json", root)
		         << ",\"report\":" << ArtifactRecord(h1_public_source_checks->report, root)
		         << ",\"command\":"
		         << CommandRecord(h1_public_source_checks->arguments, h1_public_source_checks->result) << "}";
	}
	if (h3_local && h3_local->executed) {
		manifest << ",\n  \"h3_local_audit\":{\"manifest\":"
		         << ArtifactRecord(options.output / "h3-local/manifest.json", root)
		         << ",\"public_source_report\":" << ArtifactRecord(h3_local->public_source_report, root)
		         << ",\"command\":"
		         << CommandRecord(h3_local->public_source_arguments, h3_local->public_source_result) << "}";
	}
	if (h7_local && h7_local->executed) {
		manifest << ",\n  \"h7_local_audit\":{\"manifest\":"
		         << ArtifactRecord(options.output / "h7-local/manifest.json", root)
		         << ",\"synthetic_report\":" << ArtifactRecord(h7_local->summary, root)
		         << ",\"command\":" << CommandRecord(h7_local->arguments, h7_local->result) << "}";
	}
	if (h6_local && h6_local->executed) {
		manifest << ",\n  \"h6_local_audit\":{\"manifest\":"
		         << ArtifactRecord(options.output / "h6-local/manifest.json", root) << ",\"comparison\":"
		         << ArtifactRecord(options.output / "h6-local/source-position-comparison.json", root)
		         << ",\"attempt_stress\":"
		         << ArtifactRecord(options.output / "h6-local/attempt-stress.json", root)
		         << ",\"cancellation_release\":"
		         << ArtifactRecord(options.output / "h6-local/cancelled-query-release.json", root)
		         << ",\"command\":"
		         << CommandRecord({h6_local->remote_session_test.string()}, h6_local->result) << "}";
	}
	if (h8_matrix && h8_matrix->executed) {
		manifest << ",\n  \"h8_matrix_audit\":{\"summary\":"
		         << ArtifactRecord(h8_matrix->summary, root) << ",\"stdout\":"
		         << ArtifactRecord(h8_matrix->stdout_file, root) << ",\"command\":"
		         << CommandRecord(h8_matrix->arguments, h8_matrix->result) << "}";
	}
	manifest << ",\n  \"gate_status_audit\":{\"status\":"
	         << JsonString(gate_status_errors.empty() ? "pass" : "fail") << ",\"errors\":[";
	for (std::size_t i = 0; i < gate_status_errors.size(); i++) {
		if (i) manifest << ",";
		manifest << JsonString(gate_status_errors[i]);
	}
	manifest << "]},\n  \"exit_code\":" << summary.exit_code << "\n}\n";
	const auto manifest_path = options.output / "manifest.json";
	write_file(manifest_path, manifest.str());
	const auto evidence_contract = options.root / "test/tools/grid_evidence_test.py";
	const std::vector<std::string> evidence_audit_arguments{
	    "python3", evidence_contract.string(), "--audit-manifest", manifest_path.string(), "--root", root.string()};
	const auto evidence_audit = RunProcess(evidence_audit_arguments, root);
	const bool evidence_audit_passed = evidence_audit.exit_code == 0 && gate_status_errors.empty();
	const auto manifest_hash = Sha256(manifest_path, root);
	std::ostringstream evidence_audit_record;
	evidence_audit_record << "{\n  \"schema_version\":1,\n  \"status\":"
	                      << JsonString(evidence_audit_passed ? "pass" : "fail")
	                      << ",\n  \"manifest_sha256\":"
	                      << (manifest_hash ? JsonString(*manifest_hash) : "null")
	                      << ",\n  \"validator_sha256\":"
	                      << JsonString(Sha256(evidence_contract, root).value_or(""))
	                      << ",\n  \"gate_status_errors\":[";
	for (std::size_t i = 0; i < gate_status_errors.size(); i++) {
		if (i) evidence_audit_record << ",";
		evidence_audit_record << JsonString(gate_status_errors[i]);
	}
	evidence_audit_record
	                      << "],\n  \"command\":"
	                      << CommandRecord(evidence_audit_arguments, evidence_audit)
	                      << ",\n  \"output\":" << JsonString(evidence_audit.output) << "\n}\n";
	write_file(options.output / "evidence-audit.json", evidence_audit_record.str());

	std::ostringstream final;
	final << "# Grid validation run\n\nStatus: **" << summary.status << "**\n\nRequested gates:\n\n";
	for (const auto &gate : gates) {
		final << "- " << gate.gate_id << ": " << gate.status;
		if (!gate.reason.empty()) final << " — " << gate.reason;
		final << "\n";
	}
	final << (summary.any_passed ? "\nAt least one requested gate passed.\n" :
	                           "\nNo requested gate passed in this run.\n");
	final << "\nEvidence manifest audit: **" << (evidence_audit_passed ? "pass" : "fail") << "**.\n";
	write_file(options.output / "final.md", final.str());
	std::cout << "Recorded " << gates.size() << " gate result(s) with status=" << summary.status << " in "
	          << options.output << "; exit=" << summary.exit_code << "\n";
	if (!evidence_audit_passed) {
		std::cerr << evidence_audit.output;
		return 1;
	}
	return summary.exit_code;
}

void SelfCheck() {
	const auto passed = SummarizeGates({{"H0", "pass", ""}});
	const auto pending = SummarizeGates({{"H0", "pass", ""}, {"H1", "not-run", "missing input"}});
	const auto failed = SummarizeGates({{"H0", "fail", "mismatch"}, {"H1", "not-run", "missing input"}});
	const auto invalid = SummarizeGates({{"H0", "unknown", "invalid status"}});
	if (passed.status != "pass" || passed.exit_code != 0 || !passed.any_passed ||
	    pending.status != "not-run" || pending.exit_code != 2 || !pending.any_passed ||
	    failed.status != "fail" || failed.exit_code != 1 || failed.any_passed ||
	    invalid.status != "fail" || invalid.exit_code != 1) {
		throw std::runtime_error("grid gate result aggregation self-check failed");
	}
	const auto cases = ParseGridValidationCases("H0, H3,H9");
	if (cases != std::vector<std::string>{"H0", "H3", "H9"}) {
		throw std::runtime_error("grid case parser self-check failed");
	}
	if (!AuditGridGateResults({{"H0", "not-run", "oracle missing"}}, {"H0"}).empty() ||
	    AuditGridGateResults({{"H0", "not-run", "oracle missing"}}, {"H0", "H1"}).empty() ||
	    AuditGridGateResults({{"H0", "not-run", ""}}, {"H0"}).empty()) {
		throw std::runtime_error("grid gate evidence status audit self-check failed");
	}
	bool rejected_unknown = false;
	try {
		(void)ParseGridValidationCases("H0,H10");
	} catch (const std::invalid_argument &) {
		rejected_unknown = true;
	}
	if (!rejected_unknown || JsonEscape("a\nb") != "a\\nb") {
		throw std::runtime_error("grid validation support self-check failed");
	}
	const auto audit_directory = fs::temp_directory_path() /
	                              ("duckomo-grid-audit-self-check-" + std::to_string(getpid()));
	std::error_code ignored;
	fs::remove_all(audit_directory, ignored);
	fs::create_directories(audit_directory);
	{
		std::ofstream http_log(audit_directory / "http.jsonl");
		std::ofstream s3_log(audit_directory / "s3.jsonl");
		if (!http_log.good() || !s3_log.good()) throw std::runtime_error("cannot prepare audit-log self-check");
	}
	const bool audit_logs_valid = HasRemoteAuditLogs(audit_directory);
	fs::remove_all(audit_directory, ignored);
	if (!audit_logs_valid) throw std::runtime_error("H6 audit-log directory self-check failed");
}

} // namespace

int main(int argc, char **argv) {
	try {
		const auto options = ParseOptions(argc, argv);
		if (options.self_check) {
			SelfCheck();
			std::cout << "grid validation CLI self-check passed\n";
			return 0;
		}
		return WriteRun(options, argc, argv);
	} catch (const std::exception &exception) {
		std::cerr << "duckomo_grid_validation: " << exception.what() << '\n';
		return 2;
	}
}
