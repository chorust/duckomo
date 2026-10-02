#include "validation_support.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <map>
#include <poll.h>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
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
	fs::path httpfs;
	std::string http_base;
	std::string s3_base;
	fs::path s3_setup;
	fs::path server_log;
	fs::path real_file;
	fs::path real_manifest;
};

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

Options ParseOptions(int argc, char **argv) {
	const auto executable = fs::weakly_canonical(fs::absolute(argv[0]));
	Options options;
	options.root = executable.parent_path().parent_path().parent_path().parent_path().parent_path();
	options.fixtures = options.root / "test/data";
	options.output = options.root / "build/evidence/remote";
	options.duckdb = options.root / "build/release/duckdb";
	options.extension = options.root / "build/release/extension/duckomo/duckomo.duckdb_extension";
	options.httpfs = options.root / "build/release/extension/httpfs/httpfs.duckdb_extension";
	for (int i = 1; i < argc; i++) {
		const std::string name = argv[i];
		if (name == "--help" || name == "-h") {
			std::cout << "Usage: duckomo_remote_validation --root PATH --fixtures PATH --output PATH --duckdb PATH "
			             "--extension PATH --httpfs PATH --http-base URL --s3-base URI --s3-setup PATH "
			             "--server-log PATH --real-file PATH --real-manifest PATH\n";
			std::exit(0);
		}
		Require(i + 1 < argc, "missing value after " + name);
		const std::string value = argv[++i];
		if (name == "--root") options.root = value;
		else if (name == "--fixtures") options.fixtures = value;
		else if (name == "--output") options.output = value;
		else if (name == "--duckdb") options.duckdb = value;
		else if (name == "--extension") options.extension = value;
		else if (name == "--httpfs") options.httpfs = value;
		else if (name == "--http-base") options.http_base = value;
		else if (name == "--s3-base") options.s3_base = value;
		else if (name == "--s3-setup") options.s3_setup = value;
		else if (name == "--server-log") options.server_log = value;
		else if (name == "--real-file") options.real_file = value;
		else if (name == "--real-manifest") options.real_manifest = value;
		else throw std::runtime_error("unknown argument " + name);
	}
	auto resolve = [&](fs::path &path) {
		if (path.is_relative()) path = fs::current_path() / path;
		path = fs::weakly_canonical(path);
	};
	resolve(options.root);
	resolve(options.fixtures);
	resolve(options.output);
	resolve(options.duckdb);
	resolve(options.extension);
	resolve(options.httpfs);
	resolve(options.s3_setup);
	resolve(options.server_log);
	resolve(options.real_file);
	resolve(options.real_manifest);
	Require(!options.http_base.empty() && !options.s3_base.empty(), "both HTTP and S3 endpoints are required");
	return options;
}

std::string ReadText(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "cannot read required validation input " + path.string());
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		result.push_back(character);
		if (character == '\'') result.push_back('\'');
	}
	return result + "'";
}

std::string Jq(const fs::path &path, const std::string &expression, const fs::path &root) {
	const auto result = RunProcess({"jq", "-er", expression, path.string()}, root);
	Require(result.exit_code == 0, "cannot read a required pinned manifest field");
	return result.output.substr(0, result.output.find_first_of("\r\n"));
}

std::string Sha256(const fs::path &path, const fs::path &root) {
	const auto result = RunProcess({"sha256sum", path.string()}, root);
	Require(result.exit_code == 0, "cannot compute fixture SHA-256");
	return result.output.substr(0, result.output.find_first_of(" \t\r\n"));
}

void ClearLog(const fs::path &path) {
	fs::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	Require(output.good(), "cannot reset service audit log");
}

std::uint64_t LogBytes(const fs::path &path, const fs::path &root) {
	Require(fs::is_regular_file(path), "required remote server audit log is missing");
	const auto result = RunProcess({"jq", "-s", "[.[].body_bytes] | add // 0", path.string()}, root);
	Require(result.exit_code == 0, "remote server audit log is malformed");
	return std::stoull(result.output);
}

std::uint64_t LogAuditBytes(const fs::path &path, const std::string &audit, const fs::path &root) {
	const auto result = RunProcess({"jq", "-s", "--arg", "audit", audit,
	                                "[.[] | select(.audit == $audit) | .body_bytes] | add // 0", path.string()}, root);
	Require(result.exit_code == 0, "cannot read a per-query HTTP audit total");
	return std::stoull(result.output);
}

bool HasAuditRequest(const fs::path &path, const std::string &audit, const fs::path &root) {
	if (!fs::is_regular_file(path)) return false;
	const auto result = RunProcess({"jq", "-se", "--arg", "audit", audit,
	                                "any(.[]; .audit == $audit and .event == \"started\" and "
	                                ".method == \"GET\" and .range != \"bytes=0-0\")", path.string()}, root);
	return result.exit_code == 0 && result.output.find("true") != std::string::npos;
}

bool HasAuditCompletion(const fs::path &path, const std::string &audit, const fs::path &root) {
	if (!fs::is_regular_file(path)) return false;
	const auto result = RunProcess({"jq", "-se", "--arg", "audit", audit,
	                                "any(.[]; .audit == $audit and .event == \"complete\")", path.string()}, root);
	return result.exit_code == 0 && result.output.find("true") != std::string::npos;
}

bool HasPendingAuditRequest(const fs::path &path, const std::string &audit, const fs::path &root) {
	if (!fs::is_regular_file(path)) return false;
	const auto result = RunProcess({"jq", "-se", "--arg", "audit", audit,
	                                "([.[] | select(.audit == $audit and .event == \"started\")] | length) > "
	                                "([.[] | select(.audit == $audit and .event == \"complete\")] | length)",
	                                path.string()}, root);
	return result.exit_code == 0 && result.output.find("true") != std::string::npos;
}

std::string StripCsvString(std::string value) {
	while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
	if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
		value = value.substr(1, value.size() - 2);
		std::string decoded;
		for (std::size_t i = 0; i < value.size(); i++) {
			decoded.push_back(value[i]);
			if (value[i] == '"' && i + 1 < value.size() && value[i + 1] == '"') i++;
		}
		return decoded;
	}
	return value;
}

std::string UriChild(const std::string &base, const std::string &child) {
	auto result = base;
	while (!result.empty() && result.back() == '/') result.pop_back();
	return result + "/" + child;
}

double Median(std::vector<double> values) {
	Require(!values.empty(), "cannot calculate the median of an empty sample set");
	std::sort(values.begin(), values.end());
	const auto middle = values.size() / 2;
	if (values.size() % 2 != 0) return values[middle];
	return (values[middle - 1] + values[middle]) / 2.0;
}

struct RunResult final {
	fs::path result_csv;
	fs::path metrics_v2;
	fs::path metrics_v3;
	fs::path metrics_v3_sidecar;
	std::string metrics_json;
	double elapsed_ms = 0;
	std::uint64_t peak_rss_bytes = 0;
	int exit_code = -1;
};

