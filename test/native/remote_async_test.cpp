#include "duckdb.hpp"
#include "duckdb_test_compat.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/common/file_opener.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/exception/http_exception.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/remote_file.hpp"
#include "duckomo/om_reader.hpp"
#include "duckomo/reader.hpp"
#include "duckomo_extension.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if DUCKOMO_DUCKDB_API_GENERATION >= 2

namespace duckdb {
DUCKOMO_TEST_LOAD_ALL_EXTENSIONS(db) { db.LoadStaticExtension<DuckomoExtension>(); }
}

namespace {
using namespace duckdb;
using namespace duckdb::duckomo;

void Require(bool value, const char *why) {
	if (!value) throw std::runtime_error(why);
}
void Require(bool value, const std::string &why) {
	if (!value) throw std::runtime_error(why);
}

enum class ErrorInjection {
	None,
	Http403,
	Http404,
	ShortRead,
	Corrupted
};

struct AsyncState {
	std::atomic<int> opens{0};
	std::atomic<int> query_opens{0};
	std::atomic<int> closed{0};
	std::atomic<bool> changed_on_second_open{false};
	std::atomic<ErrorInjection> error{ErrorInjection::None};
	std::atomic<int> active_concurrent_reads{0};
	std::atomic<int> max_concurrent_reads{0};
	std::atomic<uint64_t> total_reads{0};
	std::atomic<uint64_t> total_bytes_read{0};
	std::atomic<int> delay_ms{0};
};

struct AsyncHandle final : FileHandle {
	AsyncHandle(FileSystem &fs, const std::string &path, FileOpenFlags flags, AsyncState &state)
	    : FileHandle(fs, path, flags), state(state) {}
	~AsyncHandle() override { Close(); }
	void Close() override {
		if (!closed) {
			++state.closed;
			closed = true;
		}
	}
	AsyncState &state;
	bool closed = false;
};

class AsyncTestFileSystem final : public FileSystem {
public:
	explicit AsyncTestFileSystem(const std::string &fixture_path, AsyncState &state)
	    : state_(state) {
		std::ifstream input(fixture_path, std::ios::binary);
		Require(input.good(), "async fixture available: " + fixture_path);
		bytes_.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	}

	bool CanHandleFile(const string &path) override {
		return path.rfind("https://duckomo-async/", 0) == 0;
	}

	string GetName() const override {
		return "duckomo-async-test-fs";
	}

	unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags, optional_ptr<FileOpener>) override {
		++state_.opens;
		++state_.query_opens;
		const auto error = state_.error.load();
		if (error == ErrorInjection::Http403) {
			const std::unordered_map<std::string, std::string> headers = {{"Authorization", "secret_auth_token"}};
			throw HTTPException(403, "access forbidden body token=secret_auth_token", headers, "Forbidden", "https://duckomo-async/secret_auth_token");
		}
		if (error == ErrorInjection::Http404) {
			const std::unordered_map<std::string, std::string> headers = {{"Authorization", "secret_auth_token"}};
			throw HTTPException(404, "not found body token=secret_auth_token", headers, "Not Found", "https://duckomo-async/secret_auth_token");
		}
		return make_uniq<AsyncHandle>(*this, path, flags, state_);
	}

	int64_t GetFileSize(FileHandle &) override {
		return static_cast<int64_t>(bytes_.size());
	}

	string GetVersionTag(FileHandle &) override {
		return (state_.changed_on_second_open.load() && state_.query_opens > 1) ? "v2_changed" : "v1_original";
	}

