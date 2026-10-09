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
	                                             std::uint64_t chunk_scratch_bytes) {
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
	bounds.data_buffer_upper_bound_bytes = file_size;
	bounds.output_bytes = output_bytes;
	bounds.chunk_scratch_bytes = chunk_scratch_bytes;
	bounds.data_bound_uses_object_size = true;
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
	DecodeSelection(state, std::string(), read_offset, read_count, cube_offset, cube_dimensions, output,
	                output_bytes, io_size_merge, io_size_max, purpose);
}

void OmV3Reader::DecodeSelection(OmDecoderState &state, const std::string &variable_path,
	                              const std::vector<std::uint64_t> &read_offset,
	                              const std::vector<std::uint64_t> &read_count,
	                              const std::vector<std::uint64_t> &cube_offset,
	                              const std::vector<std::uint64_t> &cube_dimensions, void *output,
	                              std::uint64_t output_bytes, std::uint64_t io_size_merge,
	                              std::uint64_t io_size_max, ScanDecodePurpose purpose) const {
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
	if (output == nullptr || output_bytes < required_output_bytes) {
		throw ReaderError(ReaderErrorCode::InvalidSelection, "OM decoder output buffer is smaller than its cube");
	}
	const auto chunk_scratch_bytes = CheckedByteSize(chunk_product, element_size, "OM chunk scratch");
	const auto *array_metadata = reinterpret_cast<const OmVariableArrayV3_t *>(variable);
	state.capacity_bounds = CalculateCapacityBounds(state, file_->Size(), array_metadata->lut_size,
	                                                 required_output_bytes, chunk_scratch_bytes);
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
	const auto data_phase = purpose == ScanDecodePurpose::Coordinate ? ScanReadPhase::CoordinateData
	                                                                : ScanReadPhase::Data;
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
		try {
			state.index_bytes.EnsureSize(static_cast<std::size_t>(index_read.count));
		} catch (const std::bad_alloc &) {
			ThrowAllocationError("unable to allocate OM index buffer");
		}
		RefreshDecoderMemoryAccount(state, metrics);
		if (state.index_bytes.Size() != index_read.count || state.index_bytes.Size() == 0) {
			throw ReaderError(ReaderErrorCode::IndexRead,
			                  "official OM decoder requested an empty or incomplete index read");
		}
		try {
			file_->ReadRange(index_read.offset, index_read.count, state.index_bytes.Data(), index_phase,
			                 metric_variable_path);
		} catch (const std::bad_alloc &) {
			ThrowAllocationError("unable to allocate OM index buffer");
		}

		OmDecoder_dataRead_t data_read{};
		om_decoder_init_data_read(&data_read, &index_read);
		OmError_t read_error = ERROR_OK;
		while (om_decoder_next_data_read(&state.decoder, &data_read, state.index_bytes.Data(),
		                                 state.index_bytes.Size(), &read_error)) {
			ValidateBodyRange(data_read.offset, data_read.count, "data");
			if (data_read.count > state.capacity_bounds.data_buffer_upper_bound_bytes) {
				throw ReaderError(ReaderErrorCode::DataRead,
				                  "official OM data request exceeds the object-size capacity bound");
			}
			if (data_read.count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
				ThrowAllocationError("OM data buffer exceeds addressable memory");
			}
			try {
				state.data_bytes.EnsureSize(static_cast<std::size_t>(data_read.count));
			} catch (const std::bad_alloc &) {
				ThrowAllocationError("unable to allocate OM data buffer");
			}
			RefreshDecoderMemoryAccount(state, metrics);
			if (state.data_bytes.Size() != data_read.count || state.data_bytes.Size() == 0) {
				throw ReaderError(ReaderErrorCode::DataRead,
				                  "official OM decoder requested an empty or incomplete data read");
			}
			try {
				file_->ReadRange(data_read.offset, data_read.count, state.data_bytes.Data(), data_phase,
				                 metric_variable_path);
			} catch (const std::bad_alloc &) {
				ThrowAllocationError("unable to allocate OM data buffer");
			}

			OmError_t decode_error = ERROR_OK;
			if (!om_decoder_decode_chunks(&state.decoder, data_read.chunkIndex, state.data_bytes.Data(),
			                              data_read.count, output, state.chunk_scratch.Data(), &decode_error)) {
				if (metrics) {
					if (purpose == ScanDecodePurpose::Coordinate) metrics->MarkCoordinateDecodeCountIncomplete();
					else metrics->MarkDecodeCountIncomplete(variable_path);
				}
				ThrowOmError(decode_error, ReaderErrorCode::Decode, file_->Path(), "OM chunk decode");
			}
			if (metrics) {
				const auto decoded_ranges = data_read.chunkIndex.upperBound - data_read.chunkIndex.lowerBound;
				if (purpose == ScanDecodePurpose::Coordinate) metrics->RecordSuccessfulCoordinateDecode(decoded_ranges);
				else metrics->RecordSuccessfulDecode(variable_path, decoded_ranges);
			}
		}
		if (read_error != ERROR_OK) {
			ThrowOmError(read_error, ReaderErrorCode::DataRead, file_->Path(), "OM data request planning");
		}
	}
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
	auto bytes = file_->ReadRange(offset, size, ScanReadPhase::Metadata);
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
