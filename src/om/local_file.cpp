#include "duckomo/local_file.hpp"

#include <cctype>
#include <limits>
#include <new>
#include <sstream>
#include <utility>

#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {

namespace {

bool IsUriPath(const std::string &path) {
	auto colon = path.find(':');
	if (colon == std::string::npos || colon == 0) {
		return false;
	}
	// A colon after a slash belongs to a local filename, not its URI scheme.
	auto slash = path.find_first_of("/\\");
	if (slash != std::string::npos && slash < colon) {
		return false;
	}
	if (!std::isalpha(static_cast<unsigned char>(path[0]))) {
		return false;
	}
	for (std::size_t i = 1; i < colon; i++) {
		auto ch = static_cast<unsigned char>(path[i]);
		if (!std::isalnum(ch) && ch != '+' && ch != '-' && ch != '.') {
			return false;
		}
	}
	return true;
}

bool IsGlobPath(const std::string &path) {
	return path.find_first_of("*?[]{}") != std::string::npos;
}

std::string FileErrorMessage(const std::string &action, const std::string &path, const std::exception &exception) {
	return action + " local OM file '" + path + "': " + exception.what();
}

} // namespace

void ValidateLocalFilePath(const std::string &path) {
	if (path.empty()) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "local OM file path must not be empty");
	}
	if (IsUriPath(path)) {
		auto colon = path.find(':');
		auto scheme = path.substr(0, colon);
		std::transform(scheme.begin(), scheme.end(), scheme.begin(),
		               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		if (scheme == "http" || scheme == "https" || scheme == "s3") {
			throw ReaderError(ReaderErrorCode::InvalidPath,
			                  "remote read_om requires the paired httpfs range-session extension; this build has no compatible remote range provider");
		}
		throw ReaderError(ReaderErrorCode::InvalidPath, "URI paths are not supported by the local OM reader");
	}
	if (IsGlobPath(path)) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "glob paths are not supported: '" + path + "'");
	}
}

std::string LocalFilePathFromValue(const Value &path_value) {
	if (path_value.IsNull()) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "OM path must not be NULL");
	}
	if (path_value.type().id() != LogicalTypeId::VARCHAR) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "OM path must be a constant VARCHAR value");
	}
	const auto path = path_value.GetValue<std::string>();
	ValidateLocalFilePath(path);
	return path;
}

LocalFile::LocalFile(FileSystem &file_system_p, std::unique_ptr<FileHandle> handle_p, std::string path_p,
                     std::uint64_t file_size_p, std::shared_ptr<ScanMetrics> metrics_p,
                     ScanMetadataStage metadata_stage_p)
	: file_system(&file_system_p), handle(std::move(handle_p)), path(std::move(path_p)), file_size(file_size_p),
	  metrics(std::move(metrics_p)), metadata_stage(metadata_stage_p) {
}

LocalFile LocalFile::Open(ClientContext &context, const std::string &path, std::shared_ptr<ScanMetrics> metrics,
                          ScanMetadataStage metadata_stage) {
	ValidateLocalFilePath(path);
	auto &file_system = FileSystem::GetLocal(*context.db);
	try {
		if (file_system.DirectoryExists(path)) {
			throw ReaderError(ReaderErrorCode::InvalidPath, "directories are not supported as OM input: '" + path + "'");
		}
	} catch (const ReaderError &) {
		throw;
	} catch (const std::exception &exception) {
		throw ReaderError(ReaderErrorCode::FileIo, FileErrorMessage("could not inspect", path, exception));
	}

	std::unique_ptr<FileHandle> handle;
	try {
		handle = file_system.OpenFile(path, FileFlags::FILE_FLAGS_READ);
	} catch (const std::exception &exception) {
		throw ReaderError(ReaderErrorCode::FileIo, FileErrorMessage("could not open", path, exception));
	}
	if (!handle) {
		throw ReaderError(ReaderErrorCode::FileIo, "could not open local OM file '" + path + "'");
	}
	if (handle->GetType() == FileType::FILE_TYPE_DIR) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "directories are not supported as OM input: '" + path + "'");
	}
	if (handle->GetType() != FileType::FILE_TYPE_REGULAR) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "OM input must be a regular local file: '" + path + "'");
	}

	idx_t size;
	try {
		size = handle->GetFileSize();
	} catch (const std::exception &exception) {
		throw ReaderError(ReaderErrorCode::FileIo, FileErrorMessage("could not read the size of", path, exception));
	}
	return LocalFile(file_system, std::move(handle), path, static_cast<std::uint64_t>(size), std::move(metrics),
	                  metadata_stage);
}

