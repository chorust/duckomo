#include "duckomo/remote_file.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/file_open_flags.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_file_opener.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/database.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/om_reader.hpp"

namespace duckdb { namespace duckomo {
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

// All overrides are local to this opener. The standard superclass supplies the
// current context, secrets, logger and HTTP utility; no global SET during scans.
class RemoteFileOpener final : public ClientContextFileOpener {
public:
    explicit RemoteFileOpener(ClientContext &context) : ClientContextFileOpener(context) {}
    SettingLookupResult TryGetCurrentSetting(const TableFunctionColumnName &key, Value &value,
                                             FileOpenerInfo &info) override {
        if (Override(key, value)) return SettingLookupResult(SettingScope::LOCAL);
        return ClientContextFileOpener::TryGetCurrentSetting(key, value, info);
    }
    SettingLookupResult TryGetCurrentSetting(const TableFunctionColumnName &key, Value &value) override {
        if (Override(key, value)) return SettingLookupResult(SettingScope::LOCAL);
        return ClientContextFileOpener::TryGetCurrentSetting(key, value);
    }
private:
    static bool Override(const TableFunctionColumnName &key, Value &value) {
        const auto name = IdentifierNameString(key);
        if (name == "force_download" || name == "auto_fallback_to_full_download" ||
            name == "unsafe_disable_etag_checks") { value = Value::BOOLEAN(false); return true; }
        if (name == "force_download_threshold") { value = Value::UBIGINT(0); return true; }
        if (name == "s3_version_id_pinning") { value = Value::BOOLEAN(true); return true; }
        return false;
    }
};

class RemoteReadFile final : public ReadAtFile {
public:
    RemoteReadFile(ClientContext &context, std::shared_ptr<RemoteReadSession> session, ScanMetadataStage stage)
        : context_(context), session_(std::move(session)), stage_(stage), opener_(context),
          file_system_(context.db->config.file_system.get()) {
        if (!file_system_) throw ReaderError(ReaderErrorCode::FileIo, "remote OM has no configured file system");
        if (!context.db->config.CanAccessFile(session_->Uri(), FileType::FILE_TYPE_REGULAR))
            throw ReaderError(ReaderErrorCode::FileIo, "remote OM access is disabled for '" + Path() + "'");
        CheckInterrupt();
        // Let the OM reader choose ranges: the standard DirectIO flag avoids
        // HTTPFS read-ahead on small objects without a private HTTPFS interface.
        FileOpenFlags flags(FileFlags::FILE_FLAGS_READ | FileFlags::FILE_FLAGS_DIRECT_IO |
                            FileFlags::FILE_FLAGS_DISABLE_LOGGING);
        flags.SetCachingMode(CachingMode::NO_CACHING);
        try {
            handle_ = file_system_->OpenFile(session_->Uri(), flags, &opener_);
            if (!handle_) throw IOException("no handle");
            const auto length = file_system_->GetFileSize(*handle_);
            if (length <= 0) throw IOException("no positive length");
            size_ = static_cast<std::uint64_t>(length);
            session_->ConfirmIdentity(size_, file_system_->GetVersionTag(*handle_));
            CheckInterrupt();
        } catch (const InterruptException &) { throw;
        } catch (const ReaderError &) { throw;
        } catch (const std::exception &) {
            CheckInterrupt();
            throw ReaderError(ReaderErrorCode::FileIo, "could not open remote OM object '" + Path() +
                "'; install/load the matching official httpfs and check access configuration (details redacted)");
        }
        memory_account_ = std::make_shared<ScanMemoryAccount>(Metrics(), ScanMemoryComponent::TransportControl);
        memory_account_->Set(sizeof(*this)); // HTTPFS handle/buffer memory is outside the DuckOMO ledger.
    }
    std::uint64_t Size() const noexcept override { return size_; }
    const std::string &Path() const noexcept override { return session_->Path(); }
    const std::shared_ptr<ScanMetrics> &Metrics() const noexcept override { return session_->Metrics(); }
    void ReadRange(std::uint64_t offset, std::uint64_t size, void *destination, ScanReadPhase phase,
                   const std::string &variable_path = std::string()) const override {
        CheckInterrupt();
        if (offset > size_ || size > size_ - offset)
            throw ReaderError(ReaderErrorCode::TruncatedFile, "remote OM read range is outside '" + Path() + "'");
        if (!size) return;
        if (!destination) throw ReaderError(ReaderErrorCode::InvalidSelection, "remote read destination must not be NULL");
        if (size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            offset > static_cast<std::uint64_t>(std::numeric_limits<idx_t>::max()))
            throw ReaderError(ReaderErrorCode::InvalidSelection, "remote OM range exceeds DuckDB I/O limits");
        if (Metrics()) Metrics()->RecordLogicalRead(phase, size, variable_path, stage_);
        try {
            // DuckDB's positional overload fulfills the entire range or throws.
            file_system_->Read(*handle_, destination, static_cast<std::int64_t>(size), static_cast<idx_t>(offset));
            CheckInterrupt();
        } catch (const InterruptException &) { throw;
        } catch (const std::exception &) {
            CheckInterrupt();
            throw ReaderError(phase == ScanReadPhase::Index ? ReaderErrorCode::IndexRead : ReaderErrorCode::DataRead,
                              "remote OM range read failed for '" + Path() + "'; transport details redacted");
        }
        RecordReadMetric(Metrics(), stage_, phase, size, variable_path);
    }
    std::vector<std::uint8_t> ReadRange(std::uint64_t offset, std::uint64_t size, ScanReadPhase phase,
                                       const std::string &variable_path = std::string()) const override {
        // Reject invalid ranges before allocating a buffer.
        if (offset > size_ || size > size_ - offset)
            throw ReaderError(ReaderErrorCode::TruncatedFile, "remote OM read range is outside '" + Path() + "'");
        std::vector<std::uint8_t> result;
        if (size > result.max_size()) throw ReaderError(ReaderErrorCode::Allocation, "remote OM buffer exceeds memory limits");
        try { result.resize(static_cast<std::size_t>(size)); }
        catch (const std::bad_alloc &) { throw ReaderError(ReaderErrorCode::Allocation, "unable to allocate remote OM buffer"); }
        ReadRange(offset, size, result.data(), phase, variable_path);
        return result;
    }
private:
    void CheckInterrupt() const { if (context_.IsInterrupted()) throw InterruptException(); }
    ClientContext &context_;
    std::shared_ptr<RemoteReadSession> session_;
    ScanMetadataStage stage_;
    RemoteFileOpener opener_;
    FileSystem *file_system_;
    unique_ptr<FileHandle> handle_; // Destroy before opener and context dependencies.
    std::uint64_t size_ = 0;
    std::shared_ptr<ScanMemoryAccount> memory_account_;
};
} // namespace

RemoteReadSession::RemoteReadSession(std::string uri, std::string path, std::shared_ptr<ScanMetrics> metrics)
    : uri_(std::move(uri)), display_path_(std::move(path)), metrics_(std::move(metrics)) {
    memory_account_ = std::make_shared<ScanMemoryAccount>(metrics_, ScanMemoryComponent::TransportControl);
    RefreshMemoryAccount();
}
std::shared_ptr<RemoteReadSession> RemoteReadSession::Create(ClientContext &context, std::string uri,
                                                            std::shared_ptr<ScanMetrics> metrics) {
    const auto parsed = ParseRemoteUri(uri);
    if (!context.db->config.CanAccessFile(uri, FileType::FILE_TYPE_REGULAR))
        throw ReaderError(ReaderErrorCode::FileIo, "remote OM access is disabled for '" + parsed.display_path + "'");
    if (metrics) metrics->SetTransportUnobserved();
    return std::shared_ptr<RemoteReadSession>(new RemoteReadSession(std::move(uri), parsed.display_path, std::move(metrics)));
}
std::unique_ptr<ReadAtFile> RemoteReadSession::Open(ClientContext &context, ScanMetadataStage stage) {
    return std::make_unique<RemoteReadFile>(context, shared_from_this(), stage);
}
const std::string &RemoteReadSession::Path() const noexcept { return display_path_; }
const std::string &RemoteReadSession::Uri() const noexcept { return uri_; }
const std::shared_ptr<ScanMetrics> &RemoteReadSession::Metrics() const noexcept { return metrics_; }
void RemoteReadSession::ConfirmIdentity(std::uint64_t size, const std::string &tag) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (identity_initialized_ && (size != size_ || tag != version_tag_))
        throw ReaderError(ReaderErrorCode::FileIo, "remote OM object identity changed for '" + display_path_ + "'");
    size_ = size;
    version_tag_ = tag;
    identity_initialized_ = true;
    RefreshMemoryAccount();
}
void RemoteReadSession::RefreshMemoryAccount() const {
    if (memory_account_) memory_account_->Set(sizeof(*this) + uri_.capacity() + 1 + display_path_.capacity() + 1 + version_tag_.capacity() + 1);
}
bool IsSupportedRemoteOmUri(const std::string &uri) noexcept {
    try { (void)ParseRemoteUri(uri); return true; } catch (...) { return false; }
}
std::string RedactRemoteUri(const std::string &uri) { return ParseRemoteUri(uri).display_path; }
} } // namespace duckdb::duckomo
