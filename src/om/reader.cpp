extern "C" {
#include "om_compression.h"
}
#include "duckomo/reader.hpp"
#include "duckomo/local_file.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace duckdb {
namespace duckomo {
namespace {
std::uint64_t UsefulDataBytes(const OmDecoder_t &decoder, const std::uint8_t *index_data,
                          std::uint64_t index_count, const OmDecoder_dataRead_t &data_read) {
	if (decoder.lut_chunk_length == 0) return data_read.count;
	const auto chunk_length = decoder.lut_chunk_length;
	const auto lut_offset = data_read.indexRange.lowerBound / LUT_CHUNK_COUNT * chunk_length;
	std::uint64_t useful = 0;
	std::uint64_t loaded = UINT64_MAX;
	std::uint64_t lut[LUT_CHUNK_COUNT] = {};
	for (auto chunk = data_read.chunkIndex.lowerBound; chunk < data_read.chunkIndex.upperBound; chunk++) {
		const auto lut_chunk = chunk / LUT_CHUNK_COUNT;
		if (lut_chunk != loaded) {
			const auto elements = std::min<std::uint64_t>((lut_chunk + 1) * LUT_CHUNK_COUNT,
			                                              decoder.number_of_chunks + 1) - lut_chunk * LUT_CHUNK_COUNT;
			const auto start = lut_chunk * chunk_length - lut_offset;
			if (start > index_count || chunk_length > index_count - start || elements > LUT_CHUNK_COUNT) return data_read.count;
			if (om_pfor_decode(index_data + start, static_cast<std::size_t>(chunk_length),
			                   static_cast<std::size_t>(elements), lut, 64, false) == SIZE_MAX) return data_read.count;
			loaded = lut_chunk;
		}
		// Merged chunkIndex intervals include unselected chunks. Count only
		// chunks intersecting the requested selection in every dimension.
		bool selected = true;
		auto position = chunk;
		for (std::uint64_t axis = decoder.dimensions_count; axis-- > 0;) {
			const auto dimension = decoder.dimensions[axis];
			const auto width = decoder.chunks[axis];
			const auto chunks = dimension / width + (dimension % width != 0);
			const auto start = (position % chunks) * width;
			position /= chunks;
			if (start >= decoder.read_offset[axis] + decoder.read_count[axis] ||
			    std::min(dimension, start + width) <= decoder.read_offset[axis]) selected = false;
		}
		if (!selected) continue;
		const auto begin = lut[chunk % LUT_CHUNK_COUNT];
		const auto next_chunk = (chunk + 1) / LUT_CHUNK_COUNT;
		std::uint64_t end_value;
		if (next_chunk != loaded) {
			std::uint64_t next_lut[LUT_CHUNK_COUNT] = {};
			const auto elements = std::min<std::uint64_t>((next_chunk + 1) * LUT_CHUNK_COUNT,
			                                              decoder.number_of_chunks + 1) - next_chunk * LUT_CHUNK_COUNT;
			const auto start = next_chunk * chunk_length - lut_offset;
			if (start > index_count || chunk_length > index_count - start || elements > LUT_CHUNK_COUNT) return data_read.count;
			if (om_pfor_decode(index_data + start, static_cast<std::size_t>(chunk_length),
			                   static_cast<std::size_t>(elements), next_lut, 64, false) == SIZE_MAX) return data_read.count;
			end_value = next_lut[(chunk + 1) % LUT_CHUNK_COUNT];
		} else {
			end_value = lut[(chunk + 1) % LUT_CHUNK_COUNT];
		}
		if (end_value < begin) return data_read.count;
		useful += end_value - begin;
	}
	return useful;
}

} // namespace


namespace {

const char *OmErrorText(OmError_t error) {
	const auto *message = om_error_string(error);
	return message == nullptr ? "unknown OM error" : message;
}

ReaderErrorCode TranslateOmError(OmError_t error, ReaderErrorCode fallback) {
	switch (error) {
	case ERROR_INVALID_COMPRESSION_TYPE:
		return ReaderErrorCode::UnsupportedCompression;
	case ERROR_INVALID_DATA_TYPE:
		return ReaderErrorCode::UnsupportedDataType;
	case ERROR_OUT_OF_BOUND_READ:
		return ReaderErrorCode::TruncatedFile;
	case ERROR_NOT_AN_OM_FILE:
		return ReaderErrorCode::InvalidMetadata;
	case ERROR_DEFLATED_SIZE_MISMATCH:
		return ReaderErrorCode::Decode;
	case ERROR_INVALID_DIMENSIONS:
	case ERROR_INVALID_CHUNK_DIMENSIONS:
		return ReaderErrorCode::InvalidShape;
	case ERROR_INVALID_READ_OFFSET:
	case ERROR_INVALID_READ_COUNT:
	case ERROR_INVALID_CUBE_OFFSET:
		return ReaderErrorCode::InvalidSelection;
	case ERROR_OK:
		return fallback;
	}
	return fallback;
}

[[noreturn]] void ThrowOmError(OmError_t error, ReaderErrorCode fallback, const std::string &path,
	                            const char *phase) {
	std::ostringstream message;
	message << phase << " failed for local OM file '" << path << "': " << OmErrorText(error) << " (OM error "
	        << static_cast<unsigned int>(error) << ")";
	throw ReaderError(TranslateOmError(error, fallback), message.str(), error);
}

[[noreturn]] void ThrowAllocationError(const char *message) {
	throw ReaderError(ReaderErrorCode::Allocation, message);
}

// The account follows shared buffer ownership, including a cached last range.
std::shared_ptr<std::vector<std::uint8_t>> AllocateReadBuffer(
    std::uint64_t size, const std::shared_ptr<ScanMetrics> &metrics) {
	try {
		auto account = std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Decoder);
		auto buffer = std::shared_ptr<std::vector<std::uint8_t>>(
		    new std::vector<std::uint8_t>(static_cast<std::size_t>(size)),
		    [account](std::vector<std::uint8_t> *value) { delete value; });
		account->Set(sizeof(*buffer) + buffer->capacity() + sizeof(ScanMemoryAccount));
		return buffer;
	} catch (const std::bad_alloc &) {
		ThrowAllocationError("unable to allocate OM planned read buffer");
	}
}

void CopySelection(const std::vector<std::uint64_t> &source,
	               std::array<std::uint64_t, OM_MAX_RANK> &destination,
	               std::uint64_t initialized_rank, const char *name) {
	if (source.size() > destination.size()) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  std::string("OM decoder ") + name + " rank exceeds the supported maximum");
	}
	if (initialized_rank != 0 && initialized_rank != source.size()) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  std::string("OM decoder ") + name + " rank cannot change after initialization");
	}
	std::copy(source.begin(), source.end(), destination.begin());
}

