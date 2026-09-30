#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "duckdb/common/file_system.hpp"
#include "duckomo/metrics.hpp"

namespace duckdb {
class ClientContext;

namespace duckomo {

// Checked positional-read boundary shared by local and query-scoped remote
// OM sessions. Implementations own the handle and report only completed reads.
class ReadAtFile {
public:
	virtual ~ReadAtFile() = default;
	virtual std::uint64_t Size() const noexcept = 0;
	virtual const std::string &Path() const noexcept = 0;
	virtual void ReadRange(std::uint64_t offset, std::uint64_t size, void *destination,
	                       ScanReadPhase phase, const std::string &variable_path = std::string()) const = 0;
	virtual std::vector<std::uint8_t> ReadRange(std::uint64_t offset, std::uint64_t size,
	                                            ScanReadPhase phase,
	                                            const std::string &variable_path = std::string()) const = 0;
	virtual const std::shared_ptr<ScanMetrics> &Metrics() const noexcept = 0;
};

// Owns one read-only local file handle. OM's Sans-I/O requests are serviced by
// exact positional reads so the reader never discovers paths or byte ranges.
class LocalFile final : public ReadAtFile {
public:
	static LocalFile Open(ClientContext &context, const std::string &path,
	                      std::shared_ptr<ScanMetrics> metrics = nullptr,
	                      ScanMetadataStage metadata_stage = ScanMetadataStage::Scan);

	LocalFile(const LocalFile &) = delete;
	LocalFile &operator=(const LocalFile &) = delete;
	LocalFile(LocalFile &&other) noexcept = default;
	LocalFile &operator=(LocalFile &&other) noexcept = default;

	std::uint64_t Size() const noexcept override;
	const std::string &Path() const noexcept override;

	void ReadRange(std::uint64_t offset, std::uint64_t size, void *destination) const;
	std::vector<std::uint8_t> ReadRange(std::uint64_t offset, std::uint64_t size) const;
	void ReadRange(std::uint64_t offset, std::uint64_t size, void *destination, ScanReadPhase phase,
	               const std::string &variable_path = std::string()) const override;
	std::vector<std::uint8_t> ReadRange(std::uint64_t offset, std::uint64_t size, ScanReadPhase phase,
	                                    const std::string &variable_path = std::string()) const override;

	const std::shared_ptr<ScanMetrics> &Metrics() const noexcept override;

private:
	LocalFile(FileSystem &file_system, std::unique_ptr<FileHandle> handle, std::string path,
	          std::uint64_t file_size, std::shared_ptr<ScanMetrics> metrics, ScanMetadataStage metadata_stage);
	void ReadRangeInternal(std::uint64_t offset, std::uint64_t size, void *destination,
	                       const ScanReadPhase *phase, const std::string &variable_path) const;

	FileSystem *file_system;
	std::unique_ptr<FileHandle> handle;
	std::string path;
	std::uint64_t file_size;
	std::shared_ptr<ScanMetrics> metrics;
	ScanMetadataStage metadata_stage = ScanMetadataStage::Scan;
};

// Reject URL-like inputs and shell-style glob patterns before asking the local
// filesystem to open a path.
void ValidateLocalFilePath(const std::string &path);
std::string LocalFilePathFromValue(const Value &path_value);

} // namespace duckomo
} // namespace duckdb