class InteractiveDuckDB final {
public:
	InteractiveDuckDB(const fs::path &executable, const fs::path &working_directory,
                 const std::map<std::string, std::string> &environment, bool bail_on_error = true) {
		int input_pipe[2];
		int output_pipe[2];
		if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
			throw std::runtime_error("cannot create interactive DuckDB pipes");
		}
		child_ = fork();
		if (child_ < 0) throw std::runtime_error("cannot start interactive DuckDB process");
		if (child_ == 0) {
			dup2(input_pipe[0], STDIN_FILENO);
			dup2(output_pipe[1], STDOUT_FILENO);
			dup2(output_pipe[1], STDERR_FILENO);
			close(input_pipe[0]);
			close(input_pipe[1]);
			close(output_pipe[0]);
			close(output_pipe[1]);
			if (chdir(working_directory.c_str()) != 0) _exit(126);
			for (const auto &entry : environment) {
				if (setenv(entry.first.c_str(), entry.second.c_str(), 1) != 0) _exit(126);
			}
			const auto executable_text = executable.string();
			if (bail_on_error) {
				char *arguments[] = {const_cast<char *>(executable_text.c_str()), const_cast<char *>("-unsigned"),
				                     const_cast<char *>("-bail"), const_cast<char *>("-csv"),
				                     const_cast<char *>("-noheader"), const_cast<char *>(":memory:"), nullptr};
				execv(executable_text.c_str(), arguments);
			} else {
				char *arguments[] = {const_cast<char *>(executable_text.c_str()), const_cast<char *>("-unsigned"),
				                     const_cast<char *>("-csv"), const_cast<char *>("-noheader"),
			                     const_cast<char *>(":memory:"), nullptr};
				execv(executable_text.c_str(), arguments);
			}
			_exit(127);
		}
		close(input_pipe[0]);
		close(output_pipe[1]);
		input_fd_ = input_pipe[1];
		output_fd_ = output_pipe[0];
	}

	InteractiveDuckDB(const InteractiveDuckDB &) = delete;
	InteractiveDuckDB &operator=(const InteractiveDuckDB &) = delete;
	InteractiveDuckDB(InteractiveDuckDB &&other) noexcept
	    : child_(other.child_), input_fd_(other.input_fd_), output_fd_(other.output_fd_) {
		other.child_ = -1;
		other.input_fd_ = -1;
		other.output_fd_ = -1;
	}

	~InteractiveDuckDB() {
		if (input_fd_ >= 0) close(input_fd_);
		if (output_fd_ >= 0) close(output_fd_);
		if (child_ > 0) {
			kill(child_, SIGKILL);
			waitpid(child_, nullptr, 0);
		}
	}

	void SendUntilMarker(const std::string &sql, const std::string &marker) {
		WriteAll(sql + "\nSELECT " + SqlLiteral(marker) + ";\n");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
		std::string line;
		std::array<char, 4096> buffer {};
		while (std::chrono::steady_clock::now() < deadline) {
			pollfd descriptor {output_fd_, POLLIN | POLLHUP, 0};
			const auto ready = poll(&descriptor, 1, 1000);
			if (ready < 0 && errno == EINTR) continue;
			if (ready < 0) throw std::runtime_error("cannot read interactive DuckDB output");
			if (ready == 0) continue;
			const auto count = read(output_fd_, buffer.data(), buffer.size());
			if (count == 0) throw std::runtime_error("interactive DuckDB exited before its completion marker");
			if (count < 0) {
				if (errno == EINTR) continue;
				throw std::runtime_error("cannot read interactive DuckDB output");
			}
			for (ssize_t index = 0; index < count; index++) {
				if (buffer[static_cast<std::size_t>(index)] == '\n') {
					while (!line.empty() && line.back() == '\r') line.pop_back();
					if (line == marker) return;
					line.clear();
				} else if (line.size() < 65536) {
					line.push_back(buffer[static_cast<std::size_t>(index)]);
				}
			}
		}
		throw std::runtime_error("interactive DuckDB did not finish the remote query before timeout");
	}

	void Interrupt() {
		if (child_ <= 0 || kill(child_, SIGINT) != 0)
			throw std::runtime_error("could not interrupt the active DuckDB query");
	}

	int Finish() {
		WriteAll(".quit\n");
		close(input_fd_);
		input_fd_ = -1;
		std::array<char, 4096> buffer {};
		for (;;) {
			const auto count = read(output_fd_, buffer.data(), buffer.size());
			if (count == 0) break;
			if (count < 0 && errno == EINTR) continue;
			if (count < 0) throw std::runtime_error("cannot drain interactive DuckDB output");
		}
		close(output_fd_);
		output_fd_ = -1;
		int status = 0;
		if (waitpid(child_, &status, 0) < 0) throw std::runtime_error("cannot reap interactive DuckDB process");
		child_ = -1;
		if (WIFEXITED(status)) return WEXITSTATUS(status);
		return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
	}

private:
	void WriteAll(const std::string &text) {
		std::size_t written = 0;
		while (written < text.size()) {
			const auto count = write(input_fd_, text.data() + written, text.size() - written);
			if (count < 0 && errno == EINTR) continue;
			if (count <= 0) throw std::runtime_error("cannot send SQL to interactive DuckDB");
			written += static_cast<std::size_t>(count);
		}
	}

	pid_t child_ = -1;
	int input_fd_ = -1;
	int output_fd_ = -1;
};

class RemoteHarness final {
public:
	explicit RemoteHarness(Options options) : options_(std::move(options)) {
		auto temp_base = fs::temp_directory_path() / ("duckomo-remote-validation-" + std::to_string(getpid()));
		private_sql_ = temp_base;
		fs::create_directories(private_sql_);
		chmod(private_sql_.c_str(), 0700);
		s3_setup_sql_ = ReadText(options_.s3_setup);
	}

	~RemoteHarness() {
		std::error_code ignored;
		fs::remove_all(private_sql_, ignored);
	}

