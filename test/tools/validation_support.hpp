#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "duckomo/build_identity.hpp"

namespace duckomo_validation_support {

inline std::string ExpectedDependencyCommit(const std::string &dependency) {
	const auto &identity = duckdb::duckomo::BUILD_IDENTITY;
	if (dependency == "duckdb" && identity.duckdb_commit[0] != '\0') return identity.duckdb_commit;
	if (dependency == "om-file-format" && identity.om_commit[0] != '\0') return identity.om_commit;
	if (dependency == "extension-ci-tools" && identity.extension_ci_tools_commit[0] != '\0') {
		return identity.extension_ci_tools_commit;
	}
	if (dependency == "duckdb") return "08e34c447bae34eaee3723cac61f2878b6bdf787";
	if (dependency == "om-file-format") return "d8855e418e2231ae8439f0c7e840fa3f93b371e3";
	if (dependency == "extension-ci-tools") return "b777c70d30942cca5bef62d6d4fa23a13362f398";
	return {};
}

inline std::string ExpectedBuildLabel() {
	const auto &pair_id = duckdb::duckomo::BUILD_IDENTITY.pair_id;
	return pair_id[0] == '\0' ? "release" : pair_id;
}

inline const std::vector<std::string> &GridValidationCaseIds() {
	static const std::vector<std::string> cases{"H0", "H1", "H2", "H3", "H4", "H5", "H6", "H7", "H8", "H9"};
	return cases;
}

inline std::vector<std::string> ParseGridValidationCases(const std::string &value) {
	std::vector<std::string> cases;
	std::set<std::string> seen;
	std::istringstream input(value);
	std::string item;
	while (std::getline(input, item, ',')) {
		const auto begin = item.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) throw std::invalid_argument("--cases contains an empty gate id");
		const auto end = item.find_last_not_of(" \t\r\n");
		item = item.substr(begin, end - begin + 1);
		if (std::find(GridValidationCaseIds().begin(), GridValidationCaseIds().end(), item) ==
		    GridValidationCaseIds().end()) {
			throw std::invalid_argument("unknown validation gate " + item + "; expected H0 through H9");
		}
		if (!seen.insert(item).second) throw std::invalid_argument("duplicate validation gate " + item);
		cases.push_back(item);
	}
	if (cases.empty()) throw std::invalid_argument("--cases must name at least one gate");
	return cases;
}

inline std::string JsonEscape(const std::string &value) {
	std::string result;
	result.reserve(value.size() + 2);
	for (const auto byte : value) {
		switch (byte) {
		case '"': result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\b': result += "\\b"; break;
		case '\f': result += "\\f"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default:
			if (static_cast<unsigned char>(byte) < 0x20) {
				static constexpr char HEX[] = "0123456789abcdef";
				result += "\\u00";
				result.push_back(HEX[(static_cast<unsigned char>(byte) >> 4) & 0x0f]);
				result.push_back(HEX[static_cast<unsigned char>(byte) & 0x0f]);
			} else {
				result.push_back(byte);
			}
		}
	}
	return result;
}

inline std::string JsonString(const std::string &value) {
	return "\"" + JsonEscape(value) + "\"";
}

struct GridGateResult final {
	std::string gate_id;
	std::string status = "not-run";
	std::string reason;
};

inline std::vector<std::string> AuditGridGateResults(const std::vector<GridGateResult> &gates,
	                                                const std::vector<std::string> &requested_gates) {
	std::vector<std::string> errors;
	const std::set<std::string> requested(requested_gates.begin(), requested_gates.end());
	std::set<std::string> recorded;
	if (requested.size() != requested_gates.size()) errors.emplace_back("requested gate list contains duplicates");
	for (const auto &gate : gates) {
		if (gate.gate_id.empty() || !recorded.insert(gate.gate_id).second) {
			errors.emplace_back("gate list contains a missing or duplicate id");
		}
		if (gate.status != "pass" && gate.status != "fail" && gate.status != "not-run") {
			errors.emplace_back("gate '" + gate.gate_id + "' has an invalid status");
		}
		if (gate.status != "pass" && gate.reason.empty()) {
			errors.emplace_back("gate '" + gate.gate_id + "' needs a reason for its status");
		}
	}
	if (requested != recorded) errors.emplace_back("recorded gates do not match the requested gate set");
	return errors;
}