	void Read(FileHandle &, void *destination, int64_t count, idx_t offset) override {
		const auto error = state_.error.load();
		if (error == ErrorInjection::Http403) {
			const std::unordered_map<std::string, std::string> headers = {{"Authorization", "secret_auth_token"}};
			throw HTTPException(403, "access forbidden in read body token=secret_auth_token", headers, "Forbidden", "https://duckomo-async/secret_auth_token");
		}
		if (error == ErrorInjection::Http404) {
			const std::unordered_map<std::string, std::string> headers = {{"Authorization", "secret_auth_token"}};
			throw HTTPException(404, "not found in read body token=secret_auth_token", headers, "Not Found", "https://duckomo-async/secret_auth_token");
		}
		if (error == ErrorInjection::ShortRead) {
			throw IOException("connection reset by peer token=secret_auth_token");
		}
		if (error == ErrorInjection::Corrupted) {
			if (count > 0 && destination) {
				std::memset(destination, 0xAA, static_cast<size_t>(count));
			}
			return;
		}

		if (offset > bytes_.size() || count < 0 || static_cast<uint64_t>(count) > bytes_.size() - offset) {
			std::cerr << "AsyncTestFileSystem::Read out of bounds: offset=" << offset
			          << " count=" << count << " bytes_size=" << bytes_.size()
			          << " opens=" << state_.opens.load() << std::endl;
		}
		Require(offset <= bytes_.size() && count >= 0 && static_cast<uint64_t>(count) <= bytes_.size() - offset,
		        "async fixture read is bounded");

		const auto delay = state_.delay_ms.load();
		if (delay > 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}

		const auto current_active = ++state_.active_concurrent_reads;
		auto current_max = state_.max_concurrent_reads.load();
		while (current_active > current_max && !state_.max_concurrent_reads.compare_exchange_weak(current_max, current_active)) {
		}

		// Small delay during read to allow task overlap detection
		std::this_thread::sleep_for(std::chrono::milliseconds(2));

		std::memcpy(destination, bytes_.data() + offset, static_cast<size_t>(count));
		--state_.active_concurrent_reads;
		++state_.total_reads;
		state_.total_bytes_read += static_cast<uint64_t>(count);
	}

	void Reset() {
		state_.active_concurrent_reads = 0;
		state_.max_concurrent_reads = 0;
		state_.total_reads = 0;
		state_.total_bytes_read = 0;
		state_.error = ErrorInjection::None;
		state_.changed_on_second_open = false;
		state_.query_opens = 0;
		state_.delay_ms = 0;
	}

private:
	AsyncState &state_;
	std::vector<char> bytes_;
};

std::string ExtractRemoteIoString(const std::string &json, const std::string &key) {
	auto remote_pos = json.find("\"remote_io\":");
	if (remote_pos == std::string::npos) throw std::runtime_error("missing remote_io in metrics JSON: " + json);
	const std::string target = "\"" + key + "\":";
	auto pos = json.find(target, remote_pos);
	if (pos == std::string::npos) throw std::runtime_error("missing key in remote_io: " + key + " in: " + json);
	pos += target.size();
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
	if (pos >= json.size()) throw std::runtime_error("empty value in remote_io for: " + key);
	if (json[pos] == '"') {
		auto end_quote = json.find('"', pos + 1);
		if (end_quote == std::string::npos) throw std::runtime_error("unterminated string for: " + key);
		return json.substr(pos + 1, end_quote - pos - 1);
	}
	auto end_pos = json.find_first_of(",}", pos);
	return json.substr(pos, end_pos - pos);
}

std::uint64_t ExtractRemoteIoUint(const std::string &json, const std::string &key) {
	auto s = ExtractRemoteIoString(json, key);
	return std::stoull(s);
}

bool ExtractRemoteIoBool(const std::string &json, const std::string &key) {
	auto s = ExtractRemoteIoString(json, key);
	return s == "true";
}