std::uint64_t CheckedAddBytes(std::uint64_t left, std::uint64_t right, const char *description) {
	if (left > std::numeric_limits<std::uint64_t>::max() - right) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, std::string(description) + " byte size overflows 64 bits");
	}
	return left + right;
}

void RefreshDecoderMemoryAccount(OmDecoderState &state, const std::shared_ptr<ScanMetrics> &metrics) {
	if (!metrics) return;
	if (!state.memory_account) {
		state.memory_account = std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Decoder);
	}
	std::uint64_t bytes = CheckedAddBytes(sizeof(state), sizeof(ScanMemoryAccount), "OM decoder memory accounting");
	bytes = CheckedAddBytes(bytes, state.index_bytes.Capacity(), "OM decoder memory accounting");
	bytes = CheckedAddBytes(bytes, state.data_bytes.Capacity(), "OM decoder memory accounting");
	bytes = CheckedAddBytes(bytes, state.chunk_scratch.Capacity(), "OM decoder memory accounting");
	state.memory_account->Set(bytes, state.capacity_bounds.decoder_peak_upper_bound_bytes);
}

std::uint64_t CheckedByteSize(std::uint64_t elements, std::uint64_t bytes_per_element, const char *description) {
	if (bytes_per_element != 0 && elements > std::numeric_limits<std::uint64_t>::max() / bytes_per_element) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, std::string(description) + " byte size overflows 64 bits");
	}
	return elements * bytes_per_element;
}

OmDecoderCapacityBounds CalculateCapacityBounds(const OmDecoderState &state, std::uint64_t file_size,
	                                             std::uint64_t declared_lut_size,
	                                             std::uint64_t output_bytes,
	                                             std::uint64_t chunk_scratch_bytes,
	                                             std::uint64_t io_size_max,
	                                             std::uint64_t chunk_product,
	                                             std::uint8_t compressed_bytes_per_element) {
	OmDecoderCapacityBounds bounds;
	// Includes the fixed-rank selection arrays and OmDecoder_t in the state,
	// the account object, and the official per-request control records. The OM
	// metadata buffer is accounted separately by OwnedMetadataBuffer.
	bounds.decoder_control_bytes = CheckedAddBytes(sizeof(state), sizeof(ScanMemoryAccount),
	                                               "OM decoder control capacity");
	bounds.decoder_control_bytes = CheckedAddBytes(bounds.decoder_control_bytes, sizeof(OmDecoder_indexRead_t),
	                                               "OM decoder control capacity");
	bounds.decoder_control_bytes = CheckedAddBytes(bounds.decoder_control_bytes, sizeof(OmDecoder_dataRead_t),
	                                               "OM decoder control capacity");
	bounds.index_buffer_upper_bound_bytes = std::min(file_size, declared_lut_size);
	// The pinned PFor encoder chooses a representation no larger than raw
	// elements plus one header byte per block; FPX adds a predictor byte.
	// FPX uses 128-element blocks, including a final partial block. Using
	// that smaller block size also bounds the supported PFor codecs.
	const auto blocks = chunk_product / 128 + (chunk_product % 128 != 0);
	const auto framing_bytes = CheckedByteSize(blocks, 2, "OM compressed block framing");
	bounds.maximum_compressed_chunk_bytes = CheckedAddBytes(
	    CheckedAddBytes(CheckedByteSize(chunk_product, compressed_bytes_per_element, "OM compressed chunk"),
	                    framing_bytes, "OM compressed chunk framing"),
	    4096, "OM compressed chunk capacity");
	bounds.data_buffer_upper_bound_bytes = std::min(file_size,
	                                               std::max(io_size_max, bounds.maximum_compressed_chunk_bytes));
	bounds.output_bytes = output_bytes;
	bounds.chunk_scratch_bytes = chunk_scratch_bytes;
	bounds.decoder_peak_upper_bound_bytes = bounds.decoder_control_bytes;
	bounds.decoder_peak_upper_bound_bytes = CheckedAddBytes(bounds.decoder_peak_upper_bound_bytes,
	                                                       bounds.index_buffer_upper_bound_bytes,
	                                                       "OM decoder peak capacity");
	bounds.decoder_peak_upper_bound_bytes = CheckedAddBytes(bounds.decoder_peak_upper_bound_bytes,
	                                                       bounds.data_buffer_upper_bound_bytes,
	                                                       "OM decoder peak capacity");
	bounds.decoder_peak_upper_bound_bytes = CheckedAddBytes(bounds.decoder_peak_upper_bound_bytes,
	                                                       bounds.chunk_scratch_bytes,
	                                                       "OM decoder peak capacity");
	bounds.peak_including_output_upper_bound_bytes = CheckedAddBytes(bounds.decoder_peak_upper_bound_bytes,
	                                                                bounds.output_bytes,
	                                                                "OM decoder peak including output");
	return bounds;
}

} // namespace

