#include "duckdb.hpp"
#include "duckdb_test_compat.hpp"
#include "duckdb/common/exception/http_exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/main/client_context.hpp"
#include "core_functions_extension.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/om_reader.hpp"
#include "duckomo/remote_file.hpp"
#include "duckomo_extension.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace duckdb {
DUCKOMO_TEST_LOAD_ALL_EXTENSIONS(database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb

namespace {

using namespace duckdb;
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

struct FakeState final {
	std::function<void()> open_error;
	std::function<void()> read_error;
	std::size_t opens = 0;
	std::size_t successful_opens = 0;
	std::size_t closes = 0;
};

struct FakeHandle final : FileHandle {
	FakeHandle(FileSystem &file_system, const std::string &path, FileOpenFlags flags, FakeState &state)
	    : FileHandle(file_system, path, flags), state(state) {
	}
	~FakeHandle() override { Close(); }
	void Close() override {
		if (!closed) {
			closed = true;
			state.closes++;
		}
	}
	FakeState &state;
	bool closed = false;
};

class FakeFileSystem final : public FileSystem {
public:
	explicit FakeFileSystem(FakeState &state) : state(state) {
	}
	bool CanHandleFile(const string &path) override {
		return path.find("https://duckomo-series-errors/") == 0;
	}
	string GetName() const override { return "duckomo-series-error-classification-test"; }
	unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags,
	                                optional_ptr<FileOpener>) override {
		state.opens++;
		if (state.open_error) state.open_error();
		state.successful_opens++;
		return make_uniq<FakeHandle>(*this, path, flags, state);
	}
	int64_t GetFileSize(FileHandle &) override { return 8; }
	string GetVersionTag(FileHandle &) override { return "v1"; }
	void Read(FileHandle &, void *destination, int64_t size, idx_t offset) override {
		if (state.read_error) state.read_error();
		Require(offset <= 8 && size >= 0 && static_cast<std::uint64_t>(size) <= 8 - offset,
		        "fake remote fixture read stays within its object");
		std::memcpy(destination, "abcdefgh" + offset, static_cast<std::size_t>(size));
	}
	FakeState &state;
};

void ExpectReaderError(const std::function<void()> &action, ReaderErrorCategory category,
                       ReaderErrorStage stage, ReaderErrorCode code, const std::string &label,
                       const std::string &expected_message = {}) {
	try {
		action();
	} catch (const ReaderError &error) {
		Require(error.Category() == category, label + ": typed category differs");
		Require(error.Stage() == stage, label + ": typed stage differs");
		Require(error.Code() == code, label + ": ordinary ReaderErrorCode changed");
		const std::string message = error.what();
		Require(message.find("sensitive-marker") == std::string::npos,
		        label + ": sensitive transport text escaped sanitization");
		if (!expected_message.empty()) {
			Require(message == expected_message, label + ": sanitized ordinary-read diagnostic changed: " + message);
		}
		return;
	}
	throw std::runtime_error(label + ": expected ReaderError");
}

void TestRemoteCategoriesAndStages(Connection &connection, FakeState &state) {
	FileSystem::GetFileSystem(*connection.context).RegisterSubSystem(make_uniq<FakeFileSystem>(state));
	auto metrics = std::make_shared<ScanMetrics>();
	auto session = RemoteReadSession::Create(*connection.context,
	                                        "https://duckomo-series-errors/object.om?token=sensitive-marker", metrics);
	const auto status_error = [](int status) {
		return [status]() {
			const std::unordered_map<std::string, std::string> headers{{"Authorization", "sensitive-marker"}};
			throw HTTPException(status, "sensitive-marker body", headers, "sensitive-marker reason",
			                    "sensitive-marker signed URL");
		};
	};
	for (const auto &item : std::vector<std::tuple<int, ReaderErrorCategory, const char *>>{
	         {404, ReaderErrorCategory::ObjectNotFound, "structured 404"},
	         {403, ReaderErrorCategory::AccessDenied, "structured 403"},
	         {412, ReaderErrorCategory::ObjectChanged, "structured 412"}}) {
		state.open_error = status_error(std::get<0>(item));
		ExpectReaderError([&] { (void)session->Open(*connection.context, ScanMetadataStage::Bind); },
		                  std::get<1>(item), ReaderErrorStage::InitialOpen, ReaderErrorCode::FileIo,
		                  std::get<2>(item));
	}

	state.open_error = [] { throw IOException("HTTP 404 sensitive-marker in an unstructured message"); };
	const std::string unchanged_diagnostic =
	    "remote OM open failed for 'https://duckomo-series-errors/object.om?<redacted>' [transport]; "
	    "check connectivity, proxy, TLS and timeout settings; no structured HTTP status was supplied; "
	    "transport details redacted";
	ExpectReaderError([&] { (void)session->Open(*connection.context, ScanMetadataStage::Bind); },
	                  ReaderErrorCategory::Unclassified, ReaderErrorStage::InitialOpen,
	                  ReaderErrorCode::FileIo, "unclassified message containing 404", unchanged_diagnostic);

	state.open_error = status_error(404);
	ExpectReaderError([&] { (void)session->Open(*connection.context, ScanMetadataStage::Scan); },
	                  ReaderErrorCategory::ObjectNotFound, ReaderErrorStage::LaterOpen,
	                  ReaderErrorCode::FileIo, "structured 404 on later open is not initial absence");

	state.open_error = {};
	auto file = session->Open(*connection.context, ScanMetadataStage::Scan);
	state.read_error = status_error(404);
	ExpectReaderError([&] { (void)file->ReadRange(0, 1, ScanReadPhase::Data, "value"); },
	                  ReaderErrorCategory::ObjectNotFound, ReaderErrorStage::Read,
	                  ReaderErrorCode::DataRead, "structured 404 during later read");
	state.read_error = {};
	Require(file->ReadRange(0, 1, ScanReadPhase::Data, "value")[0] == 'a',
	        "ordinary remote read recovers after diagnostic classification");
	file.reset();
	Require(state.successful_opens == state.closes, "all successful remote handles are closed");
}

void TestLocalCategoriesAndStages(ClientContext &context) {
	const auto directory = std::filesystem::temp_directory_path() /
	                       ("duckomo-series-error-" + std::to_string(static_cast<unsigned long long>(getpid())));
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	const auto missing_path = directory / "missing.om";
	ExpectReaderError([&] {
		(void)LocalFile::Open(context, missing_path.string(), nullptr, ScanMetadataStage::Bind);
	}, ReaderErrorCategory::ObjectNotFound, ReaderErrorStage::InitialOpen, ReaderErrorCode::FileIo,
	                  "local ENOENT at initial open");
	ExpectReaderError([&] {
		(void)LocalFile::Open(context, missing_path.string(), nullptr, ScanMetadataStage::Scan);
	}, ReaderErrorCategory::ObjectNotFound, ReaderErrorStage::LaterOpen, ReaderErrorCode::FileIo,
	                  "local ENOENT at later open");

	const auto denied_path = directory / "denied.om";
	{
		std::ofstream file(denied_path, std::ios::binary);
		file << "fixture";
	}
	std::filesystem::permissions(denied_path, std::filesystem::perms::none);
	ExpectReaderError([&] {
		(void)LocalFile::Open(context, denied_path.string(), nullptr, ScanMetadataStage::Bind);
	}, ReaderErrorCategory::AccessDenied, ReaderErrorStage::InitialOpen, ReaderErrorCode::FileIo,
	                  "local EACCES is not absence");
	std::filesystem::permissions(denied_path, std::filesystem::perms::owner_all);
	std::filesystem::remove_all(directory);
}

struct LifecycleObject final {
	std::shared_ptr<const std::vector<std::uint8_t>> bytes;
	std::string version;
};

struct SeriesLifecycleState final {
	void Put(const std::string &path, const std::vector<std::uint8_t> &bytes, const std::string &version) {
		std::lock_guard<std::mutex> guard(mutex);
		objects[path] = {std::make_shared<const std::vector<std::uint8_t>>(bytes), version};
	}

	void Remove(const std::string &path) {
		std::lock_guard<std::mutex> guard(mutex);
		objects.erase(path);
	}

	std::uint64_t OpensFor(const std::string &path) const {
		std::lock_guard<std::mutex> guard(mutex);
		const auto found = opens_by_path.find(path);
		return found == opens_by_path.end() ? 0 : found->second;
	}

	std::pair<std::uint64_t, std::uint64_t> HandleCounts() const {
		std::lock_guard<std::mutex> guard(mutex);
		return {successful_opens, closed_handles};
	}

	void ArmReadBarrier(const std::string &path, std::uint64_t minimum_open_number) {
		std::lock_guard<std::mutex> guard(mutex);
		barrier_path = path;
		barrier_open_number = minimum_open_number;
		barrier_armed = true;
		barrier_entered = false;
		barrier_released = false;
	}

	bool WaitForReadBarrier() {
		std::unique_lock<std::mutex> lock(mutex);
		return condition.wait_for(lock, std::chrono::seconds(10), [&] { return barrier_entered; });
	}

	void ReleaseReadBarrier() {
		{
			std::lock_guard<std::mutex> guard(mutex);
			barrier_released = true;
		}
		condition.notify_all();
	}

	mutable std::mutex mutex;
	std::condition_variable condition;
	std::unordered_map<std::string, LifecycleObject> objects;
	std::unordered_map<std::string, std::uint64_t> opens_by_path;
	std::uint64_t open_attempts = 0;
	std::uint64_t successful_opens = 0;
	std::uint64_t closed_handles = 0;
	std::uint64_t failed_opens = 0;
	std::string barrier_path;
	std::uint64_t barrier_open_number = 0;
	bool barrier_armed = false;
	bool barrier_entered = false;
	bool barrier_released = false;
};

class SeriesLifecycleHandle final : public FileHandle {
public:
	SeriesLifecycleHandle(FileSystem &file_system, const std::string &path, FileOpenFlags flags,
	                      SeriesLifecycleState &state, LifecycleObject object, std::uint64_t open_number)
	    : FileHandle(file_system, path, flags), state(state), object(std::move(object)),
	      open_number(open_number), path(path) {
	}

	~SeriesLifecycleHandle() override { Close(); }

	void Close() override {
		if (closed) return;
		closed = true;
		std::lock_guard<std::mutex> guard(state.mutex);
		state.closed_handles++;
	}

	SeriesLifecycleState &state;
	LifecycleObject object;
	std::uint64_t open_number;
	std::string path;
	bool closed = false;
};

class SeriesLifecycleFileSystem final : public FileSystem {
public:
	explicit SeriesLifecycleFileSystem(SeriesLifecycleState &state) : state(state) {
	}

	bool CanHandleFile(const string &path) override {
		return path.find("https://duckomo-series-lifecycle.test/") == 0;
	}

	string GetName() const override { return "duckomo-series-lifecycle-test"; }

	unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags,
	                                optional_ptr<FileOpener>) override {
		std::lock_guard<std::mutex> guard(state.mutex);
		state.open_attempts++;
		const auto open_number = ++state.opens_by_path[path];
		const auto found = state.objects.find(path);
		if (found == state.objects.end()) {
			state.failed_opens++;
			const std::unordered_map<std::string, std::string> headers;
			throw HTTPException(404, "fixture object missing", headers, "not found", path);
		}
		state.successful_opens++;
		return make_uniq<SeriesLifecycleHandle>(*this, path, flags, state, found->second, open_number);
	}

	int64_t GetFileSize(FileHandle &handle) override {
		return static_cast<int64_t>(dynamic_cast<SeriesLifecycleHandle &>(handle).object.bytes->size());
	}

	string GetVersionTag(FileHandle &handle) override {
		return dynamic_cast<SeriesLifecycleHandle &>(handle).object.version;
	}

	void Read(FileHandle &handle, void *destination, int64_t size, idx_t offset) override {
		auto &series_handle = dynamic_cast<SeriesLifecycleHandle &>(handle);
		{
			std::unique_lock<std::mutex> lock(state.mutex);
			if (state.barrier_armed && state.barrier_path == series_handle.path &&
			    series_handle.open_number >= state.barrier_open_number) {
				state.barrier_armed = false;
				state.barrier_entered = true;
				state.condition.notify_all();
				state.condition.wait(lock, [&] { return state.barrier_released; });
			}
		}
		const auto &bytes = *series_handle.object.bytes;
		Require(size >= 0 && offset <= bytes.size() && static_cast<std::uint64_t>(size) <= bytes.size() - offset,
		        "series lifecycle fake read stays within its immutable object version");
		if (size != 0) std::memcpy(destination, bytes.data() + offset, static_cast<std::size_t>(size));
	}

private:
	SeriesLifecycleState &state;
};