void TestAsyncOnOffMatches(Connection &conn, AsyncTestFileSystem &fs, AsyncState &state) {
	const std::string uri = "'https://duckomo-async/projection.om'";
	const std::string query =
	    "SELECT count(*), avg(humidity), avg(pressure), avg(temperature) FROM read_om(" + uri +
	    ", dimensions := map(['humidity','pressure','temperature'], "
	    "[['row','column'],['row','column'],['row','column']]))";

	// 1. Run sync: duckomo_async_io = false
	Require(!conn.Query("SET duckomo_async_io = false")->HasError(), "disable async succeeds");
	Require(!conn.Query("SET duckomo_remote_merge = true")->HasError(), "enable merge succeeds");
	fs.Reset();

	auto sync_result = duckdb::duckomo_test::Rows(conn.Query(query));
	Require(sync_result && !sync_result->HasError() && sync_result->RowCount() == 1,
	        "sync query succeeds");
	const auto sync_count = sync_result->GetValue(0, 0).ToString();
	const auto sync_humidity = sync_result->GetValue(1, 0).ToString();
	const auto sync_pressure = sync_result->GetValue(2, 0).ToString();
	const auto sync_temperature = sync_result->GetValue(3, 0).ToString();

	auto sync_metrics_result = duckdb::duckomo_test::Rows(conn.Query("SELECT metrics FROM duckomo_last_scan_metrics()"));
	Require(sync_metrics_result && sync_metrics_result->RowCount() == 1, "sync metrics available");
	const auto sync_metrics = sync_metrics_result->GetValue(0, 0).ToString();
	Require(ExtractRemoteIoBool(sync_metrics, "async_applied") == false, "sync metrics reports async_applied=false");

	// Keep one scan worker and enough executor threads to expose parallel I/O.
	Require(!conn.Query("SET threads = 4")->HasError(), "set async executor threads");
	// 2. Run async: duckomo_async_io = true
	Require(!conn.Query("SET duckomo_async_io = true")->HasError(), "enable async succeeds");
	fs.Reset();
	state.delay_ms = 2;

	auto async_result = duckdb::duckomo_test::Rows(conn.Query(query));
	Require(async_result && !async_result->HasError() && async_result->RowCount() == 1,
	        "async query succeeds");
	const auto async_count = async_result->GetValue(0, 0).ToString();
	const auto async_humidity = async_result->GetValue(1, 0).ToString();
	const auto async_pressure = async_result->GetValue(2, 0).ToString();
	const auto async_temperature = async_result->GetValue(3, 0).ToString();

	Require(sync_count == async_count, "row count matches between sync and async");
	Require(sync_humidity == async_humidity, "humidity matches between sync and async");
	Require(sync_pressure == async_pressure, "pressure matches between sync and async");
	Require(sync_temperature == async_temperature, "temperature matches between sync and async");

	auto async_metrics_result = duckdb::duckomo_test::Rows(conn.Query("SELECT metrics FROM duckomo_last_scan_metrics()"));
	Require(async_metrics_result && async_metrics_result->RowCount() == 1, "async metrics available");
	const auto async_metrics = async_metrics_result->GetValue(0, 0).ToString();
	Require(ExtractRemoteIoBool(async_metrics, "async_applied") == true, "async metrics reports async_applied=true");
	Require(ExtractRemoteIoUint(async_metrics, "inflight_peak") > 1, "async scan has concurrent requests");
	Require(state.max_concurrent_reads.load() > 1, "filesystem observes concurrent requests");
	Require(state.active_concurrent_reads.load() == 0, "no reads survive query completion");
	Require(ExtractRemoteIoUint(async_metrics, "inflight_budget_bytes") > 0, "inflight_budget_bytes is recorded and > 0");

	std::cout << "  [PASS] Async on/off results match bit-for-bit (peak inflight="
	          << ExtractRemoteIoUint(async_metrics, "inflight_peak")
	          << ", budget=" << ExtractRemoteIoUint(async_metrics, "inflight_budget_bytes")
	          << ", max concurrent=" << state.max_concurrent_reads.load() << ")" << std::endl;
}

