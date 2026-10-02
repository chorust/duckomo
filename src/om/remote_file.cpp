#include "duckomo/remote_file.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"
#include "duckdb/common/exception/http_exception.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/file_open_flags.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "duckdb/main/client_context_file_opener.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/database.hpp"
#include "duckomo/om_reader.hpp"
#include "httpfs_om_range.hpp"

namespace duckdb {
namespace duckomo {

namespace {

struct ParsedRemoteUri final {
	std::string scheme;
	std::string endpoint;
	std::string display_path;
};

ParsedRemoteUri ParseRemoteUri(const std::string &uri) {
	const auto delimiter = uri.find("://");
	if (delimiter == std::string::npos || delimiter == 0) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "remote OM path must use HTTP, HTTPS, or S3");
	}
	std::string scheme = uri.substr(0, delimiter);
	std::transform(scheme.begin(), scheme.end(), scheme.begin(),
	               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	if (scheme != "http" && scheme != "https" && scheme != "s3") {
		throw ReaderError(ReaderErrorCode::InvalidPath, "remote OM path must use HTTP, HTTPS, or S3");
	}
	if (uri.find('#') != std::string::npos || uri.find_first_of("*?[]{}") != std::string::npos) {
		// A query string is allowed for signed URLs. Only glob metacharacters in
		// the path portion are rejected below.
		const auto query = uri.find('?');
		const auto glob = uri.find_first_of("*[]{}");
		if (uri.find('#') != std::string::npos || (glob != std::string::npos && (query == std::string::npos || glob < query))) {
			throw ReaderError(ReaderErrorCode::InvalidPath, "remote OM input must identify one object without a fragment or glob");
		}
	}
	const auto authority_start = delimiter + 3;
	const auto authority_end = uri.find_first_of("/?#", authority_start);
	const auto authority_stop = authority_end == std::string::npos ? uri.size() : authority_end;
	if (authority_stop == authority_start) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "remote OM URI has no endpoint");
	}
	std::string authority = uri.substr(authority_start, authority_stop - authority_start);
	const auto user_info = authority.rfind('@');
	if (user_info != std::string::npos) authority.erase(0, user_info + 1);
	if (authority.empty()) throw ReaderError(ReaderErrorCode::InvalidPath, "remote OM URI has no endpoint");
	const auto query = uri.find('?', authority_stop);
	const auto display_end = query == std::string::npos ? uri.size() : query;
	std::string display = scheme + "://" + authority + uri.substr(authority_stop, display_end - authority_stop);
	if (query != std::string::npos) display += "?<redacted>";
	return {scheme, scheme + "://" + authority, std::move(display)};
}

std::string NewAccessPartition() {
	static std::atomic<std::uint64_t> next_id{1};
	static const std::uint64_t process_salt = [] {
		std::random_device random;
		return (static_cast<std::uint64_t>(random()) << 32) ^ static_cast<std::uint64_t>(random());
	}();
	const auto id = next_id.fetch_add(1, std::memory_order_relaxed);
	std::ostringstream output;
	output << std::hex << process_salt << '-' << id;
	return output.str();
}

class RemoteAccessPartitionState final : public ClientContextState {
public:
	std::string Get() const {
		std::lock_guard<std::mutex> guard(mutex_);
		if (partition_.empty()) partition_ = NewAccessPartition();
		return partition_;
	}

private:
	mutable std::mutex mutex_;
	mutable std::string partition_;
};

std::string Header(const HTTPFSOmResponseV2 &response, const char *name) {
	for (const auto &entry : response.headers) {
		if (StringUtil::CIEquals(entry.first, name)) return entry.second;
	}
	return {};
}

std::uint64_t ParsePositiveSize(const std::string &text, const std::string &display_path) {
	try {
		std::size_t consumed = 0;
		const auto size = std::stoull(text, &consumed);
		if (consumed != text.size() || size == 0) throw std::invalid_argument("invalid size");
		return size;
	} catch (...) {
		throw ReaderError(ReaderErrorCode::FileIo,
		                  "remote OM object '" + display_path + "' did not report a positive content length");
	}
}