std::filesystem::path FindSeriesFixture(const std::string &relative_path) {
	auto cursor = std::filesystem::absolute(std::filesystem::current_path());
	for (;;) {
		const auto candidate = cursor / "build" / "series-fixtures" / relative_path;
		if (std::filesystem::is_regular_file(candidate)) return candidate;
		const auto parent = cursor.parent_path();
		if (parent == cursor) break;
		cursor = parent;
	}
	throw std::runtime_error("generated series fixture is unavailable: " + relative_path);
}

std::vector<std::uint8_t> ReadSeriesFixture(const std::string &relative_path) {
	std::ifstream input(FindSeriesFixture(relative_path), std::ios::binary);
	Require(input.good(), "cannot open generated series fixture: " + relative_path);
	return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		if (character == '\'') result += '\'';
		result += character;
	}
	return result + "'";
}

std::string SeriesOptions(const std::string &end_time, bool strict = false) {
	std::string result = "chunk_time_length := 4, time_step := INTERVAL '1 hour'";
	if (strict) result += ", strict := true";
	result += ", dimensions := ['lead','point','time'], "
	          "grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'lon_fastest'}, "
	          "spatial_axes := ['point'], valid_times := [TIMESTAMP '1970-01-01 00:00:00', TIMESTAMP '" + end_time + "']";
	return result;
}

