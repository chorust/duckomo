#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "duckomo/metrics.hpp"

extern "C" {
#include "om_decoder.h"
#include "om_variable.h"
}

namespace duckdb {
namespace duckomo {

constexpr std::size_t OM_MAX_RANK = 8;

// Reader errors retain a stable category while allowing callers to include the
// original OM error when a Sans-I/O or decode operation produced one.
enum class ReaderErrorCode : std::uint8_t {
	InvalidPath,
	FileIo,
	TruncatedFile,
	InvalidHeader,
	UnsupportedVersion,
	InvalidMetadata,
	UnsupportedNode,
	UnsupportedDataType,
	UnsupportedCompression,
	InvalidShape,
	ShapeOverflow,
	InvalidSelection,
	IndexRead,
	DataRead,
	Decode,
	Cancelled,
	Allocation
};

class ReaderError final : public std::runtime_error {
public:
	ReaderError(ReaderErrorCode error_code, std::string message, OmError_t om_error_code = ERROR_OK)
	    : std::runtime_error(std::move(message)), code_(error_code), om_error_(om_error_code) {
	}

	ReaderErrorCode Code() const noexcept {
		return code_;
	}

	OmError_t OmError() const noexcept {
		return om_error_;
	}

private:
	ReaderErrorCode code_;
	OmError_t om_error_;
};

// A checked row count for the supported positive-rank OM array subset. The
// signed 64-bit ceiling matches DuckDB's row-count limit in this scanner.
inline std::uint64_t CheckedShapeProduct(const std::uint64_t *shape, std::size_t rank) {
	if (shape == nullptr || rank == 0 || rank > OM_MAX_RANK) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "OM array rank must be between 1 and 8");
	}
	constexpr auto MAX_ROW_COUNT = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
	std::uint64_t product = 1;
	for (std::size_t axis = 0; axis < rank; axis++) {
		const auto length = shape[axis];
		if (length == 0) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "OM array dimensions must be positive");
		}
		if (product > MAX_ROW_COUNT / length) {
			throw ReaderError(ReaderErrorCode::ShapeOverflow, "OM array row count exceeds the signed 64-bit limit");
		}
		product *= length;
	}
	return product;
}

inline std::uint64_t CheckedShapeProduct(const std::vector<std::uint64_t> &shape) {
	return CheckedShapeProduct(shape.data(), shape.size());
}

// OM variable handles and all pointers returned by its getters alias the
// metadata bytes. This immutable owner is shared by every borrowed view.
class OwnedMetadataBuffer final {
public:
	explicit OwnedMetadataBuffer(std::vector<std::uint8_t> buffer,
	                             std::shared_ptr<ScanMetrics> metrics = nullptr)
	    : bytes_(std::move(buffer)) {
		if (metrics) {
			memory_account_ = std::make_shared<ScanMemoryAccount>(std::move(metrics));
			memory_account_->Set(sizeof(*this) + bytes_.capacity());
		}
	}

	const void *Data() const noexcept {
		return bytes_.data();
	}

	std::size_t Size() const noexcept {
		return bytes_.size();
	}

	std::size_t Capacity() const noexcept {
		return bytes_.capacity();
	}

private:
	std::vector<std::uint8_t> bytes_;
	std::shared_ptr<ScanMemoryAccount> memory_account_;
};

// The OM handle itself is borrowed. Retaining the shared owner in the same
// object guarantees that the handle, name, dimensions, and chunks stay valid.
class BorrowedOmVariable final {
public:
	explicit BorrowedOmVariable(std::shared_ptr<const OwnedMetadataBuffer> owner)
	    : metadata_owner_(std::move(owner)), variable_(nullptr) {
		if (!metadata_owner_ || metadata_owner_->Size() == 0) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM variable metadata buffer is empty");
		}
		variable_ = om_variable_init(metadata_owner_->Data());
		if (variable_ == nullptr) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM variable metadata could not be initialized");
		}
	}

	const OmVariable_t *Get() const noexcept {
		return variable_;
	}

	const std::shared_ptr<const OwnedMetadataBuffer> &MetadataOwner() const noexcept {
		return metadata_owner_;
	}

private:
	// Keep this member before `variable_`; the borrowed handle must never outlive it.
	std::shared_ptr<const OwnedMetadataBuffer> metadata_owner_;
	const OmVariable_t *variable_;
};

// One decoder request owns every parameter array retained by OmDecoder_t,
// together with its I/O and chunk scratch. OM's dimensions/chunks point into
// `variable`'s metadata owner. Do not resize parameter or scratch buffers while
// the decoder is being used. Keep instances behind unique_ptr in query state;
// moving this object after initialization would invalidate decoder pointers.
struct OmDecoderState final {
	explicit OmDecoderState(BorrowedOmVariable borrowed_variable)
	    : variable_owner(std::move(borrowed_variable)) {
	}

	OmDecoderState(const OmDecoderState &) = delete;
	OmDecoderState &operator=(const OmDecoderState &) = delete;
	OmDecoderState(OmDecoderState &&) = delete;
	OmDecoderState &operator=(OmDecoderState &&) = delete;

	BorrowedOmVariable variable_owner;
	std::vector<std::uint64_t> read_offset;
	std::vector<std::uint64_t> read_count;
	std::vector<std::uint64_t> cube_offset;
	std::vector<std::uint64_t> cube_dimensions;
	OmDecoder_t decoder{};
	std::vector<std::uint8_t> index_bytes;
	std::vector<std::uint8_t> data_bytes;
	std::vector<std::uint8_t> chunk_scratch;
	std::shared_ptr<ScanMemoryAccount> memory_account;
};

// Mutable state belongs to one bound/executing query. No decoder, cursor, or
// buffer is shared across independent scans.
struct OmQueryReaderState final {
	std::vector<std::uint64_t> shape;
	std::uint64_t row_count = 0;
	std::uint64_t next_linear_index = 0;
	std::uint64_t batch_count = 0;
	std::vector<std::unique_ptr<OmDecoderState>> decoders;
};

} // namespace duckomo
} // namespace duckdb
