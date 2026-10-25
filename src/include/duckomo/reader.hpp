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

inline constexpr std::uint64_t DEFAULT_LOCAL_MERGE_GAP = 512;
inline constexpr std::uint64_t DEFAULT_LOCAL_MAX_REQUEST = 64 * 1024;

// Remote settings also carry the historical defaults for local reads.
struct RemoteReadBudget final {
	bool enabled = false;
	std::uint64_t merge_gap = DEFAULT_LOCAL_MERGE_GAP;
	std::uint64_t max_request = DEFAULT_LOCAL_MAX_REQUEST;
	bool async_requested = false;
	std::uint64_t max_inflight = 1;
	std::uint64_t prefetch_batches = 1;
	bool allow_read_ahead = true;
};

struct PlannedDataRange final {
	std::uint64_t read_offset = 0;
	std::uint64_t read_size = 0;
	bool reuse_range = false;
	std::uint64_t useful_bytes = 0;
	std::vector<OmDecoder_dataRead_t> ranges;
	std::shared_ptr<std::vector<std::uint8_t>> buffer;
};

struct PlannedIndexBlock final {
	OmDecoder_indexRead_t index_read{};
	std::shared_ptr<std::vector<std::uint8_t>> index_data;
	std::vector<PlannedDataRange> data_ranges;
};

struct PlannedSelection final {
	ScanDecodePurpose purpose = ScanDecodePurpose::Value;
	std::string variable_path;
	std::vector<std::uint64_t> read_offset;
	std::vector<std::uint64_t> read_count;
	std::vector<std::uint64_t> cube_offset;
	std::vector<std::uint64_t> cube_dimensions;
	std::uint64_t io_size_merge = 0;
	std::uint64_t io_size_max = 0;
	std::vector<PlannedIndexBlock> blocks;
	std::shared_ptr<ScanMemoryAccount> control_account;
};

// Read-only adapter around the official OM C Sans-I/O API. The file handle
// and root metadata live here; variables own their borrowed metadata bytes.
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

	// Cache a compact metadata directory for this reader only. Wide/sparse
	// directories fall back to ordinary reads; this is not a format constraint.
	void PrefetchMetadata(std::uint64_t offset, std::uint64_t size) const;
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

	// Same decode with an explicit remote budget. A null budget keeps local behaviour and records no remote useful bytes.

	void DecodeSelection(OmDecoderState &state, const std::string &variable_path,
	                     const std::vector<std::uint64_t> &read_offset,
	                     const std::vector<std::uint64_t> &read_count,
	                     const std::vector<std::uint64_t> &cube_offset,
                     const std::vector<std::uint64_t> &cube_dimensions, void *output,
                     std::uint64_t output_bytes, const RemoteReadBudget &budget,
                     ScanDecodePurpose purpose = ScanDecodePurpose::Value) const;

	PlannedSelection PlanSelection(OmDecoderState &state, const std::string &variable_path,
	                               const std::vector<std::uint64_t> &read_offset,
	                               const std::vector<std::uint64_t> &read_count,
	                               const std::vector<std::uint64_t> &cube_offset,
	                               const std::vector<std::uint64_t> &cube_dimensions,
	                               std::uint64_t output_bytes, std::uint64_t io_size_merge,
	                               std::uint64_t io_size_max, ScanDecodePurpose purpose,
	                               const RemoteReadBudget *budget, void *synchronous_output = nullptr) const;

	void FetchSelection(PlannedSelection &plan, const RemoteReadBudget *budget) const;

	void DecodeSelectionPlan(OmDecoderState &state, const PlannedSelection &plan, void *output,
	                         const RemoteReadBudget *budget) const;


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
	void DecodeSelectionImpl(OmDecoderState &state, const std::string &variable_path,
	                         const std::vector<std::uint64_t> &read_offset,
	                         const std::vector<std::uint64_t> &read_count,
	                         const std::vector<std::uint64_t> &cube_offset,
	                         const std::vector<std::uint64_t> &cube_dimensions, void *output,
	                         std::uint64_t output_bytes, std::uint64_t io_size_merge,
	                         std::uint64_t io_size_max, ScanDecodePurpose purpose,
	                         const RemoteReadBudget *budget) const;
	void ValidateBodyRange(std::uint64_t offset, std::uint64_t size, const char *phase) const;
	std::shared_ptr<const OwnedMetadataBuffer> ReadAndValidateMetadata(std::uint64_t offset,
	                                                                   std::uint64_t size) const;

	std::unique_ptr<ReadAtFile> file_;
	std::uint64_t trailer_offset_ = 0;
	std::uint64_t root_offset_ = 0;
	std::uint64_t root_size_ = 0;
	std::shared_ptr<const OwnedMetadataBuffer> root_metadata_;
	mutable std::uint64_t metadata_cache_offset_ = 0;
	mutable std::vector<std::uint8_t> metadata_cache_;
	mutable std::shared_ptr<ScanMemoryAccount> metadata_cache_account_;
};

} // namespace duckomo
} // namespace duckdb