struct CommandResult {
	int exit_code = -1;
	std::string output;
	std::uint64_t peak_rss_bytes = 0;
	double elapsed_ms = 0;
};

inline CommandResult RunProcess(const std::vector<std::string> &arguments,
	                            const std::filesystem::path &working_directory,
	                            const std::map<std::string, std::string> &environment = {}) {
	int output_pipe[2];
	if (pipe(output_pipe) != 0) {
		throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));
	}
	const auto started = std::chrono::steady_clock::now();
	const pid_t child = fork();
	if (child < 0) {
		close(output_pipe[0]);
		close(output_pipe[1]);
		throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
	}
	if (child == 0) {
		close(output_pipe[0]);
		if (dup2(output_pipe[1], STDOUT_FILENO) < 0 || dup2(output_pipe[1], STDERR_FILENO) < 0) {
			_exit(126);
		}
		close(output_pipe[1]);
		if (chdir(working_directory.c_str()) != 0) {
			_exit(126);
		}
		for (const auto &entry : environment) {
			if (setenv(entry.first.c_str(), entry.second.c_str(), 1) != 0) {
				_exit(126);
			}
		}
		std::vector<char *> argv;
		argv.reserve(arguments.size() + 1);
		for (const auto &argument : arguments) {
			argv.push_back(const_cast<char *>(argument.c_str()));
		}
		argv.push_back(nullptr);
		execvp(arguments[0].c_str(), argv.data());
		const std::string error = "exec failed: " + std::string(std::strerror(errno)) + "\n";
		const auto error_written = write(STDERR_FILENO, error.data(), error.size());
		(void)error_written;
		_exit(127);
	}
	close(output_pipe[1]);
	CommandResult result;
	std::array<char, 16384> buffer {};
	for (;;) {
		const auto count = read(output_pipe[0], buffer.data(), buffer.size());
		if (count > 0) {
			result.output.append(buffer.data(), static_cast<std::size_t>(count));
			continue;
		}
		if (count == 0) {
			break;
		}
		if (errno == EINTR) {
			continue;
		}
		close(output_pipe[0]);
		throw std::runtime_error("read from child failed: " + std::string(std::strerror(errno)));
	}
	close(output_pipe[0]);
	int status = 0;
	struct rusage usage {};
	pid_t waited;
	do {
		waited = wait4(child, &status, 0, &usage);
	} while (waited < 0 && errno == EINTR);
	if (waited < 0) {
		throw std::runtime_error("wait4 failed: " + std::string(std::strerror(errno)));
	}
	const auto finished = std::chrono::steady_clock::now();
	result.elapsed_ms = std::chrono::duration<double, std::milli>(finished - started).count();
	if (WIFEXITED(status)) {
		result.exit_code = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		result.exit_code = 128 + WTERMSIG(status);
	}
#if defined(__APPLE__)
	result.peak_rss_bytes = static_cast<std::uint64_t>(usage.ru_maxrss);
#else
	result.peak_rss_bytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
	return result;
}

struct SpatialEvidenceExpectation final {
	std::string fixture_id;
	std::string fixture_sha256;
	std::string selection_mode;
	bool require_zero_value_reads = false;
	bool optimizer_plan_is_empty_result = false;
	bool query_result_succeeded = false;
};