ObjectVersionStrength VersionStrengthFrom(const HTTPFSOmResponseV2 &response, std::string &token) {
	const auto version_id = Header(response, "x-amz-version-id");
	if (!version_id.empty() && version_id != "null") {
		token = version_id;
		return ObjectVersionStrength::S3VersionId;
	}
	const auto etag = Header(response, "ETag");
	if (!etag.empty()) {
		token = etag;
		if (etag.size() >= 2 && StringUtil::CIEquals(etag.substr(0, 2), "W/")) {
			return ObjectVersionStrength::Weak;
		}
		return ObjectVersionStrength::StrongETag;
	}
	token.clear();
	return ObjectVersionStrength::None;
}

HTTPFSOmVersionKindV2 HttpfsVersionKind(ObjectVersionStrength strength) {
	if (strength == ObjectVersionStrength::StrongETag) return HTTPFSOmVersionKindV2::StrongETag;
	if (strength == ObjectVersionStrength::S3VersionId) return HTTPFSOmVersionKindV2::S3VersionId;
	return HTTPFSOmVersionKindV2::Unverified;
}

HTTPFSOmReadClassV2 HttpfsReadClass(ScanReadPhase phase) {
	switch (phase) {
	case ScanReadPhase::Index:
		return HTTPFSOmReadClassV2::ValueIndex;
	case ScanReadPhase::Data:
		return HTTPFSOmReadClassV2::ValueData;
	case ScanReadPhase::Coordinate:
	case ScanReadPhase::CoordinateIndex:
	case ScanReadPhase::CoordinateData:
		return HTTPFSOmReadClassV2::Coordinate;
	case ScanReadPhase::Metadata:
	default:
		return HTTPFSOmReadClassV2::Metadata;
	}
}

std::string VersionStrengthName(ObjectVersionStrength strength) {
	switch (strength) {
	case ObjectVersionStrength::StrongETag: return "strong_etag";
	case ObjectVersionStrength::S3VersionId: return "s3_version_id";
	case ObjectVersionStrength::Weak: return "weak";
	case ObjectVersionStrength::None: return "unverified";
	}
	return "unverified";
}

void RecordReadMetric(const std::shared_ptr<ScanMetrics> &metrics, ScanMetadataStage stage,
                      ScanReadPhase phase, std::uint64_t size, const std::string &variable_path) {
	if (!metrics) return;
	if (phase == ScanReadPhase::Metadata) {
		metrics->RecordMetadataRead(stage, size);
	} else if (phase == ScanReadPhase::Coordinate) {
		metrics->RecordCoordinateRead(size);
	} else {
		metrics->RecordSuccessfulRead(phase, size, variable_path);
	}
}

class RemoteHandleRangeSession final : public HTTPFSOmRangeSessionV2 {
public:
	RemoteHandleRangeSession(ClientContext &context, std::shared_ptr<RemoteReadSession> query_session,
	                         std::shared_ptr<ScanMetrics> metrics, bool bootstrap)
	    : context_(&context), query_session_(std::move(query_session)), metrics_(std::move(metrics)), bootstrap_(bootstrap) {
		candidate_ = query_session_->Identity();
		if (query_session_->HasIdentity()) SetHttpfsIdentity(candidate_);
		else {
			httpfs_identity_.canonical_uri = query_session_->Path();
		}
	}

	const HTTPFSOmRangeIdentityV2 &Identity() const noexcept override { return httpfs_identity_; }
	bool IsCancelled() const noexcept override {
		return cancelled_.load(std::memory_order_acquire) || context_->IsInterrupted();
	}
	HTTPFSOmReadClassV2 CurrentReadClass() const noexcept override {
		return read_class_.load(std::memory_order_relaxed);
	}
	void SetReadClass(HTTPFSOmReadClassV2 read_class) noexcept {
		read_class_.store(read_class, std::memory_order_relaxed);
	}