std::string SeriesQuery(const std::string &prefix, const std::string &projection = "count(*)",
                        const std::string &end_time = "1970-01-01 03:00:00", bool strict = false) {
	return "SELECT " + projection + " FROM read_om_series(" + SqlLiteral(prefix) + ", " +
	       SeriesOptions(end_time, strict) + ")";
}

std::string OrdinaryQuery(const std::string &uri) {
	return "SELECT count(*) FROM read_om(" + SqlLiteral(uri) + ", "
	       "dimensions := ['lead','point','time'], "
	       "grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,'dlat':1.0,'dlon':1.0,'order':'lon_fastest'}, "
	       "spatial_axes := ['point'], "
	       "axes := {'time': {'axis':'time','start':TIMESTAMP '1970-01-01 00:00:00','step':INTERVAL '1 hour'}})";
}

std::string RequireScalar(Connection &connection, const std::string &sql) {
	auto result = duckomo_test::Rows(connection.Query(sql));
	Require(result && !result->HasError(), sql + ": " + (result ? result->GetError() : "no result"));
	Require(result->RowCount() == 1 && result->ColumnCount() == 1, "expected one scalar row: " + sql);
	return result->GetValue(0, 0).ToString();
}

void RequireQuerySuccess(Connection &connection, const std::string &sql) {
	auto result = duckomo_test::Rows(connection.Query(sql));
	Require(result && !result->HasError(), sql + ": " + (result ? result->GetError() : "no result"));
}