inline bool ValidateSpatialMetricsEvidence(const std::filesystem::path &sidecar,
	                                      const SpatialEvidenceExpectation &expectation,
	                                      const std::filesystem::path &working_directory) {
	if (!std::filesystem::is_regular_file(sidecar)) {
		return false;
	}
	std::string filter =
	    ".schema_version == 2 and .fixture_id == $fixture_id and .fixture_sha256 == $fixture_sha256 and "
	    ".status == \"success\" and .comparison_passed == true and "
	    "(.result_rows | type == \"number\" and . >= 0) and "
	    "(.elapsed_ms | type == \"number\" and . >= 0) and "
	    "(.peak_rss_bytes | type == \"number\" and . > 0) and "
	    "(.reference_identity | type == \"string\" and length > 0) and "
	    "(.coordinate_tolerance | type == \"number\" and . >= 0) and "
	    "(.sql | type == \"string\" and length > 0) and "
	    "(.comparison | type == \"string\" and length > 0) and "
	    "(.command | type == \"array\" and length > 0) and "
	    "(.error_category == \"\" and .child_exit_code == 0) and "
	    "(.dependency_commits.duckdb == $expected_duckdb and "
	    ".dependency_commits.\"om-file-format\" == $expected_om and "
	    ".dependency_commits.\"extension-ci-tools\" == $expected_ci_tools) and "
	    "(.bind_metadata_bytes | type == \"number\" and . >= 0) and "
	    "(.bind_metadata_requests | type == \"number\" and . >= 0) and "
	    "(.scan_metadata_bytes | type == \"number\" and . >= 0) and "
	    "(.scan_metadata_requests | type == \"number\" and . >= 0) and "
	    "(.metadata_bytes | type == \"number\" and . >= 0) and "
	    "(.metadata_requests | type == \"number\" and . >= 0) and "
	    "(.grid_definition | type == \"string\" and length > 0) and "
	    "(.spatial_layout | type == \"string\" and length > 0) and "
	    "(.grid_source | type == \"string\" and length > 0) and "
	    ".residual_filter_retained == true and "
	    "(.fallback_reasons | type == \"array\") and "
	    "(.candidate_rows | type == \"number\" and . >= 0) and "
	    "(.optimizer_empty | type == \"boolean\") and "
	    "(.environment | type == \"object\" and .build == $expected_build and .threads == \"1\" and "
	    "(.system | type == \"string\" and length > 0) and (.machine | type == \"string\" and length > 0)) and "
	    "(.cache_policy | type == \"object\" and (.application_cache | type == \"string\" and length > 0) and "
	    "(.os_page_cache | type == \"string\" and length > 0)) and "
	    "(.variables | type == \"object\" and length > 0) and "
	    "all(.variables[]; (.index_bytes | type == \"number\" and . >= 0) and "
	    "(.index_requests | type == \"number\" and . >= 0) and "
	    "(.data_bytes | type == \"number\" and . >= 0) and "
	    "(.data_requests | type == \"number\" and . >= 0) and "
	    "(.decoded_chunks | type == \"number\" and . >= 0) and .decode_count_complete == true) and "
	    ".decode_count_complete == true and "
	    ".bytes_fetched == (.metadata_bytes + ([.variables[].index_bytes] | add // 0) + "
	    "([.variables[].data_bytes] | add // 0)) and "
	    ".read_requests == (.metadata_requests + ([.variables[].index_requests] | add // 0) + "
	    "([.variables[].data_requests] | add // 0)) and "
	    ".selection_mode == $selection_mode";
	if (expectation.require_zero_value_reads) {
		filter += " and all(.variables[]; .index_bytes == 0 and .index_requests == 0 and "
		          ".data_bytes == 0 and .data_requests == 0 and .decoded_chunks == 0)";
	}
	if (expectation.selection_mode == "restricted") {
		filter += " and .optimizer_empty == false";
	}
	if (expectation.selection_mode == "full" || expectation.selection_mode == "empty") {
		filter += " and .optimizer_empty == false";
	}
	if (expectation.selection_mode == "fallback") {
		filter += " and .optimizer_empty == false and (.fallback_reasons | length > 0)";
	}
	if (expectation.selection_mode == "optimizer_empty") {
		filter += " and .optimizer_empty == true and .selection_mode == \"empty\" and .candidate_rows == 0 and "
		          ".bind_metadata_bytes > 0 and $plan_is_empty_result == true and $query_succeeded == true";
	}
	const auto validation = RunProcess(
	    {"jq", "-er", "--arg", "expected_duckdb", ExpectedDependencyCommit("duckdb"), "--arg",
	     "expected_om", ExpectedDependencyCommit("om-file-format"), "--arg", "expected_ci_tools",
	     ExpectedDependencyCommit("extension-ci-tools"), "--arg", "expected_build", ExpectedBuildLabel(),
	     "--arg", "fixture_id", expectation.fixture_id, "--arg", "fixture_sha256",
	     expectation.fixture_sha256, "--arg", "selection_mode",
	     expectation.selection_mode == "optimizer_empty" ? "empty" : expectation.selection_mode,
	     "--argjson", "plan_is_empty_result", expectation.optimizer_plan_is_empty_result ? "true" : "false",
	     "--argjson", "query_succeeded", expectation.query_result_succeeded ? "true" : "false", filter, sidecar.string()},
	    working_directory);
	return validation.exit_code == 0 && validation.output.find("true") != std::string::npos;
}

} // namespace duckomo_validation_support