	void BeforeRequest(HTTPFSOmRequestV2 &request, std::map<std::string, std::string> &headers) override {
		if (request.method == "HEAD" && !request.effective_endpoint.empty()) {
			candidate_.endpoint = request.effective_endpoint;
		}
		if (httpfs_identity_.version_kind == HTTPFSOmVersionKindV2::StrongETag &&
		    !httpfs_identity_.version_token.empty()) {
			headers.emplace("If-Match", httpfs_identity_.version_token);
		}
	}

	bool AllowRedirect(const std::string &from_uri, const std::string &to_uri, bool carries_authorization) override {
		if (carries_authorization || redirects_ >= 5) return false;
		const auto from = ParseRemoteUri(from_uri);
		const auto to = ParseRemoteUri(to_uri);
		if (from.endpoint != to.endpoint || from.scheme != to.scheme) return false;
		++redirects_;
		return true;
	}

	void OnResponse(const HTTPFSOmResponseV2 &response) override {
		if (response.method == "HEAD") {
			if (response.status >= 300 && response.status < 400) return;
			if (response.status < 200 || response.status >= 300) {
				throw HTTPException("DuckOMO remote HEAD returned an unsuccessful status");
			}
			ObjectIdentity next = candidate_;
			next.size = ParsePositiveSize(Header(response, "Content-Length"), query_session_->Path());
			next.version_strength = VersionStrengthFrom(response, next.version_token);
			if (!bootstrap_) VerifySameIdentity(candidate_, next);
			candidate_ = next;
			SetHttpfsIdentity(candidate_);
			query_session_->ConfirmIdentity(candidate_);
			return;
		}
		if (response.method == "GET" && !response.transport_error && response.status > 0 &&
		    (response.status < 300 || response.status >= 400)) {
			VerifyResponseVersion(response);
		}
	}

	void OnBodyBytes(std::uint64_t request_id, std::uint64_t attempt, std::uint64_t received_bytes) override {
		std::lock_guard<std::mutex> guard(mutex_);
		body_bytes_[{request_id, attempt}] += received_bytes;
	}

	void OnAttemptComplete(const HTTPFSOmResponseV2 &response) override {
		std::uint64_t observed = 0;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			auto entry = body_bytes_.find({response.request_id, response.attempt});
			if (entry != body_bytes_.end()) {
				observed = entry->second;
				body_bytes_.erase(entry);
			}
		}
		if (observed != response.body_bytes) {
			if (metrics_) {
				metrics_->RecordTransportAttempt(response.request_id, response.attempt, response.status,
				                                 observed, response.status > 0, false);
			}
			throw HTTPException("DuckOMO httpfs observer body count was inconsistent");
		}
		if (metrics_) {
			metrics_->RecordTransportAttempt(response.request_id, response.attempt, response.status,
			                                 observed, response.status > 0, response.complete);
		}
	}

	ObjectIdentity Candidate() const { return candidate_; }

private:
	static void VerifySameIdentity(const ObjectIdentity &expected, const ObjectIdentity &actual) {
		if (expected.endpoint != actual.endpoint || expected.size != actual.size ||
		    expected.version_strength != actual.version_strength ||
		    expected.version_token != actual.version_token) {
			throw HTTPException("DuckOMO remote object identity changed during the query");
		}
	}

	void VerifyResponseVersion(const HTTPFSOmResponseV2 &response) const {
		const auto version_id = Header(response, "x-amz-version-id");
		const auto etag = Header(response, "ETag");
		if (candidate_.version_strength == ObjectVersionStrength::S3VersionId) {
			if (version_id.empty() || version_id != candidate_.version_token) {
				throw HTTPException("DuckOMO observed an S3 object version change");
			}
		} else if (candidate_.version_strength == ObjectVersionStrength::StrongETag) {
			if (etag.empty() || etag != candidate_.version_token) {
				throw HTTPException("DuckOMO observed an ETag change");
			}
		} else if (candidate_.version_strength == ObjectVersionStrength::Weak && !etag.empty() &&
		           etag != candidate_.version_token) {
			throw HTTPException("DuckOMO observed an ETag change");
		} else if (candidate_.version_strength == ObjectVersionStrength::None &&
		           (!version_id.empty() || !etag.empty())) {
			throw HTTPException("DuckOMO observed new object version evidence during the query");
		}
	}

	void SetHttpfsIdentity(const ObjectIdentity &identity) {
		httpfs_identity_.canonical_uri = query_session_->Path();
		httpfs_identity_.object_size = identity.size;
		httpfs_identity_.version_kind = HttpfsVersionKind(identity.version_strength);
		httpfs_identity_.version_token = identity.version_token;
	}

	ClientContext *context_;
	std::shared_ptr<RemoteReadSession> query_session_;
	std::shared_ptr<ScanMetrics> metrics_;
	bool bootstrap_;
	ObjectIdentity candidate_;
	HTTPFSOmRangeIdentityV2 httpfs_identity_;
	std::atomic<HTTPFSOmReadClassV2> read_class_{HTTPFSOmReadClassV2::Metadata};
	std::atomic<bool> cancelled_{false};
	std::uint64_t redirects_ = 0;
	std::mutex mutex_;
	std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> body_bytes_;
};

