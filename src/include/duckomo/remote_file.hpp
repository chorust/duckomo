#pragma once

#include <memory>
#include <mutex>
#include <string>
#include "duckomo/read_at_file.hpp"

namespace duckdb {
class ClientContext;
namespace duckomo {

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