void EnableOmDecoderMemoryAccounting(OmDecoderState &state, const std::shared_ptr<ScanMetrics> &metrics) {
	RefreshDecoderMemoryAccount(state, metrics);
}

OmV3Reader::OmV3Reader(LocalFile file)
	: OmV3Reader(std::make_unique<LocalFile>(std::move(file))) {
}

OmV3Reader::OmV3Reader(std::unique_ptr<ReadAtFile> file) : file_(std::move(file)) {
	if (!file_) {
		throw ReaderError(ReaderErrorCode::InvalidPath, "OM reader requires an open positional-read file session");
	}
	const auto header_size = static_cast<std::uint64_t>(om_header_write_size());
	if (file_->Size() < header_size) {
		throw ReaderError(ReaderErrorCode::TruncatedFile,
		                  "OM file '" + file_->Path() + "' is shorter than a version header");
	}
	ScanMemoryAccount metadata_header_scratch(file_->Metrics(), ScanMemoryComponent::Definitions);
	metadata_header_scratch.Set(0, CheckedAddBytes(sizeof(std::vector<std::uint8_t>), header_size,
	                                               "OM reader header scratch upper bound"));
	auto header = file_->ReadRange(0, header_size, ScanReadPhase::Metadata);
	const auto header_bytes = CheckedAddBytes(sizeof(header), static_cast<std::uint64_t>(header.capacity()),
	                                         "OM reader metadata scratch accounting");
	metadata_header_scratch.Set(header_bytes);
	const auto header_type = om_header_type(header.data());
	if (header_type == OM_HEADER_INVALID) {
		throw ReaderError(ReaderErrorCode::InvalidHeader,
		                  "OM file '" + file_->Path() + "' has an invalid or unsupported header");
	}
	if (header_type != OM_HEADER_READ_TRAILER) {
		throw ReaderError(ReaderErrorCode::UnsupportedVersion,
		                  "OM file '" + file_->Path() + "' is not OM v3; only version 3 is supported");
	}

	const auto trailer_size = static_cast<std::uint64_t>(om_trailer_size());
	if (file_->Size() < header_size + trailer_size) {
		throw ReaderError(ReaderErrorCode::TruncatedFile,
		                  "OM v3 file '" + file_->Path() + "' is too short to contain its trailer");
	}
	trailer_offset_ = file_->Size() - trailer_size;
	metadata_header_scratch.Set(header_bytes,
	                            CheckedAddBytes(header_bytes,
	                                            CheckedAddBytes(sizeof(std::vector<std::uint8_t>), trailer_size,
	                                                            "OM reader trailer scratch upper bound"),
	                                            "OM reader header and trailer scratch upper bound"));
	auto trailer = file_->ReadRange(trailer_offset_, trailer_size, ScanReadPhase::Metadata);
	const auto trailer_bytes = CheckedAddBytes(sizeof(trailer), static_cast<std::uint64_t>(trailer.capacity()),
	                                          "OM reader metadata scratch accounting");
	metadata_header_scratch.Set(CheckedAddBytes(header_bytes, trailer_bytes,
	                                           "OM reader metadata scratch accounting"));
	if (!om_trailer_read(trailer.data(), &root_offset_, &root_size_)) {
		throw ReaderError(ReaderErrorCode::InvalidHeader,
		                  "OM file '" + file_->Path() + "' has an invalid version 3 trailer");
	}
	root_metadata_ = ReadAndValidateMetadata(root_offset_, root_size_);
}

std::uint64_t OmV3Reader::RootOffset() const noexcept {
	return root_offset_;
}

std::uint64_t OmV3Reader::RootSize() const noexcept {
	return root_size_;
}

void OmV3Reader::PrefetchMetadata(std::uint64_t offset, std::uint64_t size) const {
	constexpr std::uint64_t MAX_PREFETCH_BYTES = 1024 * 1024;
	if (size == 0 || size > MAX_PREFETCH_BYTES) return;
	ValidateBodyRange(offset, size, "metadata");
	metadata_cache_account_ = std::make_shared<ScanMemoryAccount>(file_->Metrics(), ScanMemoryComponent::Definitions);
	metadata_cache_account_->Set(0, sizeof(metadata_cache_) + size);
	metadata_cache_ = file_->ReadRange(offset, size, ScanReadPhase::Metadata);
	metadata_cache_offset_ = offset;
	metadata_cache_account_->Set(sizeof(metadata_cache_) + metadata_cache_.capacity());
}

BorrowedOmVariable OmV3Reader::ReadRootVariable() const {
	return BorrowedOmVariable(root_metadata_);
}

BorrowedOmVariable OmV3Reader::ReadVariable(std::uint64_t offset, std::uint64_t size) const {
	return BorrowedOmVariable(ReadAndValidateMetadata(offset, size));
}

void OmV3Reader::DecodeSelection(OmDecoderState &state, const std::vector<std::uint64_t> &read_offset,
	                              const std::vector<std::uint64_t> &read_count,
	                              const std::vector<std::uint64_t> &cube_offset,
	                              const std::vector<std::uint64_t> &cube_dimensions, void *output,
	                              std::uint64_t output_bytes, std::uint64_t io_size_merge,
	                              std::uint64_t io_size_max, ScanDecodePurpose purpose) const {
	DecodeSelectionImpl(state, std::string(), read_offset, read_count, cube_offset, cube_dimensions, output,
	                output_bytes, io_size_merge, io_size_max, purpose, nullptr);
}