class RemoteFileOpener final : public ClientContextFileOpener, public HTTPFSOmRangeProviderV2 {
public:
	RemoteFileOpener(ClientContext &context, std::string uri, std::shared_ptr<RemoteHandleRangeSession> session)
	    : ClientContextFileOpener(context), uri_(std::move(uri)), session_(std::move(session)) {
	}
	std::uint32_t OmRangeAbiVersion() const noexcept override { return HTTPFS_OM_RANGE_ABI_VERSION; }
	std::shared_ptr<HTTPFSOmRangeSessionV2> OpenOmRangeSessionV2(const std::string &uri) override {
		if (uri != uri_) throw InvalidInputException("DuckOMO httpfs provider received an unexpected object URI");
		return session_;
	}

private:
	std::string uri_;
	std::shared_ptr<RemoteHandleRangeSession> session_;
};

class RemoteReadFile final : public ReadAtFile {
public:
	RemoteReadFile(ClientContext &context, std::shared_ptr<RemoteReadSession> session,
	               ScanMetadataStage metadata_stage, RangeCache *cache)
	    : query_session_(std::move(session)), path_(query_session_->Path()), metrics_(query_session_->Metrics()),
	      metadata_stage_(metadata_stage), cache_(cache), bootstrap_(!query_session_->HasIdentity()),
	      observer_(std::make_shared<RemoteHandleRangeSession>(context, query_session_, metrics_, bootstrap_)),
	      opener_(make_uniq<RemoteFileOpener>(context, query_session_->Uri(), observer_)),
	      file_system_(context.db->config.file_system.get()) {
		if (!file_system_) throw ReaderError(ReaderErrorCode::FileIo, "remote OM has no configured file system");
		if (!context.db->config.CanAccessFile(query_session_->Uri(), FileType::FILE_TYPE_REGULAR)) {
			throw ReaderError(ReaderErrorCode::FileIo, "remote OM object access is disabled for '" + path_ + "'");
		}
		FileOpenFlags flags(static_cast<idx_t>(1));
		flags.SetCachingMode(CachingMode::NO_CACHING);
		try {
			handle_ = file_system_->OpenFile(query_session_->Uri(), flags, opener_.get());
			if (!handle_) throw IOException("httpfs returned no object handle");
			const auto signed_size = handle_->GetFileSize();
			if (signed_size <= 0) throw IOException("remote object has no positive length");
			size_ = static_cast<std::uint64_t>(signed_size);
			const auto candidate = observer_->Candidate();
			if (candidate.size != size_) throw IOException("remote object size did not match its HEAD response");
			query_session_->ConfirmIdentity(candidate);
			identity_ = query_session_->Identity();
			UpdateCacheMetrics();
		} catch (const ReaderError &) {
			throw;
		} catch (const std::exception &) {
			throw ReaderError(ReaderErrorCode::FileIo,
			                  "could not establish a strict range session for remote OM object '" + path_ + "'");
		}
	}

	std::uint64_t Size() const noexcept override { return size_; }
	const std::string &Path() const noexcept override { return path_; }
	const std::shared_ptr<ScanMetrics> &Metrics() const noexcept override { return metrics_; }

