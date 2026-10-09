#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "duckomo/read_at_file.hpp"

// The pinned upstream headers are C headers without their own C++ linkage
// guards. Include them here first so this adapter and its C++ callers link to
// the official C symbols.
extern "C" {
#include "om_decoder.h"
}

#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
class LocalFile;

// Local, read-only adapter around the official OM C Sans-I/O API. The file
// handle and root metadata live with this object; each returned variable owns
// the metadata bytes behind its borrowed OM handle.
class OmV3Reader final {
public:
	explicit OmV3Reader(LocalFile file);
	explicit OmV3Reader(std::unique_ptr<ReadAtFile> file);

	OmV3Reader(const OmV3Reader &) = delete;
	OmV3Reader &operator=(const OmV3Reader &) = delete;
	OmV3Reader(OmV3Reader &&) noexcept = default;
	OmV3Reader &operator=(OmV3Reader &&) noexcept = default;

	std::uint64_t RootOffset() const noexcept;
	std::uint64_t RootSize() const noexcept;

	BorrowedOmVariable ReadRootVariable() const;
	BorrowedOmVariable ReadVariable(std::uint64_t offset, std::uint64_t size) const;

	// Configure one logical slice and decode it into `output`. Decoder-owned
	// selection arrays and scratch stay alive in `state` and may be reused for
	// subsequent slices with the same rank. The official io_size_max argument
	// guides request splitting/merging; it is not a hard cap on an encoded data
	// block or the corresponding reader buffer.
	void DecodeSelection(OmDecoderState &state, const std::string &variable_path,
	                     const std::vector<std::uint64_t> &read_offset,
	                     const std::vector<std::uint64_t> &read_count,
	                     const std::vector<std::uint64_t> &cube_offset,
                     const std::vector<std::uint64_t> &cube_dimensions, void *output,
                     std::uint64_t output_bytes, std::uint64_t io_size_merge = 512,
                     std::uint64_t io_size_max = 64 * 1024,
                     ScanDecodePurpose purpose = ScanDecodePurpose::Value) const;

	// Keep source compatibility for raw scans, which do not attach query metrics.
	void DecodeSelection(OmDecoderState &state, const std::vector<std::uint64_t> &read_offset,
	                     const std::vector<std::uint64_t> &read_count,
	                     const std::vector<std::uint64_t> &cube_offset,
                     const std::vector<std::uint64_t> &cube_dimensions, void *output,
                     std::uint64_t output_bytes, std::uint64_t io_size_merge = 512,
                     std::uint64_t io_size_max = 64 * 1024,
                     ScanDecodePurpose purpose = ScanDecodePurpose::Value) const;

	const ReadAtFile &File() const noexcept;

private:
	void ValidateBodyRange(std::uint64_t offset, std::uint64_t size, const char *phase) const;
	std::shared_ptr<const OwnedMetadataBuffer> ReadAndValidateMetadata(std::uint64_t offset,
	                                                                   std::uint64_t size) const;

	std::unique_ptr<ReadAtFile> file_;
	std::uint64_t trailer_offset_ = 0;
	std::uint64_t root_offset_ = 0;
	std::uint64_t root_size_ = 0;
	std::shared_ptr<const OwnedMetadataBuffer> root_metadata_;
};

} // namespace duckomo
} // namespace duckdb