	RunResult RunScan(const std::string &name, const std::string &fixture_id, const std::string &input,
	                  const std::string &query, bool s3, const fs::path &log_path,
	                  bool expect_success = true, std::uint64_t duckdb_threads = 1,
	                  std::uint64_t duckomo_threads = 1, bool cache_enabled = false) {
		RunResult run;
		run.result_csv = options_.output / (name + ".csv");
		run.metrics_v2 = options_.output / (name + ".metrics.v2.json");
		run.metrics_v3 = options_.output / (name + ".metrics.v3.csv");
		run.metrics_v3_sidecar = options_.output / (name + ".metrics.v3.sidecar.json");
		if (!log_path.empty()) ClearLog(log_path);
		const auto script = private_sql_ / (name + ".sql");
		std::ostringstream sql;
		sql << "LOAD " << SqlLiteral(options_.httpfs.string()) << ";\n"
		    << "LOAD " << SqlLiteral(options_.extension.string()) << ";\n";
		if (s3) sql << s3_setup_sql_ << '\n';
		sql << "SET threads=" << duckdb_threads << "; SET duckomo_max_threads=" << duckomo_threads
		    << "; SET duckomo_cache_enabled=" << (cache_enabled ? "true" : "false") << ";\n";
		sql << "COPY (" << query << ") TO " << SqlLiteral(run.result_csv.string())
		    << " (FORMAT CSV, HEADER true);\n";
		sql << "COPY (SELECT metrics FROM duckomo_last_scan_metrics()) TO "
		    << SqlLiteral(run.metrics_v3.string()) << " (FORMAT CSV, HEADER false);\n";
		{
			std::ofstream output(script, std::ios::binary | std::ios::trunc);
			Require(output.good(), "cannot create private DuckDB validation script");
			output << sql.str();
		}
		chmod(script.c_str(), 0600);
		const auto child = RunProcess({options_.duckdb.string(), "-unsigned", "-bail", "-no-stdin", "-noheader",
		                               "-csv", "-nullvalue", "NULL", "-f", script.string(), ":memory:"},
		                              options_.root,
		                              {{"DUCKOMO_METRICS_OUTPUT", (options_.output / (name + ".metrics.v2.json")).string()},
		                               {"DUCKOMO_METRICS_V3_OUTPUT", run.metrics_v3_sidecar.string()},
		                               {"DUCKOMO_SCENARIO", name}, {"DUCKOMO_FIXTURE_ID", fixture_id}});
		run.exit_code = child.exit_code;
		run.elapsed_ms = child.elapsed_ms;
		run.peak_rss_bytes = child.peak_rss_bytes;
		if (expect_success) {
			Require(run.exit_code == 0 && fs::is_regular_file(run.result_csv),
			        "remote validation scenario failed; details were omitted to avoid exposing request data");
			Require(fs::is_regular_file(run.metrics_v2) && fs::is_regular_file(run.metrics_v3) &&
			            fs::is_regular_file(run.metrics_v3_sidecar),
			        "remote validation scenario did not produce v2, SQL v3, and QueryEnd v3 profiles");
			run.metrics_json = StripCsvString(ReadText(run.metrics_v3));
			const auto json_path = options_.output / (name + ".metrics.v3.json");
			{
				std::ofstream out(json_path, std::ios::binary | std::ios::trunc);
				out << run.metrics_json << '\n';
			}
			const auto schema = RunProcess({"jq", "-er", ".schema_version == 3", json_path.string()}, options_.root);
			Require(schema.exit_code == 0, "remote validation profile is not schema v3");
			const auto query_end = RunProcess({"jq", "-cer", "--argjson", "profile", run.metrics_json,
			                                  "length == 1 and .[0] == $profile", run.metrics_v3_sidecar.string()},
			                                 options_.root);
			Require(query_end.exit_code == 0, "QueryEnd v3 sidecar differs from SQL scan metrics");
		} else {
			Require(run.exit_code != 0, "remote validation accepted an injected HTTP protocol failure");
			Require(fs::is_regular_file(run.metrics_v3_sidecar),
			        "failed scan did not retain a QueryEnd v3 profile");
			const auto failed_profile = RunProcess({"jq", "-cer", ".[0]", run.metrics_v3_sidecar.string()}, options_.root);
			Require(failed_profile.exit_code == 0, "failed scan v3 sidecar is malformed");
			run.metrics_json = failed_profile.output;
			const auto json_path = options_.output / (name + ".metrics.v3.json");
			{
				std::ofstream out(json_path, std::ios::binary | std::ios::trunc);
				out << run.metrics_json;
			}
			const auto status = RunProcess({"jq", "-er", ".schema_version == 3 and .status == \"failure\"", json_path.string()},
			                              options_.root);
			Require(status.exit_code == 0, "failed scan v3 profile lost its failure terminal state");
		}
		if (!log_path.empty()) {
			WaitForLogBytes(log_path, V3Uint(run, "response_body_bytes"));
			fs::copy_file(log_path, options_.output / (name + ".server.jsonl"), fs::copy_options::overwrite_existing);
		}
		return run;
	}

	void Compare(const fs::path &left, const fs::path &right, const std::string &name) {
		const auto diff = RunProcess({"diff", "-q", left.string(), right.string()}, options_.root);
		Require(diff.exit_code == 0, "complete local/remote result files differ for " + name);
		comparisons_.push_back(name);
	}

	std::uint64_t MetricUint(const fs::path &metrics, const std::string &expression) const {
		return std::stoull(Jq(metrics, expression, options_.root));
	}