	void ReadRange(std::uint64_t offset, std::uint64_t size, void *destination, ScanReadPhase phase,
	               const std::string &variable_path = std::string()) const override {
		if (offset > size_ || size > size_ - offset) {
			throw ReaderError(ReaderErrorCode::TruncatedFile, "remote OM read range is outside '" + path_ + "'");
		}
		if (size == 0) return;
		if (destination == nullptr) {
			throw ReaderError(ReaderErrorCode::InvalidSelection, "remote positional read destination must not be NULL");
		}
		if (size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
		    offset > static_cast<std::uint64_t>(std::numeric_limits<idx_t>::max())) {
			throw ReaderError(ReaderErrorCode::InvalidSelection, "remote OM read range exceeds DuckDB I/O limits");
		}
		if (metrics_) metrics_->RecordLogicalRead(phase, size, variable_path, metadata_stage_);
		if (cache_) {
			const auto hit = cache_->Read(identity_, offset, size, destination);
			if (metrics_) metrics_->RecordCacheLookup(hit, hit ? size : 0);
			UpdateCacheMetrics();
			if (hit) return;
		}
		observer_->SetReadClass(HttpfsReadClass(phase));
		try {
			file_system_->Read(*handle_, destination, static_cast<std::int64_t>(size), static_cast<idx_t>(offset));
		} catch (const std::exception &) {
			if (observer_->IsCancelled()) throw InterruptException();
			throw ReaderError(phase == ScanReadPhase::Index ? ReaderErrorCode::IndexRead : ReaderErrorCode::DataRead,
			                  "remote OM range read failed for '" + path_ + "'; transport details were redacted");
		}
		if (cache_) {
			cache_->Insert(identity_, offset, destination, size);
			UpdateCacheMetrics();
		}
		RecordReadMetric(metrics_, metadata_stage_, phase, size, variable_path);
	}

	std::vector<std::uint8_t> ReadRange(std::uint64_t offset, std::uint64_t size, ScanReadPhase phase,
	                                   const std::string &variable_path = std::string()) const override {
		if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
			throw ReaderError(ReaderErrorCode::Allocation, "remote OM read buffer exceeds addressable memory");
		}
		std::vector<std::uint8_t> result;
		try {
			result.resize(static_cast<std::size_t>(size));
		} catch (const std::bad_alloc &) {
			throw ReaderError(ReaderErrorCode::Allocation, "unable to allocate remote OM read buffer");
		}
		ReadRange(offset, size, result.data(), phase, variable_path);
		return result;
	}

private:
	void UpdateCacheMetrics() const {
		if (!metrics_ || !cache_) return;
		const auto stats = cache_->Stats();
		std::string bypass_reason;
		if (!identity_.Cacheable()) bypass_reason = "object_version_not_strong";
		else if (!stats.enabled) bypass_reason = "disabled";
		metrics_->SetCacheState(stats.enabled, stats.capacity_bytes, stats.accounted_bytes,
		                        stats.peak_charged_bytes, stats.control_bytes, stats.evictions,
		                        std::move(bypass_reason), VersionStrengthName(identity_.version_strength));
	}

	std::shared_ptr<RemoteReadSession> query_session_;
	std::string path_;
	std::shared_ptr<ScanMetrics> metrics_;
	ScanMetadataStage metadata_stage_;
	RangeCache *cache_;
	bool bootstrap_;
	std::shared_ptr<RemoteHandleRangeSession> observer_;
	unique_ptr<RemoteFileOpener> opener_;
	FileSystem *file_system_;
	unique_ptr<FileHandle> handle_;
	std::uint64_t size_ = 0;
	ObjectIdentity identity_;
};

} // namespace

RemoteReadSession::RemoteReadSession(std::string uri, std::string display_path, ObjectIdentity identity,
                                     std::shared_ptr<ScanMetrics> metrics)
    : uri_(std::move(uri)), display_path_(std::move(display_path)), identity_(std::move(identity)),
      metrics_(std::move(metrics)) {
}

