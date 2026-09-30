#include "duckomo/reader.hpp"

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

void CopySelection(const std::vector<std::uint64_t> &source, std::vector<std::uint64_t> &destination,
	               std::uint64_t initialized_rank, const char *name) {
	if (initialized_rank != 0 && destination.size() != source.size()) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  std::string("OM decoder ") + name + " rank cannot change after initialization");
	}
	if (destination.size() != source.size()) {
		try {
			destination.resize(source.size());
		} catch (const std::bad_alloc &) {
			ThrowAllocationError("unable to allocate OM decoder selection parameters");
		} catch (const std::length_error &) {
			ThrowAllocationError("OM decoder selection parameters exceed addressable memory");
		}
	}
	std::copy(source.begin(), source.end(), destination.begin());
}

std::uint64_t CheckedByteSize(std::uint64_t elements, std::uint64_t bytes_per_element, const char *description) {
	if (bytes_per_element != 0 && elements > std::numeric_limits<std::uint64_t>::max() / bytes_per_element) {
		throw ReaderError(ReaderErrorCode::ShapeOverflow, std::string(description) + " byte size overflows 64 bits");
	}
	return elements * bytes_per_element;
}

} // namespace

OmV3Reader::OmV3Reader(LocalFile file) : file_(std::move(file)) {
	const auto header_size = static_cast<std::uint64_t>(om_header_write_size());
	if (file_.Size() < header_size) {
		throw ReaderError(ReaderErrorCode::TruncatedFile,
		                  "local OM file '" + file_.Path() + "' is shorter than a version header");
	}
	auto header = file_.ReadRange(0, header_size, ScanReadPhase::Metadata);
	const auto header_type = om_header_type(header.data());
	if (header_type == OM_HEADER_INVALID) {
		throw ReaderError(ReaderErrorCode::InvalidHeader,
		                  "local OM file '" + file_.Path() + "' has an invalid or unsupported header");
	}
	if (header_type != OM_HEADER_READ_TRAILER) {
		throw ReaderError(ReaderErrorCode::UnsupportedVersion,
		                  "local OM file '" + file_.Path() + "' is not OM v3; only version 3 is supported");
	}

	const auto trailer_size = static_cast<std::uint64_t>(om_trailer_size());
	if (file_.Size() < header_size + trailer_size) {
		throw ReaderError(ReaderErrorCode::TruncatedFile,
		                  "local OM v3 file '" + file_.Path() + "' is too short to contain its trailer");
	}
	trailer_offset_ = file_.Size() - trailer_size;
	auto trailer = file_.ReadRange(trailer_offset_, trailer_size, ScanReadPhase::Metadata);
	if (!om_trailer_read(trailer.data(), &root_offset_, &root_size_)) {
		throw ReaderError(ReaderErrorCode::InvalidHeader,
		                  "local OM file '" + file_.Path() + "' has an invalid version 3 trailer");
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
	                              std::uint64_t io_size_max) const {
	DecodeSelection(state, std::string(), read_offset, read_count, cube_offset, cube_dimensions, output,
	                output_bytes, io_size_merge, io_size_max);
}

void OmV3Reader::DecodeSelection(OmDecoderState &state, const std::string &variable_path,
	                              const std::vector<std::uint64_t> &read_offset,
	                              const std::vector<std::uint64_t> &read_count,
	                              const std::vector<std::uint64_t> &cube_offset,
	                              const std::vector<std::uint64_t> &cube_dimensions, void *output,
	                              std::uint64_t output_bytes, std::uint64_t io_size_merge,
	                              std::uint64_t io_size_max) const {
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
	if (io_size_max < sizeof(std::uint64_t) * 64) {
		throw ReaderError(ReaderErrorCode::InvalidSelection,
		                  "OM decoder maximum I/O size must be at least 512 bytes");
	}

	CopySelection(read_offset, state.read_offset, state.decoder.dimensions_count, "read offset");
	CopySelection(read_count, state.read_count, state.decoder.dimensions_count, "read count");
	CopySelection(cube_offset, state.cube_offset, state.decoder.dimensions_count, "cube offset");
	CopySelection(cube_dimensions, state.cube_dimensions, state.decoder.dimensions_count, "cube dimensions");

	const auto init_error = om_decoder_init(&state.decoder, variable, rank, state.read_offset.data(),
	                                        state.read_count.data(), state.cube_offset.data(),
	                                        state.cube_dimensions.data(), io_size_merge, io_size_max);
	if (init_error != ERROR_OK) {
		ThrowOmError(init_error, ReaderErrorCode::InvalidSelection, file_.Path(), "OM decoder initialization");
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
	const auto checked_scratch_size = CheckedByteSize(chunk_product, element_size, "OM chunk scratch");
	if (scratch_size == 0 || scratch_size != checked_scratch_size ||
	    scratch_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
		throw ReaderError(ReaderErrorCode::Allocation, "OM decoder reported an invalid chunk scratch size");
	}
	try {
		state.chunk_scratch.resize(static_cast<std::size_t>(scratch_size));
	} catch (const std::bad_alloc &) {
		ThrowAllocationError("unable to allocate OM decoder chunk scratch");
	} catch (const std::length_error &) {
		ThrowAllocationError("OM decoder chunk scratch exceeds addressable memory");
	}

	OmDecoder_indexRead_t index_read{};
	om_decoder_init_index_read(&state.decoder, &index_read);
	while (om_decoder_next_index_read(&state.decoder, &index_read)) {
		ValidateBodyRange(index_read.offset, index_read.count, "index");
		try {
			state.index_bytes = file_.ReadRange(index_read.offset, index_read.count, ScanReadPhase::Index,
			                                   variable_path);
		} catch (const std::bad_alloc &) {
			ThrowAllocationError("unable to allocate OM index buffer");
		}
		if (state.index_bytes.size() != index_read.count || state.index_bytes.empty()) {
			throw ReaderError(ReaderErrorCode::IndexRead,
			                  "official OM decoder requested an empty or incomplete index read");
		}

		OmDecoder_dataRead_t data_read{};
		om_decoder_init_data_read(&data_read, &index_read);
		OmError_t read_error = ERROR_OK;
		while (om_decoder_next_data_read(&state.decoder, &data_read, state.index_bytes.data(),
		                                 state.index_bytes.size(), &read_error)) {
			ValidateBodyRange(data_read.offset, data_read.count, "data");
			try {
				state.data_bytes = file_.ReadRange(data_read.offset, data_read.count, ScanReadPhase::Data,
				                                 variable_path);
			} catch (const std::bad_alloc &) {
				ThrowAllocationError("unable to allocate OM data buffer");
			}
			if (state.data_bytes.size() != data_read.count || state.data_bytes.empty()) {
				throw ReaderError(ReaderErrorCode::DataRead,
				                  "official OM decoder requested an empty or incomplete data read");
			}

			OmError_t decode_error = ERROR_OK;
			if (!om_decoder_decode_chunks(&state.decoder, data_read.chunkIndex, state.data_bytes.data(),
			                              data_read.count, output, state.chunk_scratch.data(), &decode_error)) {
				if (file_.Metrics()) {
					file_.Metrics()->MarkDecodeCountIncomplete(variable_path);
				}
				ThrowOmError(decode_error, ReaderErrorCode::Decode, file_.Path(), "OM chunk decode");
			}
			if (file_.Metrics()) {
				file_.Metrics()->RecordSuccessfulDecode(variable_path,
				                                       data_read.chunkIndex.upperBound - data_read.chunkIndex.lowerBound);
			}
		}
		if (read_error != ERROR_OK) {
			ThrowOmError(read_error, ReaderErrorCode::DataRead, file_.Path(), "OM data request planning");
		}
	}
}

const LocalFile &OmV3Reader::File() const noexcept {
	return file_;
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
		message << ") is empty or outside the OM v3 body in '" << file_.Path() << "'";
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
	auto bytes = file_.ReadRange(offset, size, ScanReadPhase::Metadata);
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
		ThrowOmError(error, ReaderErrorCode::InvalidMetadata, file_.Path(), "OM variable metadata validation");
	}
	const auto *variable = om_variable_init(bytes.data());
	const auto data_type = om_variable_get_type(variable);
	if (data_type >= DATA_TYPE_INT8_ARRAY && data_type <= DATA_TYPE_DOUBLE_ARRAY &&
	    size < sizeof(OmVariableArrayV3_t)) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM array metadata is shorter than the official v3 array header");
	}
	try {
		return std::make_shared<const OwnedMetadataBuffer>(std::move(bytes));
	} catch (const std::bad_alloc &) {
		ThrowAllocationError("unable to retain OM variable metadata");
	}
}

} // namespace duckomo
} // namespace duckdb