std::uint64_t LocalFile::Size() const noexcept {
	return file_size;
}

const std::string &LocalFile::Path() const noexcept {
	return path;
}

void LocalFile::ReadRange(std::uint64_t offset, std::uint64_t size, void *destination) const {
	ReadRangeInternal(offset, size, destination, nullptr, std::string());
}

void LocalFile::ReadRange(std::uint64_t offset, std::uint64_t size, void *destination, ScanReadPhase phase,
                          const std::string &variable_path) const {
	ReadRangeInternal(offset, size, destination, &phase, variable_path);
}

void LocalFile::ReadRangeInternal(std::uint64_t offset, std::uint64_t size, void *destination,
                                  const ScanReadPhase *phase, const std::string &variable_path) const {
	if (offset > file_size || size > file_size - offset) {
		std::ostringstream message;
		message << "read range [" << offset << ", ";
		if (size > std::numeric_limits<std::uint64_t>::max() - offset) {
			message << "overflow";
		} else {
			message << offset + size;
		}
		message << ") is outside local OM file '" << path << "' (" << file_size << " bytes)";
		throw ReaderError(ReaderErrorCode::TruncatedFile, message.str());
	}
	if (size == 0) {
		return;
	}
	if (destination == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "positional read destination must not be NULL");
	}
	if (size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
	    offset > static_cast<std::uint64_t>(std::numeric_limits<idx_t>::max())) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "local OM read range exceeds DuckDB positional I/O limits");
	}
	try {
		if (metrics && phase != nullptr) {
			metrics->RecordLogicalRead(*phase, size, variable_path,
			                           *phase == ScanReadPhase::Metadata ? metadata_stage : ScanMetadataStage::Scan);
		}
		file_system->Read(*handle, destination, static_cast<std::int64_t>(size), static_cast<idx_t>(offset));
	} catch (const std::exception &exception) {
		throw ReaderError(ReaderErrorCode::FileIo, FileErrorMessage("could not read", path, exception));
	}
	if (metrics && phase != nullptr) {
		if (*phase == ScanReadPhase::Metadata) {
			metrics->RecordMetadataRead(metadata_stage, size);
		} else if (*phase == ScanReadPhase::Coordinate) {
			metrics->RecordCoordinateRead(size);
		} else {
			metrics->RecordSuccessfulRead(*phase, size, variable_path);
		}
	}
}

std::vector<std::uint8_t> LocalFile::ReadRange(std::uint64_t offset, std::uint64_t size) const {
	if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
		throw ReaderError(ReaderErrorCode::Allocation, "local OM read buffer exceeds addressable memory");
	}
	std::vector<std::uint8_t> result;
	try {
		result.resize(static_cast<std::size_t>(size));
	} catch (const std::bad_alloc &) {
		throw ReaderError(ReaderErrorCode::Allocation, "unable to allocate local OM read buffer");
	}
	ReadRange(offset, size, result.data());
	return result;
}

std::vector<std::uint8_t> LocalFile::ReadRange(std::uint64_t offset, std::uint64_t size, ScanReadPhase phase,
                                               const std::string &variable_path) const {
	if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
		throw ReaderError(ReaderErrorCode::Allocation, "local OM read buffer exceeds addressable memory");
	}
	std::vector<std::uint8_t> result;
	try {
		result.resize(static_cast<std::size_t>(size));
	} catch (const std::bad_alloc &) {
		throw ReaderError(ReaderErrorCode::Allocation, "unable to allocate local OM read buffer");
	}
	ReadRange(offset, size, result.data(), phase, variable_path);
	return result;
}

const std::shared_ptr<ScanMetrics> &LocalFile::Metrics() const noexcept {
	return metrics;
}

} // namespace duckomo
} // namespace duckdb