std::shared_ptr<RemoteReadSession> RemoteReadSession::Create(ClientContext &context, std::string uri,
                                                             std::shared_ptr<ScanMetrics> metrics) {
	RequireHttpfsRangeCapability(context);
	if (metrics) metrics->SetTransportAvailable(true);
	const auto parsed = ParseRemoteUri(uri);
	if (!context.db->config.CanAccessFile(uri, FileType::FILE_TYPE_REGULAR)) {
		throw ReaderError(ReaderErrorCode::FileIo, "remote OM object access is disabled for '" + parsed.display_path + "'");
	}
	ObjectIdentity identity;
	identity.canonical_uri = uri;
	identity.endpoint = parsed.endpoint;
	identity.redacted_display_id = parsed.display_path;
	identity.access_partition = context.registered_state
	                                 ->GetOrCreate<RemoteAccessPartitionState>("duckomo.remote_access_partition")
	                                 ->Get();
	return std::shared_ptr<RemoteReadSession>(new RemoteReadSession(std::move(uri), parsed.display_path,
	                                                               std::move(identity), std::move(metrics)));
}

std::unique_ptr<ReadAtFile> RemoteReadSession::Open(ClientContext &context, ScanMetadataStage metadata_stage,
                                                   RangeCache *cache) {
	return std::make_unique<RemoteReadFile>(context, shared_from_this(), metadata_stage, cache);
}

const std::string &RemoteReadSession::Path() const noexcept { return display_path_; }
const std::string &RemoteReadSession::Uri() const noexcept { return uri_; }

ObjectIdentity RemoteReadSession::Identity() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return identity_;
}

bool RemoteReadSession::HasIdentity() const noexcept {
	std::lock_guard<std::mutex> guard(mutex_);
	return identity_initialized_;
}

const std::shared_ptr<ScanMetrics> &RemoteReadSession::Metrics() const noexcept { return metrics_; }

void RemoteReadSession::ConfirmIdentity(const ObjectIdentity &candidate) {
	std::lock_guard<std::mutex> guard(mutex_);
	if (!identity_initialized_) {
		identity_.endpoint = candidate.endpoint;
		identity_.size = candidate.size;
		identity_.version_strength = candidate.version_strength;
		identity_.version_token = candidate.version_token;
		identity_initialized_ = true;
		return;
	}
	if (identity_.endpoint != candidate.endpoint || identity_.size != candidate.size ||
	    identity_.version_strength != candidate.version_strength ||
	    identity_.version_token != candidate.version_token) {
		throw ReaderError(ReaderErrorCode::FileIo, "remote OM object identity changed for '" + display_path_ + "'");
	}
}

bool IsSupportedRemoteOmUri(const std::string &uri) noexcept {
	try {
		(void)ParseRemoteUri(uri);
		return true;
	} catch (...) {
		return false;
	}
}

std::string RedactRemoteUri(const std::string &uri) {
	return ParseRemoteUri(uri).display_path;
}

bool IsCompatibleHttpfsRangeDescriptor(const std::string &descriptor) noexcept {
	return descriptor == HTTPFS_OM_RANGE_CAPABILITY_DESCRIPTOR;
}

void RequireHttpfsRangeCapability(ClientContext &context) {
	try {
		auto &catalog = Catalog::GetSystemCatalog(*context.db);
		auto entry = catalog.GetEntry<TableFunctionCatalogEntry>(context, DEFAULT_SCHEMA,
		                                                        "httpfs_om_range_capabilities",
		                                                        OnEntryNotFound::RETURN_NULL);
		if (!entry || entry->functions.Size() != 1 ||
		    !IsCompatibleHttpfsRangeDescriptor(entry->functions.GetFunctionByOffset(0).extra_info)) {
			throw BinderException(
			    "remote read_om requires the paired httpfs extension with the matching DuckOMO range ABI; load the companion httpfs artifact first");
		}
	} catch (const BinderException &) {
		throw;
	} catch (...) {
		throw BinderException(
		    "remote read_om requires the paired httpfs extension with the matching DuckOMO range ABI; load the companion httpfs artifact first");
	}
}

} // namespace duckomo
} // namespace duckdb