void OmV3Reader::DecodeSelection(OmDecoderState &state, const std::string &variable_path,
	                              const std::vector<std::uint64_t> &read_offset,
	                              const std::vector<std::uint64_t> &read_count,
	                              const std::vector<std::uint64_t> &cube_offset,
	                              const std::vector<std::uint64_t> &cube_dimensions, void *output,
	                              std::uint64_t output_bytes, std::uint64_t io_size_merge,
	                              std::uint64_t io_size_max, ScanDecodePurpose purpose) const {
	DecodeSelectionImpl(state, variable_path, read_offset, read_count, cube_offset, cube_dimensions, output,
	                    output_bytes, io_size_merge, io_size_max, purpose, nullptr);
}

void OmV3Reader::DecodeSelection(OmDecoderState &state, const std::string &variable_path,
	                              const std::vector<std::uint64_t> &read_offset,
	                              const std::vector<std::uint64_t> &read_count,
	                              const std::vector<std::uint64_t> &cube_offset,
	                              const std::vector<std::uint64_t> &cube_dimensions, void *output,
	                              std::uint64_t output_bytes, const RemoteReadBudget &budget,
	                              ScanDecodePurpose purpose) const {
	// Give the official decoder the full remote coalescing gap and cap; this is
	// the only layer that sees adjacent compressed chunks before fetching them.
	const auto gap = budget.enabled ? budget.merge_gap : DEFAULT_LOCAL_MERGE_GAP;
	const auto cap = budget.enabled ? budget.max_request : DEFAULT_LOCAL_MAX_REQUEST;
	DecodeSelectionImpl(state, variable_path, read_offset, read_count, cube_offset, cube_dimensions, output,
	                    output_bytes, gap, cap, purpose, &budget);
}