void TestErrorHandlingAndRedaction(Connection &conn, AsyncTestFileSystem &fs, AsyncState &state) {
	const std::string uri = "'https://duckomo-async/projection.om?token=super_secret_auth_token'";
	const std::string query =
	    "SELECT avg(humidity) FROM read_om(" + uri +
	    ", dimensions := map(['humidity','pressure','temperature'], "
	    "[['row','column'],['row','column'],['row','column']]))";

	Require(!conn.Query("SET duckomo_async_io = true")->HasError(), "enable async succeeds");

	// A. 403 Forbidden
	fs.Reset();
	state.error = ErrorInjection::Http403;
	auto res_403 = conn.Query(query);
	Require(res_403->HasError(), "403 query fails as expected");
	const auto msg_403 = res_403->GetError();
	Require(msg_403.find("[access_denied]") != std::string::npos, "403 classified as access_denied: " + msg_403);
	Require(msg_403.find("(HTTP 403)") != std::string::npos, "403 status code preserved: " + msg_403);
	Require(msg_403.find("transport details redacted") != std::string::npos, "403 has redacted details: " + msg_403);
	Require(msg_403.find("super_secret_auth_token") == std::string::npos, "403 does not leak secret: " + msg_403);

	// B. 404 Not Found
	fs.Reset();
	state.error = ErrorInjection::Http404;
	auto res_404 = conn.Query(query);
	Require(res_404->HasError(), "404 query fails as expected");
	const auto msg_404 = res_404->GetError();
	Require(msg_404.find("[object_not_found]") != std::string::npos, "404 classified as object_not_found: " + msg_404);
	Require(msg_404.find("(HTTP 404)") != std::string::npos, "404 status code preserved: " + msg_404);
	Require(msg_404.find("transport details redacted") != std::string::npos, "404 has redacted details: " + msg_404);
	Require(msg_404.find("super_secret_auth_token") == std::string::npos, "404 does not leak secret: " + msg_404);

	// C. Short read / connection reset
	fs.Reset();
	state.error = ErrorInjection::ShortRead;
	auto res_short = conn.Query(query);
	Require(res_short->HasError(), "short read query fails as expected");
	const auto msg_short = res_short->GetError();
	Require(msg_short.find("[transport]") != std::string::npos, "short read classified as transport: " + msg_short);
	Require(msg_short.find("transport details redacted") != std::string::npos, "short read redacted: " + msg_short);
	Require(msg_short.find("super_secret_auth_token") == std::string::npos, "short read does not leak secret: " + msg_short);

	// D. Object identity change
	fs.Reset();
	state.changed_on_second_open = true;
	auto res_changed = conn.Query(query);
	Require(res_changed->HasError(), "changed object query fails as expected");
	const auto msg_changed = res_changed->GetError();
	Require(msg_changed.find("object identity changed") != std::string::npos, "changed object error: " + msg_changed);

	std::cout << "  [PASS] Async error classification and token redaction verified for 403, 404, short read, version change" << std::endl;
}

void TestAsyncCancellationAndRecovery(Connection &conn, AsyncTestFileSystem &fs, AsyncState &state) {
	const std::string uri = "'https://duckomo-async/projection.om'";
	const std::string query =
	    "SELECT count(*), avg(humidity), avg(pressure), avg(temperature) FROM read_om(" + uri +
	    ", dimensions := map(['humidity','pressure','temperature'], "
	    "[['row','column'],['row','column'],['row','column']]))";

	Require(!conn.Query("SET duckomo_async_io = true")->HasError(), "enable async succeeds");
	fs.Reset();
	// Inject significant delay per read to allow interrupt
	state.delay_ms = 50;

	std::thread interrupter([&]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		conn.Interrupt();
	});

	const auto start = std::chrono::steady_clock::now();
	auto result = conn.Query(query);
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

	if (interrupter.joinable()) interrupter.join();

	Require(result->HasError(), "cancelled query returns error");
	Require(state.active_concurrent_reads.load() == 0, "no reads survive cancellation");
	Require(elapsed < 1000, "cancellation completes within 1 second (elapsed=" + std::to_string(elapsed) + "ms)");

	auto metrics_result = duckdb::duckomo_test::Rows(conn.Query("SELECT metrics FROM duckomo_last_scan_metrics()"));
	Require(metrics_result && metrics_result->RowCount() == 1, "metrics available after cancellation");
	const auto metrics = metrics_result->GetValue(0, 0).ToString();
	Require(metrics.find("\"status\":\"cancelled\"") != std::string::npos, "metrics status is cancelled: " + metrics);

	std::cout << "  [PASS] Cancellation finishes in " << elapsed << "ms with status='cancelled'" << std::endl;

	// Same connection recovery
	fs.Reset();
	state.delay_ms = 0;
	auto recovery_result = duckdb::duckomo_test::Rows(conn.Query(query));
	Require(recovery_result && !recovery_result->HasError() && recovery_result->RowCount() == 1,
	        "same-connection query succeeds after cancellation");

	std::cout << "  [PASS] Same-connection query succeeds after cancellation recovery" << std::endl;
}