std::string RequireQueryError(Connection &connection, const std::string &sql) {
	auto result = duckomo_test::Rows(connection.Query(sql));
	if (!result || !result->HasError()) {
		std::string details = sql + ": expected an error";
		if (result) {
			details += "; returned " + std::to_string(result->RowCount()) + " rows";
			if (result->RowCount() == 1 && result->ColumnCount() == 1) {
				details += " with scalar " + result->GetValue(0, 0).ToString();
			}
		}
		throw std::runtime_error(details);
	}
	return result->GetError();
}

bool ContainsCancellation(const std::string &message) {
	auto lowered = message;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
	               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return lowered.find("interrupt") != std::string::npos || lowered.find("cancel") != std::string::npos;
}

void TestPostBindRemovalAndReplacement(DuckDB &database, SeriesLifecycleState &state,
                                       const std::vector<std::uint8_t> &original,
                                       const std::vector<std::uint8_t> &replacement) {
	Connection connection(database);
	const std::string base = "https://duckomo-series-lifecycle.test/after-bind";
	const std::string removed_prefix = base + "/removed/temperature";
	const std::string removed_uri = removed_prefix + "/chunk_0000.om";
	state.Put(removed_uri, original, "fixture-v1");
	RequireQuerySuccess(connection, "PREPARE stale_series AS " + SeriesQuery(removed_prefix));
	RequireQuerySuccess(connection, "PREPARE ordinary_series AS " + OrdinaryQuery(removed_uri));
	Require(state.OpensFor(removed_uri) >= 2, "PREPARE must bind both series and ordinary children before the mutation checkpoint");

	// Returning from PREPARE is the deterministic after-bind barrier. A fresh
	// statement may observe initial absence; these already-bound plans may not.
	state.Remove(removed_uri);
	const auto series_missing_error = RequireQueryError(connection, "EXECUTE stale_series");
	const auto ordinary_missing_error = RequireQueryError(connection, "EXECUTE ordinary_series");
	Require(series_missing_error == ordinary_missing_error,
	        "post-bind removal diagnostic differs from ordinary read_om: series=" + series_missing_error +
	            " ordinary=" + ordinary_missing_error);
	Require(series_missing_error.find("fixture object missing") == std::string::npos,
	        "raw fake transport detail must not escape the sanitized later-open diagnostic");

	RequireQuerySuccess(connection, "PREPARE fresh_series AS " + SeriesQuery(removed_prefix));
	Require(RequireScalar(connection, "EXECUTE fresh_series") == "0",
	        "a new default-skip bind after removal should use the all-missing typed schema");
	state.Put(removed_uri, original, "fixture-v1");
	Require(RequireScalar(connection, "EXECUTE stale_series") == "24",
	        "stale series statement recovers when the originally bound object returns");
	Require(RequireScalar(connection, "EXECUTE ordinary_series") == "24",
	        "ordinary read_om recovers after the same removal and restoration");
	RequireQuerySuccess(connection, "DEALLOCATE fresh_series");
	RequireQuerySuccess(connection, "DEALLOCATE stale_series");
	RequireQuerySuccess(connection, "DEALLOCATE ordinary_series");

	const std::string replaced_prefix = base + "/replaced/temperature";
	const std::string replaced_uri = replaced_prefix + "/chunk_0000.om";
	state.Put(replaced_uri, original, "fixture-v1");
	RequireQuerySuccess(connection, "PREPARE replaced_series AS " + SeriesQuery(replaced_prefix));
	RequireQuerySuccess(connection, "PREPARE ordinary_replaced AS " + OrdinaryQuery(replaced_uri));
	Require(state.OpensFor(replaced_uri) >= 2, "replacement statements must bind before replacement");
	state.Put(replaced_uri, replacement, "fixture-v2");
	const auto series_replaced_error = RequireQueryError(connection, "EXECUTE replaced_series");
	Require(series_replaced_error.find("object identity changed") != std::string::npos,
	        "the bound series child must reject a replacement object: " + series_replaced_error);
	Require(RequireScalar(connection, "EXECUTE ordinary_replaced") == "18",
	        "ordinary read_om keeps its rebind policy and reads the newly bound replacement");
	state.Put(replaced_uri, original, "fixture-v1");
	Require(RequireScalar(connection, "EXECUTE replaced_series") == "24",
	        "series query recovers after the original version returns");
	Require(RequireScalar(connection, "EXECUTE ordinary_replaced") == "24",
	        "ordinary query recovers after the original version returns");
	RequireQuerySuccess(connection, "DEALLOCATE replaced_series");
	RequireQuerySuccess(connection, "DEALLOCATE ordinary_replaced");
}

