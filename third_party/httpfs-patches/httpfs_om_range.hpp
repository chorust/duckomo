#pragma once

// Shared, versioned interface between DuckOMO and the companion httpfs build.
// Keep this header independent of DuckDB internals: both extensions include
// the same copy and communicate through the FileOpener's provider interface.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace duckdb {

static constexpr std::uint32_t HTTPFS_OM_RANGE_ABI_VERSION = 2;
static constexpr char HTTPFS_OM_RANGE_UPSTREAM_COMMIT[] = "c3f215ab360f04dc3d3d5305fa81849c0121f111";
static constexpr char HTTPFS_OM_RANGE_PATCH_REVISION[] = "duckomo-httpfs-range-v2";
static constexpr char HTTPFS_OM_RANGE_CAPABILITY_DESCRIPTOR[] =
    "duckomo.httpfs.om-range|abi=2|upstream=c3f215ab360f04dc3d3d5305fa81849c0121f111|revision=duckomo-httpfs-range-v2";

struct HTTPFSOmRangeCapabilitiesV2 {
	std::uint32_t abi_version;
	const char *upstream_commit;
	const char *patch_revision;
};

static constexpr HTTPFSOmRangeCapabilitiesV2 HTTPFS_OM_RANGE_CAPABILITIES_V2 = {
	HTTPFS_OM_RANGE_ABI_VERSION,
	HTTPFS_OM_RANGE_UPSTREAM_COMMIT,
	HTTPFS_OM_RANGE_PATCH_REVISION,
};

enum class HTTPFSOmReadClassV2 : std::uint8_t {
	Metadata,
	Coordinate,
	ValueIndex,
	ValueData,
};

enum class HTTPFSOmVersionKindV2 : std::uint8_t {
	Unverified,
	StrongETag,
	S3VersionId,
};

struct HTTPFSOmRangeIdentityV2 {
	std::string canonical_uri;
	std::uint64_t object_size = 0;
	HTTPFSOmVersionKindV2 version_kind = HTTPFSOmVersionKindV2::Unverified;
	std::string version_token;
};

struct HTTPFSOmRequestV2 {
	std::uint64_t request_id = 0;
	std::uint64_t attempt = 0;
	std::string method;
	std::string uri;
	// Resolved origin used by S3 after reading the secret and URL style.
	// Empty for transports whose endpoint is already explicit in the input URI.
	std::string effective_endpoint;
	bool has_range = false;
	std::uint64_t range_offset = 0;
	std::uint64_t range_length = 0;
	HTTPFSOmReadClassV2 read_class = HTTPFSOmReadClassV2::Metadata;
};

struct HTTPFSOmResponseV2 {
	std::uint64_t request_id = 0;
	std::uint64_t attempt = 0;
	std::string method;
	std::string uri;
	int status = 0;
	std::map<std::string, std::string> headers;
	std::uint64_t body_bytes = 0;
	bool complete = false;
	bool transport_error = false;
	std::string error_code;
};

// A session is query-scoped. It must outlive every HTTPFS handle opened from
// it. Implementations may throw a sanitized DuckDB exception from request and
// response callbacks to abort the complete scan.
class HTTPFSOmRangeSessionV2 {
public:
	virtual ~HTTPFSOmRangeSessionV2() = default;

	virtual const HTTPFSOmRangeIdentityV2 &Identity() const noexcept = 0;
	virtual bool IsCancelled() const noexcept = 0;
	virtual HTTPFSOmReadClassV2 CurrentReadClass() const noexcept = 0;

	// Called before each signed HTTP attempt. Implementations add conditional
	// version constraints to `headers`; httpfs still owns credentials/signing.
	virtual void BeforeRequest(HTTPFSOmRequestV2 &request,
	                          std::map<std::string, std::string> &headers) = 0;
	virtual bool AllowRedirect(const std::string &from_uri, const std::string &to_uri,
	                           bool carries_authorization) = 0;

	// One response event is emitted for every attempt, including failed and
	// retried attempts. Header names/values must exclude credentials and cookies.
	virtual void OnResponse(const HTTPFSOmResponseV2 &response) = 0;
	virtual void OnBodyBytes(std::uint64_t request_id, std::uint64_t attempt,
	                         std::uint64_t received_bytes) = 0;
	virtual void OnAttemptComplete(const HTTPFSOmResponseV2 &response) = 0;
};

class HTTPFSOmRangeProviderV2 {
public:
	virtual ~HTTPFSOmRangeProviderV2() = default;
	virtual std::uint32_t OmRangeAbiVersion() const noexcept = 0;
	virtual std::shared_ptr<HTTPFSOmRangeSessionV2>
	OpenOmRangeSessionV2(const std::string &uri) = 0;
};

} // namespace duckdb