PlannedSelection OmV3Reader::PlanSelection(OmDecoderState &state, const std::string &variable_path,
                                             const std::vector<std::uint64_t> &read_offset,
                                             const std::vector<std::uint64_t> &read_count,
                                             const std::vector<std::uint64_t> &cube_offset,
                                             const std::vector<std::uint64_t> &cube_dimensions,
                                             std::uint64_t output_bytes, std::uint64_t io_size_merge,
                                             std::uint64_t io_size_max, ScanDecodePurpose purpose,
                                             const RemoteReadBudget *budget, void *synchronous_output) const {
	PlannedSelection plan;
	if (!synchronous_output) {
		plan.purpose = purpose;
		plan.variable_path = variable_path;
		plan.read_offset = read_offset;
		plan.read_count = read_count;
		plan.cube_offset = cube_offset;
		plan.cube_dimensions = cube_dimensions;
		plan.io_size_merge = io_size_merge;
		plan.io_size_max = io_size_max;
	}

	const auto metrics = file_->Metrics();
	RefreshDecoderMemoryAccount(state, metrics);
	const auto *variable = state.variable_owner.Get();
	if (variable == nullptr || !state.variable_owner.MetadataOwner()) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM decoder has no owned variable metadata");
	}

	const auto type = om_variable_get_type(variable);
	if (type != DATA_TYPE_FLOAT_ARRAY && type != DATA_TYPE_INT64_ARRAY) {
		throw ReaderError(ReaderErrorCode::UnsupportedDataType,
		                  "OM decoder only supports Float32 values and Int64 time coordinates");
	}
	const auto compression = om_variable_get_compression(variable);
	if ((type == DATA_TYPE_FLOAT_ARRAY && compression != COMPRESSION_FPX_XOR2D &&
	     compression != COMPRESSION_PFOR_DELTA2D_INT16) ||
	    (type == DATA_TYPE_INT64_ARRAY && compression != COMPRESSION_PFOR_DELTA2D)) {
		throw ReaderError(
		    ReaderErrorCode::UnsupportedCompression,
		    "OM decoder requires FPX_XOR2D/PFOR_DELTA2D_INT16 for values or PFOR_DELTA2D for time coordinates");
	}
	const auto element_size = type == DATA_TYPE_INT64_ARRAY ? sizeof(std::int64_t) : sizeof(float);

	const auto rank = om_variable_get_dimensions_count(variable);
	if (rank == 0 || rank > OM_MAX_RANK || read_offset.size() != rank || read_count.size() != rank ||
	    cube_offset.size() != rank || cube_dimensions.size() != rank) {
		throw ReaderError(ReaderErrorCode::InvalidShape,
		                  "OM decoder selection rank must match the variable rank (1 through 8)");
	}
	const auto *dimensions = om_variable_get_dimensions(variable);
	const auto *chunks = om_variable_get_chunks(variable);
	if (dimensions == nullptr || chunks == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM array metadata is missing dimensions or chunks");
	}
	const auto dimension_count = static_cast<std::size_t>(rank);
	const auto dimension_product = CheckedShapeProduct(dimensions, dimension_count);
	const auto chunk_product = CheckedShapeProduct(chunks, dimension_count);
	CheckedByteSize(chunk_product, element_size, "OM chunk scratch");
	CheckedByteSize(CheckedShapeProduct(cube_dimensions), element_size, "OM decoder output");

	for (std::size_t axis = 0; axis < dimension_count; axis++) {
		if (dimensions[axis] == 0 || chunks[axis] == 0 || chunks[axis] > dimensions[axis]) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "OM dimensions and chunk lengths must be positive and valid");
		}
		if (read_count[axis] == 0 || read_offset[axis] > dimensions[axis] ||
		    read_count[axis] > dimensions[axis] - read_offset[axis]) {
			throw ReaderError(ReaderErrorCode::InvalidSelection,
			                  "OM selection offset/count is empty or outside the variable dimensions");
		}
		if (cube_offset[axis] > cube_dimensions[axis] ||
		    read_count[axis] > cube_dimensions[axis] - cube_offset[axis]) {
			throw ReaderError(ReaderErrorCode::InvalidSelection,
			                  "OM selection does not fit within the output cube dimensions");
		}
	}
	if (dimension_product == 0 || chunk_product == 0) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "OM dimensions and chunks must have a positive product");
	}
	const auto required_output_bytes =
	    CheckedByteSize(CheckedShapeProduct(cube_dimensions), element_size, "OM decoder output");
	if (output_bytes < required_output_bytes) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "OM decoder output buffer is smaller than its cube");
	}
	const auto chunk_scratch_bytes = CheckedByteSize(chunk_product, element_size, "OM chunk scratch");
	const auto *array_metadata = reinterpret_cast<const OmVariableArrayV3_t *>(variable);
	OmError_t compressed_type_error = ERROR_OK;
	const auto compressed_bytes_per_element =
	    om_get_bytes_per_element_compressed(type, compression, &compressed_type_error);
	if (compressed_type_error != ERROR_OK) {
		ThrowOmError(compressed_type_error, ReaderErrorCode::UnsupportedCompression, file_->Path(),
		             "OM decoder compressed element-size lookup");
	}
	state.capacity_bounds = CalculateCapacityBounds(state, file_->Size(), array_metadata->lut_size,
	                                                 required_output_bytes, chunk_scratch_bytes,
	                                                 io_size_max, chunk_product,
	                                                 compressed_bytes_per_element);
	if (io_size_max < sizeof(std::uint64_t) * 64) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  "OM decoder maximum I/O size must be at least 512 bytes");
	}

	CopySelection(read_offset, state.read_offset, state.decoder.dimensions_count, "read offset");
	CopySelection(read_count, state.read_count, state.decoder.dimensions_count, "read count");
	CopySelection(cube_offset, state.cube_offset, state.decoder.dimensions_count, "cube offset");
	CopySelection(cube_dimensions, state.cube_dimensions, state.decoder.dimensions_count, "cube dimensions");
	RefreshDecoderMemoryAccount(state, metrics);

	const auto init_error = om_decoder_init(&state.decoder, variable, rank, state.read_offset.data(),
	                                        state.read_count.data(), state.cube_offset.data(),
	                                        state.cube_dimensions.data(), io_size_merge, io_size_max);
	if (init_error != ERROR_OK) {
		ThrowOmError(init_error, ReaderErrorCode::InvalidSelection, file_->Path(), "OM decoder initialization");
	}
	if (budget && state.decoder.lut_chunk_length > budget->max_request) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  "[configuration] duckomo_remote_max_request is smaller than an official LUT block");
	}
	if (state.decoder.lut_chunk_length > io_size_max) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  "OM decoder maximum I/O size is smaller than an official LUT block");
	}
	if (state.decoder.lut_chunk_length == 0) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM v3 array metadata does not contain an official lookup table");
	}

	const auto scratch_size = om_decoder_read_buffer_size(&state.decoder);
	if (scratch_size == 0 || scratch_size != chunk_scratch_bytes ||
	    scratch_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
		throw ReaderError(ReaderErrorCode::Allocation, "OM decoder reported an invalid chunk scratch size");
	}
	try {
		state.chunk_scratch.EnsureSize(static_cast<std::size_t>(scratch_size));
	} catch (const std::bad_alloc &) {
		ThrowAllocationError("unable to allocate OM decoder chunk scratch");
	}
	RefreshDecoderMemoryAccount(state, metrics);
	const auto index_phase = purpose == ScanDecodePurpose::Coordinate ? ScanReadPhase::CoordinateIndex
	                                                                 : ScanReadPhase::Index;
	const auto &metric_variable_path = purpose == ScanDecodePurpose::Coordinate ? std::string() : variable_path;

	OmDecoder_indexRead_t index_read{};
	om_decoder_init_index_read(&state.decoder, &index_read);
	while (om_decoder_next_index_read(&state.decoder, &index_read)) {
		ValidateBodyRange(index_read.offset, index_read.count, "index");
		if (index_read.count > state.capacity_bounds.index_buffer_upper_bound_bytes) {
			throw ReaderError(ReaderErrorCode::IndexRead,
			                  "official OM index request exceeds its declared lookup-table capacity bound");
		}
		if (index_read.count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
			ThrowAllocationError("OM index buffer exceeds addressable memory");
		}
		const bool reuse_index = state.index_range_size != 0 && index_read.offset >= state.index_range_offset &&
		    index_read.offset - state.index_range_offset <= state.index_range_size &&
		    index_read.count <= state.index_range_size - (index_read.offset - state.index_range_offset);
		if (!reuse_index) {
			state.index_range_size = 0;
			try {
				state.index_bytes.EnsureSize(static_cast<std::size_t>(index_read.count));
				RefreshDecoderMemoryAccount(state, metrics);
				file_->ReadRange(index_read.offset, index_read.count, state.index_bytes.Data(), index_phase,
				                 metric_variable_path);
				if (budget && metrics) metrics->RecordRemoteUsefulBytes(index_read.count);
			} catch (const std::bad_alloc &) {
				ThrowAllocationError("unable to allocate OM index buffer");
			}
			state.index_range_offset = index_read.offset;
			state.index_range_size = index_read.count;
		} else if (metrics) {
			metrics->RecordLogicalRead(index_phase, index_read.count, metric_variable_path);
		}
		const auto *index_data = state.index_bytes.Data() + (index_read.offset - state.index_range_offset);

		PlannedIndexBlock block;
		block.index_read = index_read;
		if (!synchronous_output) {
			block.index_data = AllocateReadBuffer(index_read.count, metrics);
			std::memcpy(block.index_data->data(), index_data, static_cast<std::size_t>(index_read.count));
		}

		OmDecoder_dataRead_t data_read{};
		om_decoder_init_data_read(&data_read, &index_read);
		OmError_t read_error = ERROR_OK;
		std::vector<OmDecoder_dataRead_t> pending_ranges;

		auto add_planned_group = [&](const std::vector<OmDecoder_dataRead_t> &ranges) {
			if (ranges.empty()) return;
			const auto merged_begin = ranges.front().offset;
			const auto merged_end = ranges.back().offset + ranges.back().count;
			if (merged_end < merged_begin) throw ReaderError(ReaderErrorCode::DataRead, "OM merged range overflows");
			const auto merged_size = merged_end - merged_begin;
			for (const auto &range : ranges) {
				ValidateBodyRange(range.offset, range.count, "data");
			}
			const bool can_merge = budget && budget->enabled && ranges.size() > 1;
			const bool reuse_range = !can_merge && ranges.size() == 1 && state.data_range_size != 0 &&
			    state.data_cache != nullptr &&
			    ranges.front().offset >= state.data_range_offset &&
			    ranges.front().offset - state.data_range_offset <= state.data_range_size &&
			    ranges.front().count <= state.data_range_size - (ranges.front().offset - state.data_range_offset);
			const auto read_offset = can_merge ? merged_begin : (reuse_range ? state.data_range_offset : ranges.front().offset);
			const auto requested_read_size = can_merge ? merged_size : ranges.front().count;
			const auto available_bytes = file_->Size() > read_offset + requested_read_size
			    ? file_->Size() - (read_offset + requested_read_size)
			    : 0;
			const auto read_ahead = budget && budget->enabled && budget->allow_read_ahead && !reuse_range
			    ? std::min({budget->merge_gap,
			                budget->max_request > requested_read_size ? budget->max_request - requested_read_size : 0,
			                state.capacity_bounds.data_buffer_upper_bound_bytes > requested_read_size
			                    ? state.capacity_bounds.data_buffer_upper_bound_bytes - requested_read_size
			                    : 0,
			                available_bytes})
			    : 0;
			const auto read_size = reuse_range ? state.data_range_size : requested_read_size + read_ahead;

			PlannedDataRange planned;
			planned.read_offset = read_offset;
			planned.read_size = read_size;
			planned.reuse_range = reuse_range;
			planned.ranges = ranges;
			for (const auto &range : ranges) {
				planned.useful_bytes = CheckedAddBytes(planned.useful_bytes,
				    UsefulDataBytes(state.decoder, index_data, index_read.count, range), "OM useful bytes");
			}
			if (reuse_range) {
				planned.buffer = state.data_cache;
			} else {
				state.data_range_size = 0;
				state.data_cache.reset();
				planned.buffer = AllocateReadBuffer(read_size, metrics);
			}
			block.data_ranges.push_back(std::move(planned));
		};

		while (om_decoder_next_data_read(&state.decoder, &data_read, index_data,
		                                 index_read.count, &read_error)) {
			ValidateBodyRange(data_read.offset, data_read.count, "data");
			if (data_read.count > state.capacity_bounds.data_buffer_upper_bound_bytes) {
				throw ReaderError(ReaderErrorCode::DataRead,
				                  "official OM data request exceeds the decoder data-buffer capacity bound");
			}
			if (data_read.count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
				ThrowAllocationError("OM data buffer exceeds addressable memory");
			}
			if (synchronous_output) {
				const bool reuse = state.data_range_size && data_read.offset >= state.data_range_offset &&
				    data_read.offset - state.data_range_offset <= state.data_range_size &&
				    data_read.count <= state.data_range_size - (data_read.offset - state.data_range_offset);
				const auto phase = purpose == ScanDecodePurpose::Coordinate ? ScanReadPhase::CoordinateData : ScanReadPhase::Data;
				if (!reuse) {
					state.data_range_size = 0;
					try {
						state.data_bytes.EnsureSize(static_cast<std::size_t>(data_read.count));
						RefreshDecoderMemoryAccount(state, metrics);
						file_->ReadRange(data_read.offset, data_read.count, state.data_bytes.Data(), phase, metric_variable_path);
					} catch (const std::bad_alloc &) { ThrowAllocationError("unable to allocate OM data buffer"); }
					state.data_range_offset = data_read.offset;
					state.data_range_size = data_read.count;
				} else if (metrics) metrics->RecordLogicalRead(phase, data_read.count, metric_variable_path);
				OmError_t error = ERROR_OK;
				if (!om_decoder_decode_chunks(&state.decoder, data_read.chunkIndex,
				    state.data_bytes.Data() + data_read.offset - state.data_range_offset, data_read.count,
				    synchronous_output, state.chunk_scratch.Data(), &error)) {
					if (metrics) {
						if (purpose == ScanDecodePurpose::Coordinate) metrics->MarkCoordinateDecodeCountIncomplete();
						else metrics->MarkDecodeCountIncomplete(variable_path);
					}
					ThrowOmError(error, ReaderErrorCode::Decode, file_->Path(), "OM chunk decode");
				}
				if (metrics) {
					const auto chunks = data_read.chunkIndex.upperBound - data_read.chunkIndex.lowerBound;
					if (purpose == ScanDecodePurpose::Coordinate) metrics->RecordSuccessfulCoordinateDecode(chunks);
					else metrics->RecordSuccessfulDecode(variable_path, chunks);
				}
			} else pending_ranges.push_back(data_read);
		}
		if (read_error != ERROR_OK) {
			ThrowOmError(read_error, ReaderErrorCode::DataRead, file_->Path(), "OM data request planning");
		}

		if (synchronous_output) continue;
		std::vector<OmDecoder_dataRead_t> merge_group;
		for (const auto &range : pending_ranges) {
			if (!merge_group.empty()) {
				const auto previous_end = merge_group.back().offset + merge_group.back().count;
				const auto merged_end = range.offset + range.count;
				const auto merged_size = merged_end >= merge_group.front().offset ? merged_end - merge_group.front().offset : UINT64_MAX;
				const auto decoder_gap = range.offset >= previous_end ? range.offset - previous_end : UINT64_MAX;
				const bool merge_allowed = budget && budget->enabled && range.offset >= previous_end &&
				    decoder_gap <= budget->merge_gap && merged_size <= budget->max_request &&
				    merged_size <= state.capacity_bounds.data_buffer_upper_bound_bytes;
				if (!merge_allowed) {
					add_planned_group(merge_group);
					merge_group.clear();
				}
			}
			merge_group.push_back(range);
		}
		add_planned_group(merge_group);
		plan.blocks.push_back(std::move(block));
	}
	if (synchronous_output) return plan;
	std::uint64_t control_bytes = sizeof(plan);
	for (const auto *selection : {&plan.read_offset, &plan.read_count, &plan.cube_offset, &plan.cube_dimensions})
		control_bytes = CheckedAddBytes(control_bytes, selection->capacity() * sizeof(std::uint64_t), "plan control");
	control_bytes = CheckedAddBytes(control_bytes, plan.variable_path.capacity() + 1, "plan control");
	control_bytes = CheckedAddBytes(control_bytes, plan.blocks.capacity() * sizeof(PlannedIndexBlock), "plan control");
	for (const auto &block : plan.blocks) {
		control_bytes = CheckedAddBytes(control_bytes, block.data_ranges.capacity() * sizeof(PlannedDataRange), "plan control");
		for (const auto &range : block.data_ranges)
			control_bytes = CheckedAddBytes(control_bytes, range.ranges.capacity() * sizeof(OmDecoder_dataRead_t), "plan control");
	}
	plan.control_account = std::make_shared<ScanMemoryAccount>(metrics, ScanMemoryComponent::Decoder);
	plan.control_account->Set(control_bytes);
	return plan;
}