void TestCancellationDuringBinding(DuckDB &database, SeriesLifecycleState &state,
                                  const std::vector<std::uint8_t> &chunk0,
                                  const std::vector<std::uint8_t> &chunk1) {
	Connection connection(database);
	const std::string prefix = "https://duckomo-series-lifecycle.test/cancel-bind/temperature";
	const std::string uri0 = prefix + "/chunk_0000.om";
	const std::string uri1 = prefix + "/chunk_0001.om";
	state.Put(uri0, chunk0, "fixture-v1");
	state.Put(uri1, chunk1, "fixture-v1");
	state.ArmReadBarrier(uri0, 1);

	std::string query_error;
	std::thread binder([&] {
		try {
			auto result = duckomo_test::Rows(connection.Query(
			    "PREPARE cancelled_bind AS " + SeriesQuery(prefix, "count(*)", "1970-01-01 04:00:00")));
			if (!result) query_error = "bind returned no result";
			else if (result->HasError()) query_error = result->GetError();
			else query_error = "binding unexpectedly succeeded after cancellation";
		} catch (const std::exception &error) {
			query_error = error.what();
		}
	});
	if (!state.WaitForReadBarrier()) {
		state.ReleaseReadBarrier();
		binder.join();
		throw std::runtime_error("series bind did not reach the deterministic metadata-read barrier");
	}
	connection.Interrupt();
	state.ReleaseReadBarrier();
	binder.join();
	Require(ContainsCancellation(query_error), "binding interruption was not preserved: " + query_error);
	Require(RequireScalar(connection, "SELECT 1") == "1", "connection must recover after bind cancellation");
}