	std::string V3Value(const RunResult &run, const std::string &field) const {
		const auto path = options_.output / (field + ".profile.tmp.json");
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << run.metrics_json << '\n';
		}
		auto value = Jq(path, field.empty() || field.front() != '.' ? "." + field : field, options_.root);
		fs::remove(path);
		return value;
	}

	std::uint64_t V3Uint(const RunResult &run, const std::string &field) const {
		return std::stoull(V3Value(run, field));
	}

	void WaitForLogBytes(const fs::path &path, std::uint64_t expected) const {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < deadline) {
			bool valid_log = false;
			std::uint64_t observed = 0;
			try {
				observed = LogBytes(path, options_.root);
				valid_log = true;
			} catch (const std::runtime_error &) {
				// The HTTP handler appends its JSONL event after the final response byte is sent.
			}
			if (valid_log && observed == expected) return;
			if (valid_log && observed > expected)
				throw std::runtime_error("server audit body exceeds the per-query v3 profile");
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		throw std::runtime_error("remote server body did not reconcile with the completed query profile");
	}

	void WaitForAuditRequest(const fs::path &path, const std::string &audit) const {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < deadline) {
			if (HasAuditRequest(path, audit, options_.root)) return;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		throw std::runtime_error("HTTP fixture did not observe the expected ranged request for cancellation");
	}

	void WaitForAuditBytes(const fs::path &path, const std::string &audit, std::uint64_t expected) const {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < deadline) {
			if (HasAuditCompletion(path, audit, options_.root) &&
			    !HasPendingAuditRequest(path, audit, options_.root)) {
				const auto observed = LogAuditBytes(path, audit, options_.root);
				if (observed == expected) return;
				if (observed > expected)
					throw std::runtime_error("per-query HTTP audit body exceeds the v3 cancellation profile");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		throw std::runtime_error("HTTP cancellation response did not reconcile with its v3 profile");
	}

	RunResult RunSessionScan(InteractiveDuckDB &session, const std::string &name, const std::string &query,
	                         const fs::path &log_path) {
		RunResult run;
		run.result_csv = options_.output / (name + ".csv");
		run.metrics_v3 = options_.output / (name + ".metrics.v3.csv");
		ClearLog(log_path);
		std::ostringstream sql;
		sql << "COPY (" << query << ") TO " << SqlLiteral(run.result_csv.string())
		    << " (FORMAT CSV, HEADER true);\n"
		    << "COPY (SELECT metrics FROM duckomo_last_scan_metrics()) TO "
		    << SqlLiteral(run.metrics_v3.string()) << " (FORMAT CSV, HEADER false);";
		session.SendUntilMarker(sql.str(), "DUCKOMO_DONE_" + name);
		Require(fs::is_regular_file(run.result_csv) && fs::is_regular_file(run.metrics_v3),
		        "same-connection scenario did not produce result and v3 profile files");
		run.metrics_json = StripCsvString(ReadText(run.metrics_v3));
		const auto json_path = options_.output / (name + ".metrics.v3.json");
		{
			std::ofstream output(json_path, std::ios::binary | std::ios::trunc);
			output << run.metrics_json << '\n';
		}
		const auto schema = RunProcess({"jq", "-er", ".schema_version == 3 and .scan_complete == true and .status == \"success\"",
		                               json_path.string()}, options_.root);
		Require(schema.exit_code == 0, "same-connection query did not publish a complete v3 profile");
		const auto body_bytes = V3Uint(run, "response_body_bytes");
		Require(V3Value(run, "transport_count_complete") == "true",
		        "same-connection remote transport count is incomplete");
		WaitForLogBytes(log_path, body_bytes);
		fs::copy_file(log_path, options_.output / (name + ".server.jsonl"), fs::copy_options::overwrite_existing);
		return run;
	}

	RunResult RunSessionFailure(InteractiveDuckDB &session, const std::string &name, const std::string &query,
	                           const fs::path &log_path, const std::string &audit) {
		RunResult run;
		run.result_csv = options_.output / (name + ".csv");
		run.metrics_v3 = options_.output / (name + ".metrics.v3.csv");
		ClearLog(log_path);
		std::ostringstream sql;
		sql << "COPY (" << query << ") TO " << SqlLiteral(run.result_csv.string())
		    << " (FORMAT CSV, HEADER true);\n"
		    << "COPY (SELECT metrics FROM duckomo_last_scan_metrics()) TO "
		    << SqlLiteral(run.metrics_v3.string()) << " (FORMAT CSV, HEADER false);";
		session.SendUntilMarker(sql.str(), "DUCKOMO_DONE_" + name);
		Require(fs::is_regular_file(run.metrics_v3), "revoked query did not preserve a v3 profile");
		run.metrics_json = StripCsvString(ReadText(run.metrics_v3));
		const auto json_path = options_.output / (name + ".metrics.v3.json");
		{
			std::ofstream output(json_path, std::ios::binary | std::ios::trunc);
			output << run.metrics_json << '\n';
		}
		const auto failed = RunProcess({"jq", "-er", ".schema_version == 3 and .status == \"failure\"",
		                               json_path.string()}, options_.root);
		Require(failed.exit_code == 0, "revoked query did not publish a failed v3 profile");
		const auto denied = RunProcess({"jq", "-er", ".response_statuses[\"403\"] > 0", json_path.string()},
		                              options_.root);
		Require(denied.exit_code == 0, "revoked query profile omitted its HTTP 403 response");
		const auto body_bytes = V3Uint(run, "response_body_bytes");
		WaitForAuditBytes(log_path, audit, body_bytes);
		fs::copy_file(log_path, options_.output / (name + ".server.jsonl"), fs::copy_options::overwrite_existing);
		return run;
	}

	void SetHttpFixtureState(const std::string &state_key, const std::string &object_name,
	                        const std::string &state, const std::string &replacement = {}) {
		std::string payload = "{\"state_key\":\"" + state_key + "\",\"object\":\"" +
		                     object_name + "\",\"state\":\"" + state + "\"";
		if (!replacement.empty()) payload += ",\"replacement\":\"" + replacement + "\"";
		payload += "}";
		static constexpr const char *python =
		    "import sys,urllib.request; r=urllib.request.Request(sys.argv[1],data=sys.argv[2].encode(),"
		    "headers={'Content-Type':'application/json'},method='POST'); "
		    "response=urllib.request.urlopen(r,timeout=5); "
		    "sys.exit(0 if response.status == 204 else 1)";
		const auto result = RunProcess({"python3", "-c", python, options_.http_base + "/__control", payload},
		                              options_.root);
		Require(result.exit_code == 0, "loopback HTTP fixture rejected a controlled object-state change");
	}

	InteractiveDuckDB StartSession(const std::string &name, bool s3, const fs::path &metrics_v3_sidecar = {},
	                              bool bail_on_error = true) {
		InteractiveDuckDB session(options_.duckdb, options_.root,
	                          {{"DUCKOMO_SCENARIO", name}, {"DUCKOMO_FIXTURE_ID", "dimensions_perf"},
	                           {"DUCKOMO_METRICS_V3_OUTPUT", metrics_v3_sidecar.string()}}, bail_on_error);
		std::ostringstream sql;
		sql << "LOAD " << SqlLiteral(options_.httpfs.string()) << ";\n"
		    << "LOAD " << SqlLiteral(options_.extension.string()) << ";\n";
		if (s3) sql << s3_setup_sql_ << '\n';
		sql << "SET threads=1; SET duckomo_max_threads=1; SET duckomo_cache_enabled=true; "
		       "SET duckomo_cache_capacity=67108864;";
		session.SendUntilMarker(sql.str(), "DUCKOMO_READY_" + name);
		return session;
	}

	void RunCacheScenarios(const std::string &axes) {
		const auto perf = options_.fixtures / "dimensions_perf.om";
		const auto restricted_query = [&](const std::string &input, std::uint64_t last_member) {
			return "SELECT value FROM read_om(" + SqlLiteral(input) + ", " + axes +
			       ") WHERE valid_time = TIMESTAMP '2026-01-01 00:00:00' AND member BETWEEN 100 AND " +
			       std::to_string(last_member) + " ORDER BY ALL";
		};
		const auto eviction_query = [&](const std::string &input) {
			return "SELECT value FROM read_om(" + SqlLiteral(input) + ", " + axes +
			       ") WHERE valid_time = TIMESTAMP '2026-01-01 00:00:00' AND member BETWEEN 0 AND 31 ORDER BY ALL";
		};
		const auto local_eviction_expected = RunScan("g5-local-small-capacity-reference", "dimensions_perf",
		                                             perf.string(), eviction_query(perf.string()), false, {}, true, 1, 1,
		                                             false);
		std::ostringstream summary;
		summary << "{\n  \"schema_version\":1,\n  \"gate\":\"G5\",\n  \"sources\":[";
		bool first_source = true;
		bool replacement_passed = false;
		bool revocation_passed = false;
		bool post_revocation_recovery_hit = false;
		std::uint64_t replacement_body_bytes = 0;
		std::uint64_t revoked_body_bytes = 0;
		for (const auto &source : std::vector<std::pair<std::string, std::string>>{
		         {"http", UriChild(options_.http_base, "dimensions_perf.om?audit=g5-http")},
		         {"s3", UriChild(options_.s3_base, "dimensions_perf.om")}}) {
			const bool is_s3 = source.first == "s3";
			const auto log_path = options_.server_log / (is_s3 ? "s3.jsonl" : "http.jsonl");
			auto session = StartSession("g5-" + source.first, is_s3, {}, is_s3);
			const auto cold = RunSessionScan(session, "g5-" + source.first + "-cold",
			                                restricted_query(source.second, 103), log_path);
			const auto hot = RunSessionScan(session, "g5-" + source.first + "-hot",
			                               restricted_query(source.second, 103), log_path);
			Compare(cold.result_csv, hot.result_csv, "g5-" + source.first + "-cold-hot");
			Require(V3Uint(hot, "cache.hits") > 0 && V3Uint(hot, "cache.hit_bytes") > 0,
			        "hot same-connection query did not reuse cached ranges for " + source.first);
			Require(V3Uint(hot, "response_body_bytes") < V3Uint(cold, "response_body_bytes"),
			        "hot same-connection query did not reduce audited response bytes for " + source.first);

			session.SendUntilMarker("SET duckomo_cache_enabled=false;", "DUCKOMO_DISABLED_" + source.first);
			const auto disabled = RunSessionScan(session, "g5-" + source.first + "-disabled",
			                                     restricted_query(source.second, 103), log_path);
			Compare(cold.result_csv, disabled.result_csv, "g5-" + source.first + "-cache-disabled");
			Require(V3Value(disabled, "cache.enabled") == "false" &&
				        V3Uint(disabled, "cache.charged_bytes") == 0,
			        "disabled cache retained entries or reported enabled for " + source.first);

			session.SendUntilMarker("SET duckomo_cache_enabled=true; SET duckomo_cache_capacity=67108864;",
			                       "DUCKOMO_ENABLED_" + source.first);
			const auto before_clear = RunSessionScan(session, "g5-" + source.first + "-before-clear",
			                                          restricted_query(source.second, 103), log_path);
			const auto clear_csv = options_.output / ("g5-" + source.first + "-clear.csv");
			session.SendUntilMarker("COPY (SELECT cleared_bytes FROM duckomo_clear_cache()) TO " +
			                           SqlLiteral(clear_csv.string()) + " (FORMAT CSV, HEADER false);",
			                       "DUCKOMO_CLEARED_" + source.first);
			const auto cleared_bytes = std::stoull(StripCsvString(ReadText(clear_csv)));
			Require(cleared_bytes > 0, "duckomo_clear_cache did not release charged entries for " + source.first);
			const auto after_clear_csv = options_.output / ("g5-" + source.first + "-after-clear.csv");
			session.SendUntilMarker("COPY (SELECT metrics FROM duckomo_last_scan_metrics()) TO " +
			                           SqlLiteral(after_clear_csv.string()) + " (FORMAT CSV, HEADER false);",
			                       "DUCKOMO_PROFILE_AFTER_CLEAR_" + source.first);
			Require(StripCsvString(ReadText(after_clear_csv)) == before_clear.metrics_json,
			        "cache clearing replaced the last scan snapshot for " + source.first);

			session.SendUntilMarker("SET duckomo_cache_capacity=16384;", "DUCKOMO_SMALL_CACHE_" + source.first);
			const auto small = RunSessionScan(session, "g5-" + source.first + "-small-capacity",
			                                  eviction_query(source.second), log_path);
			Compare(local_eviction_expected.result_csv, small.result_csv,
			        "g5-" + source.first + "-small-capacity");
			Require(V3Uint(small, "cache.charged_bytes") <= V3Uint(small, "cache.capacity_bytes") &&
				        V3Uint(small, "cache.evictions") > 0,
			        "small cache exceeded capacity or failed to evict ranges for " + source.first);

			if (!is_s3) {
				const auto weak_uri = UriChild(options_.http_base, "dimensions_perf.om?fault=weak-etag&audit=g5-weak");
				const auto weak_first = RunSessionScan(session, "g5-http-weak-first",
				                                      restricted_query(weak_uri, 103), log_path);
				const auto weak_second = RunSessionScan(session, "g5-http-weak-second",
				                                       restricted_query(weak_uri, 103), log_path);
				Compare(weak_first.result_csv, weak_second.result_csv, "g5-http-weak-version-result");
				Require(V3Value(weak_second, "cache.version_strength") == "weak" &&
					        V3Uint(weak_second, "cache.hits") == 0 &&
					        V3Uint(weak_second, "response_body_bytes") == V3Uint(weak_first, "response_body_bytes"),
				        "weak-version object was reused across queries");

				const std::string state_key = "g5-http-same-object";
				const auto controlled_uri = UriChild(options_.http_base,
				                                     "raw.om?state_key=" + state_key + "&audit=" + state_key);
				const auto object_query = "SELECT * FROM read_om(" + SqlLiteral(controlled_uri) + ") ORDER BY ALL";
				const auto raw_path = options_.fixtures / "raw.om";
				const auto replacement_path = options_.fixtures / "dimensions_axis_order.om";
				const auto raw_reference = RunScan("g5-http-replacement-original-reference", "raw", raw_path.string(),
				                                   "SELECT * FROM read_om(" + SqlLiteral(raw_path.string()) +
				                                       ") ORDER BY ALL", false, {});
				const auto replacement_reference = RunScan(
				    "g5-http-replacement-new-reference", "dimensions_axis_order", replacement_path.string(),
				    "SELECT * FROM read_om(" + SqlLiteral(replacement_path.string()) + ") ORDER BY ALL", false, {});
				Require(fs::file_size(raw_path) == fs::file_size(replacement_path) &&
				            raw_reference.result_csv != replacement_reference.result_csv,
				        "equal-length replacement fixtures must differ in their complete decoded results");

				SetHttpFixtureState(state_key, "raw.om", "normal");
				const auto original = RunSessionScan(session, "g5-http-same-uri-original", object_query, log_path);
				Compare(raw_reference.result_csv, original.result_csv, "g5-http-same-uri-original");

				SetHttpFixtureState(state_key, "raw.om", "replacement", "dimensions_axis_order.om");
				const auto replacement = RunSessionScan(session, "g5-http-same-uri-replacement", object_query, log_path);
				Compare(replacement_reference.result_csv, replacement.result_csv,
				        "g5-http-same-uri-equal-length-replacement");
				replacement_body_bytes = V3Uint(replacement, "response_body_bytes");
				Require(V3Uint(replacement, "cache.hits") == 0 && replacement_body_bytes > 0,
				        "equal-length replacement reused the prior object's cached ranges");
				replacement_passed = true;

				SetHttpFixtureState(state_key, "raw.om", "denied");
				const auto revoked = RunSessionFailure(session, "g5-http-revoked-cache-entry", object_query,
				                                       log_path, state_key);
				revoked_body_bytes = V3Uint(revoked, "response_body_bytes");
				Require(V3Uint(revoked, "cache.hits") == 0,
				        "revoked object access returned data from the session cache");
				revocation_passed = true;

				SetHttpFixtureState(state_key, "raw.om", "replacement", "dimensions_axis_order.om");
				const auto recovered = RunSessionScan(session, "g5-http-recovery-after-revocation",
				                                      object_query, log_path);
				Compare(replacement_reference.result_csv, recovered.result_csv,
				        "g5-http-recovery-after-revocation");
				post_revocation_recovery_hit = V3Uint(recovered, "cache.hits") > 0;
				Require(post_revocation_recovery_hit,
				        "restored authorization did not reuse the validated replacement version");
			}
			const auto finish_code = session.Finish();
			Require(finish_code == 0, "same-connection cache session exited unsuccessfully");
			if (!first_source) summary << ',';
			first_source = false;
			summary << "\n    {\"source\":\"" << source.first << "\",\"cold_body_bytes\":"
			        << V3Uint(cold, "response_body_bytes") << ",\"hot_body_bytes\":"
			        << V3Uint(hot, "response_body_bytes") << ",\"hot_cache_hit_bytes\":"
			        << V3Uint(hot, "cache.hit_bytes") << ",\"disabled_body_bytes\":"
			        << V3Uint(disabled, "response_body_bytes") << ",\"clear_released_bytes\":"
			        << cleared_bytes << ",\"small_capacity_bytes\":" << V3Uint(small, "cache.capacity_bytes")
			        << ",\"small_capacity_evictions\":" << V3Uint(small, "cache.evictions") << '}';
		}
		summary << "\n  ],\n  \"per_query_server_body_reconciled\":true,\n"
		        << "  \"weak_version_reuse_rejected\":true,\n"
		        << "  \"same_uri_equal_length_replacement_passed\":" << (replacement_passed ? "true" : "false")
		        << ",\n  \"replacement_response_body_bytes\":" << replacement_body_bytes
		        << ",\n  \"revoked_cached_object_denied\":" << (revocation_passed ? "true" : "false")
		        << ",\n  \"revoked_query_response_body_bytes\":" << revoked_body_bytes
		        << ",\n  \"post_revocation_recovery_cache_hit\":"
		        << (post_revocation_recovery_hit ? "true" : "false") << "\n}\n";
		std::ofstream output(options_.output / "g5-cache.json", std::ios::binary | std::ios::trunc);
		output << summary.str();
		Require(output.good(), "failed to write G5/G6 cache evidence");
	}

	void RunConcurrentAndCancellation(const std::string &axes, const std::function<std::string(const std::string &)> &full_query,
	                                 const fs::path &local_baseline, const fs::path &raw_baseline,
	                                 std::size_t rejected_faults) {
		const auto http_log = options_.server_log / "http.jsonl";
		ClearLog(http_log);
		const std::string first_audit = "g6-concurrent-a";
		const std::string second_audit = "g6-concurrent-b";
		const auto first_uri = UriChild(options_.http_base, "dimensions_perf.om?audit=" + first_audit);
		const auto second_uri = UriChild(options_.http_base, "dimensions_perf.om?audit=" + second_audit);
		auto first_future = std::async(std::launch::async, [&] {
			return RunScan("g6-concurrent-a", "dimensions_perf", first_uri, full_query(first_uri), false, {},
			               true, 2, 2, false);
		});
		auto second_future = std::async(std::launch::async, [&] {
			return RunScan("g6-concurrent-b", "dimensions_perf", second_uri, full_query(second_uri), false, {},
			               true, 2, 2, false);
		});
		const auto first = first_future.get();
		const auto second = second_future.get();
		Compare(local_baseline, first.result_csv, "g6-concurrent-http-a-result");
		Compare(local_baseline, second.result_csv, "g6-concurrent-http-b-result");
		WaitForAuditBytes(http_log, first_audit, V3Uint(first, "response_body_bytes"));
		WaitForAuditBytes(http_log, second_audit, V3Uint(second, "response_body_bytes"));
		fs::copy_file(http_log, options_.output / "g6-concurrent-http.server.jsonl",
		              fs::copy_options::overwrite_existing);

		const std::string cancellation_audit = "g6-cancel";
		const auto cancellation_uri = UriChild(options_.http_base,
		                                      "raw.om?fault=timeout&seconds=3&audit=" + cancellation_audit);
		const auto cancellation_profile_path = options_.output / "g6-cancel-queryend-v3.json";
		auto cancellation_session = StartSession("g6-cancel-http", false, cancellation_profile_path);
		ClearLog(http_log);
		const auto cancelled_output = options_.output / "g6-cancelled.csv";
		const auto cancellation_sql = "COPY (SELECT * FROM read_om(" + SqlLiteral(cancellation_uri) +
		                             ")) TO " + SqlLiteral(cancelled_output.string()) +
		                             " (FORMAT CSV, HEADER true);";
		auto cancellation_future = std::async(std::launch::async, [&] {
			try {
				cancellation_session.SendUntilMarker(cancellation_sql, "DUCKOMO_CANCELLED_QUERY_DONE");
				return false;
			} catch (const std::exception &) {
				return true;
			}
		});
		WaitForAuditRequest(http_log, cancellation_audit);
		cancellation_session.Interrupt();
		const auto query_interrupted = cancellation_future.get();
		const auto cancellation_exit = cancellation_session.Finish();
		(void)cancellation_exit;
		Require(query_interrupted && fs::is_regular_file(cancellation_profile_path),
		        "interrupted HTTP query did not finish with a QueryEnd v3 sidecar");
		const auto cancellation_profile = options_.output / "g6-cancelled-v3.json";
		const auto select_cancelled = RunProcess({"jq", "-cer", ".[0]", cancellation_profile_path.string()}, options_.root);
		Require(select_cancelled.exit_code == 0, "cancelled query v3 sidecar is malformed");
		{
			std::ofstream output(cancellation_profile, std::ios::binary | std::ios::trunc);
			output << select_cancelled.output;
		}
		const auto cancel_status = RunProcess({"jq", "-er",
		                                      ".schema_version == 3 and .status == \"cancelled\" and .scan_complete == false",
		                                      cancellation_profile.string()}, options_.root);
		Require(cancel_status.exit_code == 0, "interrupt did not preserve cancelled v3 terminal state");
		const auto cancel_body = std::stoull(Jq(cancellation_profile, ".response_body_bytes", options_.root));
		WaitForAuditBytes(http_log, cancellation_audit, cancel_body);
		fs::copy_file(http_log, options_.output / "g6-cancel-http.server.jsonl",
		              fs::copy_options::overwrite_existing);

		const auto recovery_uri = UriChild(options_.http_base, "raw.om");
		const auto recovery = RunScan("g6-recovery-after-cancel", "raw", recovery_uri,
		                              "SELECT * FROM read_om(" + SqlLiteral(recovery_uri) + ") ORDER BY ALL", false,
		                              http_log);
		Compare(raw_baseline, recovery.result_csv, "g6-recovery-after-cancel-result");
		Require(LogBytes(http_log, options_.root) == V3Uint(recovery, "response_body_bytes"),
		        "post-cancel recovery body did not reconcile with its per-query v3 profile");
		const auto perf_sha = Jq(options_.fixtures / "dimensions-perf-manifest.json", ".sha256", options_.root);
		std::ofstream evidence(options_.output / "g6-observation.json", std::ios::binary | std::ios::trunc);
		evidence << "{\n  \"schema_version\":1,\n  \"gate\":\"G6\",\n  \"status\":\"pass\",\n"
		        << "  \"fixture_sha256\":\"" << perf_sha << "\",\n"
		        << "  \"failed_protocol_query_profiles\":" << rejected_faults << ",\n"
		        << "  \"failed_protocol_costs_reconciled\":true,\n"
		        << "  \"concurrent_profiles\":[{\"audit\":\"" << first_audit << "\",\"body_bytes\":"
		        << V3Uint(first, "response_body_bytes") << "},{\"audit\":\"" << second_audit
		        << "\",\"body_bytes\":" << V3Uint(second, "response_body_bytes") << "}],\n"
		        << "  \"cancelled_profile_status\":\"cancelled\",\n"
		        << "  \"cancelled_profile_body_bytes\":" << cancel_body << ",\n"
		        << "  \"recovery_status\":\"" << V3Value(recovery, "status") << "\"\n}\n";
		Require(evidence.good(), "failed to write G6 observation evidence");
	}

	void RunGate() {
		const auto raw = options_.fixtures / "raw.om";
		const auto raw_expected = Jq(options_.fixtures / "manifest.json",
		                            ".fixtures[] | select(.fixture_id == \"raw\") | .sha256", options_.root);
		Require(Sha256(raw, options_.root) == raw_expected, "raw HTTP/S3 fixture differs from its fixed manifest hash");
		const auto local_raw = RunScan("g3-raw-local", "raw", raw.string(),
		                               "SELECT * FROM read_om(" + SqlLiteral(raw.string()) + ") ORDER BY ALL", false, {});
		const auto http_raw = RunScan("g3-raw-http", "raw", UriChild(options_.http_base, "raw.om"),
		                              "SELECT * FROM read_om(" + SqlLiteral(UriChild(options_.http_base, "raw.om")) +
		                                  ") ORDER BY ALL", false, options_.server_log / "http.jsonl");
		Compare(local_raw.result_csv, http_raw.result_csv, "raw-local-http");
		const auto http_bytes = LogBytes(options_.server_log / "http.jsonl", options_.root);
		Require(http_bytes == V3Uint(http_raw, "response_body_bytes"),
		        "HTTP response body bytes differ from the independent server log");

		const auto s3_raw_uri = UriChild(options_.s3_base, "raw.om");
		const auto s3_raw = RunScan("g3-raw-s3", "raw", s3_raw_uri,
		                           "SELECT * FROM read_om(" + SqlLiteral(s3_raw_uri) + ") ORDER BY ALL", true,
		                           options_.server_log / "s3.jsonl");
		Compare(local_raw.result_csv, s3_raw.result_csv, "raw-local-s3");
		const auto s3_bytes = LogBytes(options_.server_log / "s3.jsonl", options_.root);
		Require(s3_bytes == V3Uint(s3_raw, "response_body_bytes"),
		        "S3 response body bytes differ from the independent audit proxy");

		const auto real_expected = Jq(options_.real_manifest, ".sample.sha256", options_.root);
		const auto real_size = std::stoull(Jq(options_.real_manifest, ".sample.bytes", options_.root));
		Require(fs::file_size(options_.real_file) == real_size && Sha256(options_.real_file, options_.root) == real_expected,
		        "fixed real OM sample identity differs from its domain manifest");
		const auto domain_clause = ", domain := 'ncep_gfswave025'";
		const auto real_query = [&](const std::string &input) {
			return "SELECT * FROM read_om(" + SqlLiteral(input) + domain_clause + ") ORDER BY ALL";
		};
		const auto real_local = RunScan("g3-real-local", "ncep_gfswave025", options_.real_file.string(),
		                               real_query(options_.real_file.string()), false, {});
		const auto real_http_uri = UriChild(options_.http_base, "real.om");
		const auto real_http = RunScan("g3-real-http", "ncep_gfswave025", real_http_uri,
		                              real_query(real_http_uri), false, options_.server_log / "http.jsonl");
		Compare(real_local.result_csv, real_http.result_csv, "real-local-http");
		const auto real_s3_uri = UriChild(options_.s3_base, "real.om");
		const auto real_s3 = RunScan("g3-real-s3", "ncep_gfswave025", real_s3_uri,
		                            real_query(real_s3_uri), true, options_.server_log / "s3.jsonl");
		Compare(real_local.result_csv, real_s3.result_csv, "real-local-s3");
		Require(LogBytes(options_.server_log / "http.jsonl", options_.root) ==
		            V3Uint(real_http, "response_body_bytes"),
		        "real HTTP body bytes differ from its server audit log");
		Require(LogBytes(options_.server_log / "s3.jsonl", options_.root) ==
		            V3Uint(real_s3, "response_body_bytes"),
		        "real S3 body bytes differ from its server audit log");

		const auto perf = options_.fixtures / "dimensions_perf.om";
		const auto perf_sha = Jq(options_.fixtures / "dimensions-perf-manifest.json", ".sha256", options_.root);
		Require(Sha256(perf, options_.root) == perf_sha, "performance fixture differs from its fixed manifest hash");
		const auto axes = "dimensions := map(['value'], [['time','member']]), axes := {"
		                  "'time': {'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
		                  "'member': {'axis':'member','start':0,'step':1}}";
		const auto full_query = [&](const std::string &input) {
			return "SELECT value FROM read_om(" + SqlLiteral(input) + ", " + axes + ") ORDER BY ALL";
		};
		const auto local_query = [&](const std::string &input) {
			return "SELECT value FROM read_om(" + SqlLiteral(input) + ", " + axes +
			       ") WHERE valid_time = TIMESTAMP '2026-01-01 00:00:00' AND member BETWEEN 100 AND 103 ORDER BY ALL";
		};
		std::map<std::string, fs::path> perf_baselines;
		for (const auto &source : std::vector<std::pair<std::string, std::string>>{
		         {"local", perf.string()}, {"http", UriChild(options_.http_base, "dimensions_perf.om")},
		         {"s3", UriChild(options_.s3_base, "dimensions_perf.om")}}) {
			const bool is_s3 = source.first == "s3";
			const auto full = RunScan("g3-perf-" + source.first + "-full", "dimensions_perf", source.second,
			                          full_query(source.second), is_s3,
			                          source.first == "http" ? options_.server_log / "http.jsonl" :
			                          (is_s3 ? options_.server_log / "s3.jsonl" : fs::path()));
			const auto restricted = RunScan("g3-perf-" + source.first + "-restricted", "dimensions_perf",
			                                source.second, local_query(source.second), is_s3,
			                                source.first == "http" ? options_.server_log / "http.jsonl" :
			                                (is_s3 ? options_.server_log / "s3.jsonl" : fs::path()));
			const auto full_data = MetricUint(full.metrics_v2, "([.variables[].data_bytes] | add // 0)");
			const auto local_data = MetricUint(restricted.metrics_v2, "([.variables[].data_bytes] | add // 0)");
			const auto full_decode = MetricUint(full.metrics_v2, "([.variables[].decoded_chunks] | add // 0)");
			const auto local_decode = MetricUint(restricted.metrics_v2, "([.variables[].decoded_chunks] | add // 0)");
			Require(local_data < full_data && local_decode < full_decode,
			        "fixed remote/local restricted query did not lower both value bytes and decode chunks");
			perf_baselines[source.first] = full.result_csv;
			if (source.first != "local") {
				const auto log_path = options_.server_log / (is_s3 ? "s3.jsonl" : "http.jsonl");
				const auto body = LogBytes(log_path, options_.root);
				Require(body == V3Uint(restricted, "response_body_bytes"),
				        "restricted scan response body differs from its server audit log");
				Require(V3Uint(restricted, "response_body_bytes") < V3Uint(full, "response_body_bytes"),
				        "restricted remote query did not lower cold response body bytes");
			}
		}

		const std::vector<std::string> fault_names = {"403", "404", "no-head", "ignore-range", "wrong-range",
		                                              "short", "timeout", "change-version", "replace"};
		std::size_t rejected_fault_profiles = 0;
		for (const auto &fault : fault_names) {
			const auto uri = UriChild(options_.http_base, "raw.om?fault=" + fault);
			const auto rejected = RunScan("g3-fault-" + fault, "raw", uri,
			                              "SELECT * FROM read_om(" + SqlLiteral(uri) + ")", false,
			                              options_.server_log / "http.jsonl", false);
			Require(V3Value(rejected, "status") == "failure",
			        "injected HTTP failure did not publish a failure v3 profile");
			Require(LogBytes(options_.server_log / "http.jsonl", options_.root) ==
			            V3Uint(rejected, "response_body_bytes"),
			        "failed HTTP body bytes differ from the QueryEnd v3 profile for " + fault);
			++rejected_fault_profiles;
			const auto recovery_uri = UriChild(options_.http_base, "raw.om");
			const auto recovered = RunScan("g3-recovery-" + fault, "raw", recovery_uri,
			                               "SELECT * FROM read_om(" + SqlLiteral(recovery_uri) + ") ORDER BY ALL",
			                               false, options_.server_log / "http.jsonl");
			Compare(local_raw.result_csv, recovered.result_csv, "recovery-after-" + fault);
			Require(LogBytes(options_.server_log / "http.jsonl", options_.root) ==
			            V3Uint(recovered, "response_body_bytes"),
			        "recovery request body bytes differ from its HTTP server audit log");
		}
		std::ofstream summary(options_.output / "summary.json", std::ios::binary | std::ios::trunc);
		Require(summary.good(), "cannot write G3 validation summary");
		summary << "{\n  \"schema_version\": 1,\n  \"gate\": \"G3\",\n  \"status\": \"pass\",\n"
		        << "  \"comparisons\": [";
		for (std::size_t i = 0; i < comparisons_.size(); i++) {
			if (i) summary << ',';
			summary << '"' << comparisons_[i] << '"';
		}
		summary << "],\n  \"http_fault_cases_rejected\": " << rejected_fault_profiles
		        << ",\n  \"server_audit\": true,\n  \"real_sample_sha256\": \"" << real_expected << "\"\n}\n";
		std::ofstream comparison(options_.output / "server-comparison.json", std::ios::binary | std::ios::trunc);
		comparison << "{\"raw_http\":{\"server_body_bytes\":" << http_bytes << ",\"profile_body_bytes\":"
		           << V3Uint(http_raw, "response_body_bytes") << "},\"raw_s3\":{\"server_body_bytes\":"
		           << s3_bytes << ",\"profile_body_bytes\":" << V3Uint(s3_raw, "response_body_bytes") << "}}\n";
		Require(summary.good() && comparison.good(), "failed to finish G3 evidence");

		struct TimingSample final {
			std::string source;
			std::uint64_t thread_limit;
			std::uint64_t repetition;
			std::uint64_t order_in_repetition;
			double elapsed_ms;
			std::uint64_t peak_rss_bytes;
			std::uint64_t workers;
		};
		std::vector<TimingSample> timing_samples;
		std::map<std::pair<std::string, std::uint64_t>, std::vector<double>> timings;
		std::map<std::string, bool> source_parallel_workers;
		std::map<std::string, bool> source_parallel_median_wins;
		for (const auto &source : std::vector<std::pair<std::string, std::string>>{
		         {"local", perf.string()}, {"http", UriChild(options_.http_base, "dimensions_perf.om")},
		         {"s3", UriChild(options_.s3_base, "dimensions_perf.om")}}) {
			const bool is_s3 = source.first == "s3";
			const fs::path log_path = source.first == "http" ? options_.server_log / "http.jsonl" :
			                         (is_s3 ? options_.server_log / "s3.jsonl" : fs::path());
			source_parallel_workers[source.first] = true;
			for (std::uint64_t repetition = 1; repetition <= 5; repetition++) {
				std::array<std::uint64_t, 3> thread_schedule {{1, 2, 4}};
				if (repetition % 2 == 0) std::reverse(thread_schedule.begin(), thread_schedule.end());
				for (std::size_t order = 0; order < thread_schedule.size(); order++) {
					const auto thread_limit = thread_schedule[order];
					const auto name = "g4-" + source.first + "-threads-" + std::to_string(thread_limit) +
					                 "-run-" + std::to_string(repetition);
					const auto run = RunScan(name, "dimensions_perf", source.second, full_query(source.second),
					                         is_s3, log_path, true, thread_limit, thread_limit, false);
					Compare(perf_baselines.at(source.first), run.result_csv, name + "-complete-result");
					const auto workers = V3Uint(run, "tasks.max_active_workers");
					Require(V3Value(run, "scan_complete") == "true",
					        "G4 timing run did not consume the full fixed fixture");
					if (workers == 0 || workers > thread_limit || (thread_limit > 1 && workers < 2))
						source_parallel_workers[source.first] = false;
					if (!log_path.empty()) {
						Require(LogBytes(log_path, options_.root) == V3Uint(run, "response_body_bytes"),
						        "G4 remote response body differs from its independent audit log for " + name);
					}
					timings[{source.first, thread_limit}].push_back(run.elapsed_ms);
					timing_samples.push_back({source.first, thread_limit, repetition,
					                         static_cast<std::uint64_t>(order + 1), run.elapsed_ms,
					                         run.peak_rss_bytes, workers});
				}
			}
		}
		for (const auto &source : source_parallel_workers) {
			const auto serial_median = Median(timings.at({source.first, 1}));
			const auto two_median = Median(timings.at({source.first, 2}));
			const auto four_median = Median(timings.at({source.first, 4}));
			source_parallel_median_wins[source.first] = two_median < serial_median || four_median < serial_median;
		}
		bool g4_passed = true;
		for (const auto &source : source_parallel_workers) {
			g4_passed = g4_passed && source.second && source_parallel_median_wins.at(source.first);
		}
		std::ofstream performance(options_.output / "g4-performance.json", std::ios::binary | std::ios::trunc);
		Require(performance.good(), "cannot write G4 timing evidence");
		performance << "{\n  \"schema_version\":1,\n  \"gate\":\"G4\",\n  \"status\":\""
		            << (g4_passed ? "pass" : "fail") << "\",\n  \"fixture_sha256\":\""
		            << perf_sha << "\",\n  \"cache_policy\":\"disabled for all runs\",\n"
		               "  \"measurement_order\":\"1,2,4 on odd repetitions; 4,2,1 on even repetitions\",\n"
		               "  \"runs\":[";
		for (std::size_t index = 0; index < timing_samples.size(); index++) {
			const auto &sample = timing_samples[index];
			if (index) performance << ',';
			performance << "\n    {\"source\":\"" << sample.source << "\",\"thread_limit\":"
			            << sample.thread_limit << ",\"repetition\":" << sample.repetition
			            << ",\"order_in_repetition\":" << sample.order_in_repetition
			            << ",\"elapsed_ms\":" << std::setprecision(17) << sample.elapsed_ms
			            << ",\"peak_rss_bytes\":" << sample.peak_rss_bytes
			            << ",\"memory_scope\":\"process\",\"max_active_workers\":" << sample.workers << '}';
		}
		performance << "\n  ],\n  \"medians_ms\":{";
		bool first_median = true;
		for (const auto &source : source_parallel_workers) {
			for (const auto thread_limit : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(4)}) {
				if (!first_median) performance << ',';
				first_median = false;
				performance << "\n    \"" << source.first << "_threads_" << thread_limit << "\":"
				            << std::setprecision(17) << Median(timings.at({source.first, thread_limit}));
			}
		}
		performance << "\n  },\n  \"sources\":{";
		bool first_source_result = true;
		for (const auto &source : source_parallel_workers) {
			if (!first_source_result) performance << ',';
			first_source_result = false;
			performance << "\n    \"" << source.first << "\":{\"multiple_workers\":"
			            << (source.second ? "true" : "false") << ",\"parallel_median_lower\":"
			            << (source_parallel_median_wins.at(source.first) ? "true" : "false") << '}';
		}
		performance << "\n  },\n  \"performance_gate_passed\":" << (g4_passed ? "true" : "false") << "\n}\n";
		Require(performance.good(), "failed to finish G4 timing evidence");
		RunCacheScenarios(axes);
		RunConcurrentAndCancellation(axes, full_query, perf_baselines.at("local"), local_raw.result_csv,
		                             rejected_fault_profiles);
		Require(g4_passed,
		        "G4 requires multiple workers and a lower two- or four-worker median for local, HTTP, and S3");

	}