void OmV3Reader::FetchSelection(PlannedSelection &plan, const RemoteReadBudget *budget) const {
	const auto metrics = file_->Metrics();
	const auto data_phase = plan.purpose == ScanDecodePurpose::Coordinate ? ScanReadPhase::CoordinateData
	                                                                      : ScanReadPhase::Data;
	const auto &metric_variable_path = plan.purpose == ScanDecodePurpose::Coordinate ? std::string() : plan.variable_path;

	for (auto &block : plan.blocks) {
		for (auto &range : block.data_ranges) {
			if (range.reuse_range) {
				if (metrics) metrics->RecordLogicalRead(data_phase, range.ranges.front().count, metric_variable_path);
			} else {
				const auto cap = budget ? budget->max_request : range.read_size;
				for (std::uint64_t done = 0; done < range.read_size;) {
					const auto bytes = std::min(cap, range.read_size - done);
					file_->ReadRange(range.read_offset + done, bytes, range.buffer->data() + done,
					                 data_phase, metric_variable_path);
					done += bytes;
				}
				if (budget && metrics) metrics->RecordRemoteUsefulBytes(range.useful_bytes);
			}
		}
	}
}

void OmV3Reader::DecodeSelectionPlan(OmDecoderState &state, const PlannedSelection &plan, void *output,
                                    const RemoteReadBudget *) const {
	const auto metrics = file_->Metrics();
	const auto *variable = state.variable_owner.Get();
	if (variable && !plan.read_offset.empty()) {
		om_decoder_init(&state.decoder, variable, om_variable_get_dimensions_count(variable),
		                plan.read_offset.data(), plan.read_count.data(),
		                plan.cube_offset.data(), plan.cube_dimensions.data(),
		                plan.io_size_merge, plan.io_size_max);
	}

	for (const auto &block : plan.blocks) {
		for (const auto &range : block.data_ranges) {
			if (!range.reuse_range && range.ranges.size() == 1) {
				state.data_cache = range.buffer;
				state.data_range_offset = range.read_offset;
				state.data_range_size = range.read_size;
			}
			for (const auto &sub_range : range.ranges) {
				const auto *data = range.buffer->data() + (sub_range.offset - range.read_offset);
				OmError_t decode_error = ERROR_OK;
				if (!om_decoder_decode_chunks(&state.decoder, sub_range.chunkIndex, data, sub_range.count, output,
				                              state.chunk_scratch.Data(), &decode_error)) {
					if (metrics) {
						if (plan.purpose == ScanDecodePurpose::Coordinate) metrics->MarkCoordinateDecodeCountIncomplete();
						else metrics->MarkDecodeCountIncomplete(plan.variable_path);
					}
					ThrowOmError(decode_error, ReaderErrorCode::Decode, file_->Path(), "OM chunk decode");
				}
				if (metrics) {
					const auto decoded_ranges = sub_range.chunkIndex.upperBound - sub_range.chunkIndex.lowerBound;
					if (plan.purpose == ScanDecodePurpose::Coordinate) metrics->RecordSuccessfulCoordinateDecode(decoded_ranges);
					else metrics->RecordSuccessfulDecode(plan.variable_path, decoded_ranges);
				}
			}
		}
	}
}