void TestCancellationDuringExecutionAndSessionIsolation(DuckDB &database, SeriesLifecycleState &state,
                                                        const std::vector<std::uint8_t> &chunk0) {
	Connection cancelled(database);
	Connection independent(database);
	const std::string cancelled_prefix = "https://duckomo-series-lifecycle.test/cancel-exec/temperature";
	const std::string cancelled_uri = cancelled_prefix + "/chunk_0000.om";
	const std::string independent_prefix = "https://duckomo-series-lifecycle.test/independent/temperature";
	const std::string independent_uri = independent_prefix + "/chunk_0000.om";
	state.Put(cancelled_uri, chunk0, "fixture-v1");
	state.Put(independent_uri, chunk0, "fixture-v1");
	RequireQuerySuccess(cancelled, "PREPARE cancelled_exec AS " + SeriesQuery(cancelled_prefix, "sum(value)"));
	RequireQuerySuccess(independent, "PREPARE independent_exec AS " + SeriesQuery(independent_prefix));
	state.ArmReadBarrier(cancelled_uri, state.OpensFor(cancelled_uri) + 1);

	std::string query_error;
	std::thread executor([&] {
		try {
			auto stream = duckomo_test::SendQuery(cancelled, "EXECUTE cancelled_exec");
			if (!stream) {
				query_error = "execution returned no stream";
			} else {
				while (stream->Fetch()) {
				}
				query_error = stream->HasError() ? stream->GetError() : "execution unexpectedly succeeded after cancellation";
			}
		} catch (const std::exception &error) {
			query_error = error.what();
		}
	});
	if (!state.WaitForReadBarrier()) {
		state.ReleaseReadBarrier();
		executor.join();
		throw std::runtime_error("series execution did not reach the deterministic scan-read barrier");
	}
	Require(RequireScalar(independent, "EXECUTE independent_exec") == "24",
	        "a concurrent connection must complete while the first series scan is blocked");
	cancelled.Interrupt();
	state.ReleaseReadBarrier();
	executor.join();
	Require(ContainsCancellation(query_error), "execution interruption was not preserved: " + query_error);
	Require(RequireScalar(cancelled, "EXECUTE cancelled_exec") != "0",
	        "the cancelled connection must recover on a later valid query");
	const auto cancelled_query_id = RequireScalar(cancelled, "SELECT query_id FROM duckomo_last_scan_metrics()");
	const auto independent_query_id = RequireScalar(independent, "SELECT query_id FROM duckomo_last_scan_metrics()");
	Require(!cancelled_query_id.empty() && !independent_query_id.empty() && cancelled_query_id != independent_query_id,
	        "connection-local metrics must retain distinct query identities after concurrent recovery");
	RequireQuerySuccess(cancelled, "DEALLOCATE cancelled_exec");
	RequireQuerySuccess(independent, "DEALLOCATE independent_exec");
}

void TestSeriesLifecycle(DuckDB &database, SeriesLifecycleState &state) {
	const auto chunk0 = ReadSeriesFixture("data/synthetic/temperature/chunk_0000.om");
	const auto chunk1 = ReadSeriesFixture("data/synthetic/temperature/chunk_0001.om");
	const auto wrong_time_extent = ReadSeriesFixture("data/mismatch/time-extent/temperature/chunk_0001.om");
	Connection filesystem_registrar(database);
	FileSystem::GetFileSystem(*filesystem_registrar.context).RegisterSubSystem(make_uniq<SeriesLifecycleFileSystem>(state));
	{
		TestCancellationDuringBinding(database, state, chunk0, chunk1);
		TestCancellationDuringExecutionAndSessionIsolation(database, state, chunk0);
		TestPostBindRemovalAndReplacement(database, state, chunk0, wrong_time_extent);
	}
	const auto counts = state.HandleCounts();
	Require(counts.first == counts.second,
		    "all remote handles must close after removal/replacement errors, cancellation, recovery and deallocation");
	}

} // namespace

int main() {
	try {
		FakeState state;
		SeriesLifecycleState lifecycle_state;
		DuckDB database(nullptr);
		DUCKOMO_TEST_LOAD_EXTENSIONS(database);
		database.LoadStaticExtension<CoreFunctionsExtension>();
		Connection connection(database);
		TestRemoteCategoriesAndStages(connection, state);
		TestLocalCategoriesAndStages(*connection.context);
		TestSeriesLifecycle(database, lifecycle_state);
		std::cout << "series diagnostic classification and lifecycle checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "series diagnostic classification and lifecycle checks failed: " << error.what() << '\n';
		return 1;
	}
}