private:
	Options options_;
	fs::path private_sql_;
	std::string s3_setup_sql_;
	std::vector<std::string> comparisons_;
};

Options CompleteOptions(Options options) {
	for (const auto &path : {options.fixtures / "raw.om", options.fixtures / "dimensions_perf.om",
	                         options.fixtures / "manifest.json", options.fixtures / "dimensions-perf-manifest.json",
	                         options.duckdb, options.extension, options.httpfs, options.s3_setup,
	                         options.real_file, options.real_manifest}) {
		Require(fs::is_regular_file(path), "required G3 input is missing: " + path.string());
	}
	Require(fs::is_directory(options.server_log), "required HTTP/S3 server log directory is missing");
	Require(fs::is_regular_file(options.server_log / "http.jsonl") &&
	            fs::is_regular_file(options.server_log / "s3.jsonl"),
	        "required HTTP and S3 audit logs are missing");
	if (fs::exists(options.output)) {
		Require(fs::is_directory(options.output), "remote evidence output path is not a directory");
		Require(fs::directory_iterator(options.output) == fs::directory_iterator(),
		        "remote evidence output directory must be empty for an independent run");
	} else {
		fs::create_directories(options.output);
	}
	return options;
}

} // namespace

int main(int argc, char **argv) {
	try {
		auto options = CompleteOptions(ParseOptions(argc, argv));
		RemoteHarness harness(std::move(options));
		harness.RunGate();
		std::cout << "duckomo_remote_validation: G3 source parity, body audit, cost, and protocol faults passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "duckomo_remote_validation: " << error.what() << '\n';
		return 1;
	}
}