void TestSingleVariablePrefetch(Connection &conn, AsyncTestFileSystem &fs, AsyncState &state) {
	const std::string query = "SELECT count(*),sum(humidity) FROM read_om('https://duckomo-async/projection.om', "
	    "dimensions := map(['humidity','pressure','temperature'], [['row','column'],['row','column'],['row','column']]))";
	Require(!conn.Query("SET threads=4; SET duckomo_max_threads=1; SET duckomo_async_prefetch_batches=4; SET duckomo_async_io=false")->HasError(), "single worker settings");
	auto baseline = duckdb::duckomo_test::Rows(conn.Query(query));
	Require(baseline && !baseline->HasError(), "single variable baseline");
	const auto expected = baseline->GetValue(1, 0).ToString();
	fs.Reset();
	state.delay_ms = 2;
	Require(!conn.Query("SET duckomo_async_io=true")->HasError(), "enable window");
	auto result = duckdb::duckomo_test::Rows(conn.Query(query));
	Require(result && !result->HasError(), "single variable window query");
	Require(result->GetValue(0, 0).ToString() == "10541" && result->GetValue(1, 0).ToString() == expected,
	        "window preserves cardinality and values across output batches");
	Require(state.max_concurrent_reads > 1, "single variable prefetch exposes concurrent reads");
	Require(state.active_concurrent_reads == 0, "window drained at query end");
	fs.Reset();
	state.delay_ms = 20;
	std::thread interrupter([&]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		conn.Interrupt();
	});
	auto cancelled = conn.Query(query);
	interrupter.join();
	Require(cancelled->HasError(), "prefetch window cancellation returns error");
	Require(state.active_concurrent_reads == 0, "prefetch window drains on cancellation");
	fs.Reset();
	Require(!conn.Query(query)->HasError(), "prefetch window recovers after cancellation");
	Require(!conn.Query("RESET duckomo_max_threads; RESET duckomo_async_prefetch_batches")->HasError(), "reset scan workers and window");
	std::cout << "  [PASS] Single-variable cross-batch window preserves results with concurrent reads="
	          << state.max_concurrent_reads.load() << std::endl;
}

void TestMultiWorkerInflightBounds(Connection &conn, AsyncTestFileSystem &fs, AsyncState &state) {
	const std::string uri = "'https://duckomo-async/projection.om'";
	const std::string query =
	    "SELECT count(*), avg(humidity), avg(pressure), avg(temperature) FROM read_om(" + uri +
	    ", dimensions := map(['humidity','pressure','temperature'], "
	    "[['row','column'],['row','column'],['row','column']]))";

	Require(!conn.Query("SET duckomo_async_io = true")->HasError(), "enable async succeeds");
	Require(!conn.Query("SET duckomo_async_max_inflight = 4")->HasError(), "set max inflight succeeds");

	for (int threads : {2, 4, 8}) {
		Require(!conn.Query("SET threads = " + std::to_string(threads))->HasError(), "set threads");
		fs.Reset();

		auto res = duckdb::duckomo_test::Rows(conn.Query(query));
		Require(res && !res->HasError() && res->RowCount() == 1, "multi-worker scan succeeds");
		Require(res->GetValue(0, 0).ToString() == "10541", "correct row count across workers");

		auto metrics_result = duckdb::duckomo_test::Rows(conn.Query("SELECT metrics FROM duckomo_last_scan_metrics()"));
		Require(metrics_result && metrics_result->RowCount() == 1, "metrics available");
		const auto metrics = metrics_result->GetValue(0, 0).ToString();
		const auto peak = ExtractRemoteIoUint(metrics, "inflight_peak");
		const auto budget = ExtractRemoteIoUint(metrics, "inflight_budget_bytes");

		Require(peak >= 1 && peak <= 4, "per-worker peak obeys configured limit");
		Require(budget > 0, "budget bytes recorded");
		std::cout << "  [PASS] Multi-worker (" << threads << " threads): peak inflight="
		          << peak << ", max concurrent=" << state.max_concurrent_reads.load()
		          << ", total reads=" << state.total_reads.load() << std::endl;
	}
	Require(!conn.Query("RESET threads")->HasError(), "reset threads");
	Require(!conn.Query("RESET duckomo_async_max_inflight")->HasError(), "reset max inflight");
}

