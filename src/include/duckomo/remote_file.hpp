#pragma once

#include <memory>
#include <mutex>
#include <string>
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/read_at_file.hpp"

#if DUCKOMO_DUCKDB_API_GENERATION >= 2
#include "duckdb/parallel/async_result.hpp"
#endif

namespace duckdb {
class ClientContext;
namespace duckomo {

#if DUCKOMO_DUCKDB_API_GENERATION >= 2
struct RemoteAsyncState final {
	std::mutex mutex;
	std::exception_ptr exception;
	std::uint64_t active = 0;
	std::uint64_t budget_bytes = 0;
};

// A lane executes its requests serially. Scheduling at most N lanes bounds
// concurrency without blocking an executor task on another executor task.
class RemoteReadAsyncLane final : public AsyncTask {
public:
	std::vector<unique_ptr<AsyncTask>> tasks;
	void Execute() override {
		for (auto &task : tasks) task->Execute();
	}
};

class RemoteReadAsyncTask final : public AsyncTask {
public:
	RemoteReadAsyncTask(const ReadAtFile &file, std::uint64_t offset, std::uint64_t size,
	                    void *destination, ScanReadPhase phase, std::string variable_path,
	                    std::shared_ptr<RemoteAsyncState> shared_exception,
	                    std::uint64_t useful_bytes, std::uint64_t max_request)
	    : file_(file), offset_(offset), size_(size), destination_(destination),
	      phase_(phase), variable_path_(std::move(variable_path)),
	      shared_exception_(std::move(shared_exception)), useful_bytes_(useful_bytes), max_request_(max_request) {
	}

	void Execute() override {
		{
			std::lock_guard<std::mutex> lock(shared_exception_->mutex);
			if (shared_exception_->exception) return;
			++shared_exception_->active;
			if (file_.Metrics()) file_.Metrics()->RecordRemoteInflight(
			    shared_exception_->active, shared_exception_->budget_bytes);
		}
		try {
			for (std::uint64_t done = 0; done < size_;) {
				const auto bytes = std::min(max_request_, size_ - done);
				file_.ReadRange(offset_ + done, bytes, static_cast<std::uint8_t *>(destination_) + done,
				                phase_, variable_path_);
				done += bytes;
			}
			if (file_.Metrics()) file_.Metrics()->RecordRemoteUsefulBytes(useful_bytes_);
		} catch (...) {
			std::lock_guard<std::mutex> lock(shared_exception_->mutex);
			if (!shared_exception_->exception) shared_exception_->exception = std::current_exception();
		}
		std::lock_guard<std::mutex> lock(shared_exception_->mutex);
		--shared_exception_->active;
	}

	std::uint64_t GetIOSize() const noexcept {
		return size_;
	}

private:
	const ReadAtFile &file_;
	std::uint64_t offset_;
	std::uint64_t size_;
	void *destination_;
	ScanReadPhase phase_;
	std::string variable_path_;
	std::shared_ptr<RemoteAsyncState> shared_exception_;
	std::uint64_t useful_bytes_;
	std::uint64_t max_request_;
};
#endif

// Query-scoped evidence from standard file handles. No claim of a snapshot or
// fresh transport probe: HTTPFS owns its buffering, credentials and retries.
class RemoteReadSession final : public std::enable_shared_from_this<RemoteReadSession> {
public:
    static std::shared_ptr<RemoteReadSession> Create(ClientContext &context, std::string uri,
                                                     std::shared_ptr<ScanMetrics> metrics);
    std::unique_ptr<ReadAtFile> Open(ClientContext &context, ScanMetadataStage stage);
    const std::string &Path() const noexcept;
    const std::string &Uri() const noexcept;
    const std::shared_ptr<ScanMetrics> &Metrics() const noexcept;
    void ConfirmIdentity(std::uint64_t size, const std::string &version_tag);
private:
    RemoteReadSession(std::string uri, std::string display_path, std::shared_ptr<ScanMetrics> metrics);
    void RefreshMemoryAccount() const;
    mutable std::mutex mutex_;
    std::string uri_;
    std::string display_path_;
    std::uint64_t size_ = 0;
    std::string version_tag_;
    bool identity_initialized_ = false;
    std::shared_ptr<ScanMetrics> metrics_;
    std::shared_ptr<ScanMemoryAccount> memory_account_;
};
bool IsSupportedRemoteOmUri(const std::string &uri) noexcept;
std::string RedactRemoteUri(const std::string &uri);
} // namespace duckomo
} // namespace duckdb
