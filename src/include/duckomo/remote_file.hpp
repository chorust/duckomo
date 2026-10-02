#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "duckomo/local_file.hpp"
#include "duckomo/range_cache.hpp"

namespace duckdb {
class ClientContext;

namespace duckomo {

// One query-scoped remote object identity and the worker handles opened against
// it. The canonical URI is kept private to the session; Path() is always safe
// to include in diagnostics and strips user info and signed query parameters.
class RemoteReadSession final : public std::enable_shared_from_this<RemoteReadSession> {
public:
	static std::shared_ptr<RemoteReadSession> Create(ClientContext &context, std::string uri,
	                                                std::shared_ptr<ScanMetrics> metrics);

	std::unique_ptr<ReadAtFile> Open(ClientContext &context, ScanMetadataStage metadata_stage,
	                                RangeCache *cache = nullptr);
	const std::string &Path() const noexcept;
	const std::string &Uri() const noexcept;
	ObjectIdentity Identity() const;
	bool HasIdentity() const noexcept;
	const std::shared_ptr<ScanMetrics> &Metrics() const noexcept;
	// The opener observer supplies the identity observed by the HTTP(S)/S3
	// HEAD or 206 response. Every handle in this query must agree with it.
	void ConfirmIdentity(const ObjectIdentity &candidate);

private:
	RemoteReadSession(std::string uri, std::string display_path, ObjectIdentity identity,
	                  std::shared_ptr<ScanMetrics> metrics);

	mutable std::mutex mutex_;
	std::string uri_;
	std::string display_path_;
	ObjectIdentity identity_;
	std::shared_ptr<ScanMetrics> metrics_;
	bool identity_initialized_ = false;
};

bool IsSupportedRemoteOmUri(const std::string &uri) noexcept;
std::string RedactRemoteUri(const std::string &uri);
bool IsCompatibleHttpfsRangeDescriptor(const std::string &descriptor) noexcept;
void RequireHttpfsRangeCapability(ClientContext &context);

} // namespace duckomo
} // namespace duckdb