void OmV3Reader::DecodeSelectionImpl(OmDecoderState &state, const std::string &variable_path,
                                    const std::vector<std::uint64_t> &read_offset,
                                    const std::vector<std::uint64_t> &read_count,
                                    const std::vector<std::uint64_t> &cube_offset,
                                    const std::vector<std::uint64_t> &cube_dimensions, void *output,
                                    std::uint64_t output_bytes, std::uint64_t io_size_merge,
                                    std::uint64_t io_size_max, ScanDecodePurpose purpose,
                                    const RemoteReadBudget *budget) const {
	if (output == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "OM decoder output buffer must not be NULL");
	}
	const bool local = file_->Path().find("://") == std::string::npos || file_->Path().rfind("memory://", 0) == 0;
	auto plan = PlanSelection(state, variable_path, read_offset, read_count, cube_offset,
	                          cube_dimensions, output_bytes, io_size_merge, io_size_max,
	                          purpose, budget, local ? output : nullptr);
	if (local) return;
	try {
		FetchSelection(plan, budget);
	} catch (...) {
		state.data_range_size = 0;
		state.data_cache.reset();
		throw;
	}
	DecodeSelectionPlan(state, plan, output, budget);
}

const ReadAtFile &OmV3Reader::File() const noexcept {
	return *file_;
}