void TestMemoryLimitInflightReduction(Connection &conn, AsyncTestFileSystem &fs, AsyncState &state) {
	const std::string uri = "'https://duckomo-async/projection.om'";
	const std::string query =
	    "SELECT count(*), avg(humidity), avg(pressure), avg(temperature) FROM read_om(" + uri +
	    ", dimensions := map(['humidity','pressure','temperature'], "
	    "[['row','column'],['row','column'],['row','column']]))";

	Require(!conn.Query("SET duckomo_async_io = true")->HasError(), "enable async succeeds");
	Require(!conn.Query("RESET memory_limit")->HasError(), "reset memory limit");
	fs.Reset();

	auto default_res = duckdb::duckomo_test::Rows(conn.Query(query));
	Require(default_res && !default_res->HasError(), "default memory scan succeeds");
	auto default_metrics = duckdb::duckomo_test::Rows(conn.Query("SELECT metrics FROM duckomo_last_scan_metrics()"))->GetValue(0, 0).ToString();
	const auto default_budget = ExtractRemoteIoUint(default_metrics, "inflight_budget_bytes");

	// Constrain memory limit
	Require(!conn.Query("SET memory_limit = '4MB'")->HasError(), "set memory limit succeeds");
	fs.Reset();

	auto constrained_res = duckdb::duckomo_test::Rows(conn.Query(query));
	Require(constrained_res && !constrained_res->HasError(), "constrained memory scan succeeds");
	auto constrained_metrics = duckdb::duckomo_test::Rows(conn.Query("SELECT metrics FROM duckomo_last_scan_metrics()"))->GetValue(0, 0).ToString();
	const auto constrained_budget = ExtractRemoteIoUint(constrained_metrics, "inflight_budget_bytes");

	Require(ExtractRemoteIoUint(constrained_metrics, "inflight_peak") == 1,
	        "memory pressure reduces concurrency to one request");
	Require(constrained_budget < default_budget,
	        "inflight budget scales down with memory limit: default=" + std::to_string(default_budget) +
	        ", constrained=" + std::to_string(constrained_budget));

	for (int threads : {2, 4}) {
		Require(!conn.Query("SET threads = " + std::to_string(threads))->HasError(), "set threads");
		fs.Reset();
		auto res = duckdb::duckomo_test::Rows(conn.Query(query));
		Require(res && !res->HasError(), "constrained multi-worker succeeds");
		auto m = duckdb::duckomo_test::Rows(conn.Query("SELECT metrics FROM duckomo_last_scan_metrics()"))->GetValue(0, 0).ToString();
		const auto b = ExtractRemoteIoUint(m, "inflight_budget_bytes");
		Require(b <= default_budget, "constrained multi-worker budget within bound");
	}

	Require(!conn.Query("RESET threads")->HasError(), "reset threads");
	Require(!conn.Query("RESET memory_limit")->HasError(), "reset memory limit after test");
	fs.Reset();
	Require(!conn.Query(query)->HasError(), "scan recovers after memory pressure");
	const auto restored_metrics = duckdb::duckomo_test::Rows(conn.Query(
	    "SELECT metrics FROM duckomo_last_scan_metrics()"))->GetValue(0, 0).ToString();
	Require(ExtractRemoteIoUint(restored_metrics, "inflight_budget_bytes") == default_budget,
	        "inflight budget restores when pressure is removed");
	std::cout << "  [PASS] Inflight budget responds to memory limit: default=" << default_budget
	          << ", constrained=" << constrained_budget << std::endl;
}

} // namespace

int main() {
	std::cout << "=== Running DuckDB 2.0 Remote Async I/O Native Tests ===" << std::endl;
	try {
		DuckDB db(nullptr);
		DUCKOMO_TEST_LOAD_EXTENSIONS(db);
		Connection conn(db);

		AsyncState state;
		auto fs = make_uniq<AsyncTestFileSystem>("test/data/projection.om", state);
		auto *fs_ptr = fs.get();
		FileSystem::GetFileSystem(*conn.context).RegisterSubSystem(std::move(fs));

		TestAsyncOnOffMatches(conn, *fs_ptr, state);
		TestSingleVariablePrefetch(conn, *fs_ptr, state);
		TestMultiWorkerInflightBounds(conn, *fs_ptr, state);
		TestErrorHandlingAndRedaction(conn, *fs_ptr, state);
		TestAsyncCancellationAndRecovery(conn, *fs_ptr, state);
		TestMemoryLimitInflightReduction(conn, *fs_ptr, state);

		std::cout << "=== All DuckDB 2.0 Remote Async I/O Native Tests Passed! ===" << std::endl;
		return 0;
	} catch (const std::exception &ex) {
		std::cerr << "FAIL: " << ex.what() << std::endl;
		return 1;
	}
}

#else

int main() {
	return 0;
}

#endif
