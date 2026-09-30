#pragma once

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace duckomo_validation_support {

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
	    "(.dependency_commits.duckdb == \"08e34c447bae34eaee3723cac61f2878b6bdf787\" and "
	    ".dependency_commits.\"om-file-format\" == \"d8855e418e2231ae8439f0c7e840fa3f93b371e3\" and "
	    ".dependency_commits.\"extension-ci-tools\" == \"b777c70d30942cca5bef62d6d4fa23a13362f398\") and "
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
	    "(.environment | type == \"object\" and .build == \"release\" and .threads == \"1\" and "
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
	    {"jq", "-er", "--arg", "fixture_id", expectation.fixture_id, "--arg", "fixture_sha256",
	     expectation.fixture_sha256, "--arg", "selection_mode",
	     expectation.selection_mode == "optimizer_empty" ? "empty" : expectation.selection_mode,
	     "--argjson", "plan_is_empty_result", expectation.optimizer_plan_is_empty_result ? "true" : "false",
	     "--argjson", "query_succeeded", expectation.query_result_succeeded ? "true" : "false", filter, sidecar.string()},
	    working_directory);
	return validation.exit_code == 0 && validation.output.find("true") != std::string::npos;
}

} // namespace duckomo_validation_support