void OmV3Reader::ValidateBodyRange(std::uint64_t offset, std::uint64_t size, const char *phase) const {
	if (size == 0 || offset < om_header_write_size() || offset > trailer_offset_ || size > trailer_offset_ - offset) {
		std::ostringstream message;
		message << "official OM " << phase << " range [" << offset << ", ";
		if (size > std::numeric_limits<std::uint64_t>::max() - offset) {
			message << "overflow";
		} else {
			message << offset + size;
		}
		message << ") is empty or outside the OM v3 body in '" << file_->Path() << "'";
		const auto code = std::string(phase) == "metadata" ? ReaderErrorCode::InvalidMetadata
		                                                 : (std::string(phase) == "index" ? ReaderErrorCode::IndexRead
		                                                                                : ReaderErrorCode::DataRead);
		throw ReaderError(code, message.str());
	}
}

std::shared_ptr<const OwnedMetadataBuffer> OmV3Reader::ReadAndValidateMetadata(std::uint64_t offset,
	                                                                             std::uint64_t size) const {
	ValidateBodyRange(offset, size, "metadata");
	if (size < sizeof(OmVariableV3_t)) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM variable metadata is shorter than the official v3 variable header");
	}
	auto memory_account = std::make_shared<ScanMemoryAccount>(file_->Metrics(), ScanMemoryComponent::Definitions);
	const auto metadata_upper_bound = CheckedAddBytes(
	    CheckedAddBytes(sizeof(std::vector<std::uint8_t>), size, "OM metadata buffer upper bound"),
	    sizeof(OwnedMetadataBuffer), "OM metadata owner upper bound");
	memory_account->Set(0, metadata_upper_bound);
	std::vector<std::uint8_t> bytes;
	if (offset >= metadata_cache_offset_ && offset - metadata_cache_offset_ <= metadata_cache_.size() &&
	    size <= metadata_cache_.size() - (offset - metadata_cache_offset_)) {
		const auto begin = metadata_cache_.begin() + static_cast<std::size_t>(offset - metadata_cache_offset_);
		bytes.assign(begin, begin + static_cast<std::size_t>(size));
	} else {
		bytes = file_->ReadRange(offset, size, ScanReadPhase::Metadata);
	}
	const auto temporary_bytes = CheckedAddBytes(sizeof(bytes), static_cast<std::uint64_t>(bytes.capacity()),
	                                            "OM metadata buffer accounting");
	const auto transfer_upper_bound = CheckedAddBytes(temporary_bytes, sizeof(OwnedMetadataBuffer),
	                                                  "OM metadata buffer accounting");
	memory_account->Set(temporary_bytes, transfer_upper_bound);
	const auto embedded_header_type = om_header_type(bytes.data());
	if (embedded_header_type == OM_HEADER_LEGACY) {
		throw ReaderError(ReaderErrorCode::UnsupportedVersion,
		                  "legacy OM variable metadata is not valid inside an OM v3 file");
	}
	if (embedded_header_type == OM_HEADER_READ_TRAILER) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "an OM file header cannot be used as v3 variable metadata");
	}
	const auto error = om_variable_validate(bytes.data(), size);
	if (error != ERROR_OK) {
		ThrowOmError(error, ReaderErrorCode::InvalidMetadata, file_->Path(), "OM variable metadata validation");
	}
	const auto *variable = om_variable_init(bytes.data());
	const auto data_type = om_variable_get_type(variable);
	if (data_type >= DATA_TYPE_INT8_ARRAY && data_type <= DATA_TYPE_DOUBLE_ARRAY &&
	    size < sizeof(OmVariableArrayV3_t)) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM array metadata is shorter than the official v3 array header");
	}
	try {
		return std::make_shared<const OwnedMetadataBuffer>(std::move(bytes), std::move(memory_account));
	} catch (const std::bad_alloc &) {
		ThrowAllocationError("unable to retain OM variable metadata");
	}
}

} // namespace duckomo
} // namespace duckdb
