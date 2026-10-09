#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "om_decoder.h"
#include "om_encoder.h"
#include "om_file.h"
#include "om_variable.h"
}

namespace {

constexpr char OM_UPSTREAM_COMMIT[] = "d8855e418e2231ae8439f0c7e840fa3f93b371e3";
constexpr char GENERATION_COMMAND[] = "./build/release/test/tools/duckomo_fixture_tool --output test/data";
constexpr std::uint64_t IO_SIZE_MERGE = 512;
constexpr std::uint64_t IO_SIZE_MAX = 64 * 1024;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::uint64_t CheckedProduct(const std::vector<std::uint64_t> &dimensions) {
	Require(!dimensions.empty(), "OM root arrays must have at least one dimension");
	std::uint64_t result = 1;
	for (const auto length : dimensions) {
		Require(length != 0, "OM root array dimensions must be positive");
		Require(result <= std::numeric_limits<std::uint64_t>::max() / length,
		        "OM root array row count overflows uint64_t");
		result *= length;
	}
	return result;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path &path) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		throw std::runtime_error("cannot open input file: " + path.string());
	}
	stream.seekg(0, std::ios::end);
	const auto end = stream.tellg();
	Require(end >= 0, "cannot determine input file size: " + path.string());
	const auto size = static_cast<std::uint64_t>(end);
	Require(size <= std::numeric_limits<std::size_t>::max(), "input file is too large for this process");
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
	stream.seekg(0, std::ios::beg);
	if (!bytes.empty()) {
		stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Require(stream.good() || stream.eof(), "failed while reading input file: " + path.string());
		Require(static_cast<std::size_t>(stream.gcount()) == bytes.size(), "short read from input file: " + path.string());
	}
	return bytes;
}

void WriteFile(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
	if (path.has_parent_path()) {
		std::filesystem::create_directories(path.parent_path());
	}
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream) {
		throw std::runtime_error("cannot create output file: " + path.string());
	}
	if (!bytes.empty()) {
		stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	Require(stream.good(), "failed while writing output file: " + path.string());
}

void Append(std::vector<std::uint8_t> &destination, const void *data, std::size_t size) {
	const auto *first = static_cast<const std::uint8_t *>(data);
	destination.insert(destination.end(), first, first + size);
}

struct Fixture final {
	std::string id;
	std::string file_name;
	std::string reference_name;
	std::vector<std::uint64_t> shape;
	std::vector<std::uint64_t> chunks;
	std::vector<float> values;
	std::vector<std::string> axes;
	OmCompression_t compression = COMPRESSION_FPX_XOR2D;
	float scale_factor = 1.0F;
	float add_offset = 0.0F;
};

struct TreeArray final {
	Fixture fixture;
	std::vector<std::string> segments;
	std::vector<std::string> axes;
	std::string reference_name;
	std::vector<std::pair<std::string, std::string>> string_attributes;
	std::uint64_t lut_size = 0;
	std::uint64_t lut_offset = 0;
};

struct TreeNode final {
	std::string name;
	TreeArray *array = nullptr;
	std::vector<TreeNode> children;
	std::uint64_t metadata_offset = 0;
	std::uint64_t metadata_size = 0;
};

struct NodeRef final {
	std::uint64_t offset = 0;
	std::uint64_t size = 0;
};

struct DecodedVariable final {
	std::string path;
	std::vector<std::uint64_t> shape;
	std::vector<std::uint64_t> chunks;
	std::vector<float> values;
};

std::uint16_t AsNameSize(const std::string &name);

std::vector<std::uint8_t> EncodeRootArray(const Fixture &fixture) {
	Require(fixture.shape.size() == fixture.chunks.size(), "fixture shape and chunk rank differ: " + fixture.id);
	Require(CheckedProduct(fixture.shape) == fixture.values.size(), "fixture data length does not match shape: " + fixture.id);
	for (std::size_t axis = 0; axis < fixture.shape.size(); axis++) {
		Require(fixture.chunks[axis] > 0 && fixture.chunks[axis] <= fixture.shape[axis],
		        "invalid chunk length in fixture " + fixture.id);
	}

	OmEncoder_t encoder{};
	const auto init_error = om_encoder_init(&encoder, fixture.scale_factor, fixture.add_offset, fixture.compression,
	                                       DATA_TYPE_FLOAT_ARRAY,
	                                       fixture.shape.data(), fixture.chunks.data(), fixture.shape.size());
	Require(init_error == ERROR_OK, "official OM encoder init failed for " + fixture.id + ": " + om_error_string(init_error));

	const auto chunk_count = om_encoder_count_chunks(&encoder);
	Require(chunk_count > 0 && chunk_count < std::numeric_limits<std::size_t>::max(),
	        "invalid official OM chunk count for " + fixture.id);
	const auto compressed_capacity = om_encoder_compressed_chunk_buffer_size(&encoder);
	const auto chunk_capacity = om_encoder_chunk_buffer_size(&encoder);
	Require(compressed_capacity > 0 && chunk_capacity > 0, "official OM encoder returned an empty chunk buffer size");
	Require(compressed_capacity <= std::numeric_limits<std::size_t>::max() &&
	            chunk_capacity <= std::numeric_limits<std::size_t>::max(),
	        "official OM chunk buffer is too large for this process");

	std::vector<std::uint8_t> file(om_header_write_size());
	om_header_write(file.data());
	std::vector<std::uint64_t> lookup(static_cast<std::size_t>(chunk_count) + 1);
	lookup[0] = static_cast<std::uint64_t>(file.size());
	std::vector<std::uint8_t> compressed(static_cast<std::size_t>(compressed_capacity));
	std::vector<std::uint8_t> chunk_buffer(static_cast<std::size_t>(chunk_capacity));
	std::vector<std::uint64_t> array_offset(fixture.shape.size(), 0);

	for (std::uint64_t chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
		const auto compressed_size = om_encoder_compress_chunk(
		    &encoder, fixture.values.data(), fixture.shape.data(), array_offset.data(), fixture.shape.data(), chunk_index,
		    chunk_index, compressed.data(), chunk_buffer.data());
		Require(compressed_size > 0 && compressed_size <= compressed.size(),
		        "official OM encoder returned an invalid compressed chunk length for " + fixture.id);
		Append(file, compressed.data(), static_cast<std::size_t>(compressed_size));
		lookup[static_cast<std::size_t>(chunk_index + 1)] = static_cast<std::uint64_t>(file.size());
	}

	const auto compressed_lookup_capacity = om_encoder_lut_buffer_size(lookup.data(), lookup.size());
	Require(compressed_lookup_capacity > 0 && compressed_lookup_capacity <= std::numeric_limits<std::size_t>::max(),
	        "official OM encoder returned an invalid lookup buffer size");
	std::vector<std::uint8_t> compressed_lookup(static_cast<std::size_t>(compressed_lookup_capacity));
	const auto compressed_lookup_size = om_encoder_compress_lut(
	    lookup.data(), lookup.size(), compressed_lookup.data(), compressed_lookup.size());
	Require(compressed_lookup_size > 0 && compressed_lookup_size <= compressed_lookup.size(),
	        "official OM encoder returned an invalid compressed lookup length");
	const auto lookup_offset = static_cast<std::uint64_t>(file.size());
	Append(file, compressed_lookup.data(), static_cast<std::size_t>(compressed_lookup_size));

	// OM metadata handles are C structs and can be read through typed pointers.
	// Keep the root metadata naturally aligned, as an upstream file writer does.
	while (file.size() % alignof(OmVariableArrayV3_t) != 0) {
		file.push_back(0);
	}
	const auto root_offset = static_cast<std::uint64_t>(file.size());
	const auto root_size = om_variable_write_numeric_array_size(0, 0, fixture.shape.size());
	std::vector<std::uint8_t> root_metadata(root_size, 0);
	om_variable_write_numeric_array(root_metadata.data(), 0, 0, nullptr, nullptr, "", DATA_TYPE_FLOAT_ARRAY,
	                                fixture.compression, fixture.scale_factor, fixture.add_offset,
	                                fixture.shape.size(), fixture.shape.data(),
	                                fixture.chunks.data(), compressed_lookup_size, lookup_offset);
	Append(file, root_metadata.data(), root_metadata.size());

	const auto trailer_size = om_trailer_size();
	while (file.size() % alignof(OmTrailer_t) != 0) {
		file.push_back(0);
	}
	const auto trailer_offset = file.size();
	file.resize(trailer_offset + trailer_size, 0);
	om_trailer_write(file.data() + trailer_offset, root_offset, root_metadata.size());
	return file;
}

std::vector<std::uint8_t> ReadRange(const std::vector<std::uint8_t> &file, std::uint64_t offset, std::uint64_t size,
                                    const std::string &phase) {
	Require(offset <= file.size() && size <= file.size() - static_cast<std::size_t>(offset),
	        "official OM reader requested bytes outside the file during " + phase);
	Require(size <= std::numeric_limits<std::size_t>::max(), "official OM read is too large for this process");
	const auto begin = file.begin() + static_cast<std::ptrdiff_t>(offset);
	return std::vector<std::uint8_t>(begin, begin + static_cast<std::ptrdiff_t>(size));
}

void AlignForArrayMetadata(std::vector<std::uint8_t> &file) {
	while (file.size() % alignof(OmVariableArrayV3_t) != 0) {
		file.push_back(0);
	}
}

void EncodeArrayPayload(std::vector<std::uint8_t> &file, TreeArray &array) {
	const auto &fixture = array.fixture;
	Require(fixture.shape.size() == fixture.chunks.size(), "fixture shape and chunk rank differ: " + fixture.id);
	Require(CheckedProduct(fixture.shape) == fixture.values.size(), "fixture data length does not match shape: " + fixture.id);
	for (std::size_t axis = 0; axis < fixture.shape.size(); axis++) {
		Require(fixture.chunks[axis] > 0 && fixture.chunks[axis] <= fixture.shape[axis],
		        "invalid chunk length in fixture " + fixture.id);
	}

	OmEncoder_t encoder{};
	const auto init_error = om_encoder_init(&encoder, fixture.scale_factor, fixture.add_offset, fixture.compression,
	                                       DATA_TYPE_FLOAT_ARRAY,
	                                       fixture.shape.data(), fixture.chunks.data(), fixture.shape.size());
	Require(init_error == ERROR_OK, "official OM encoder init failed for " + fixture.id + ": " + om_error_string(init_error));
	const auto chunk_count = om_encoder_count_chunks(&encoder);
	Require(chunk_count > 0 && chunk_count < std::numeric_limits<std::size_t>::max(),
	        "invalid official OM chunk count for " + fixture.id);
	const auto compressed_capacity = om_encoder_compressed_chunk_buffer_size(&encoder);
	const auto chunk_capacity = om_encoder_chunk_buffer_size(&encoder);
	Require(compressed_capacity > 0 && chunk_capacity > 0, "official OM encoder returned an empty chunk buffer size");
	Require(compressed_capacity <= std::numeric_limits<std::size_t>::max() &&
	            chunk_capacity <= std::numeric_limits<std::size_t>::max(),
	        "official OM chunk buffer is too large for this process");

	std::vector<std::uint64_t> lookup(static_cast<std::size_t>(chunk_count) + 1);
	lookup[0] = static_cast<std::uint64_t>(file.size());
	std::vector<std::uint8_t> compressed(static_cast<std::size_t>(compressed_capacity));
	std::vector<std::uint8_t> chunk_buffer(static_cast<std::size_t>(chunk_capacity));
	std::vector<std::uint64_t> array_offset(fixture.shape.size(), 0);
	for (std::uint64_t chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
		const auto compressed_size = om_encoder_compress_chunk(
		    &encoder, fixture.values.data(), fixture.shape.data(), array_offset.data(), fixture.shape.data(), chunk_index,
		    chunk_index, compressed.data(), chunk_buffer.data());
		Require(compressed_size > 0 && compressed_size <= compressed.size(),
		        "official OM encoder returned an invalid compressed chunk length for " + fixture.id);
		Append(file, compressed.data(), static_cast<std::size_t>(compressed_size));
		lookup[static_cast<std::size_t>(chunk_index + 1)] = static_cast<std::uint64_t>(file.size());
	}
	const auto lookup_capacity = om_encoder_lut_buffer_size(lookup.data(), lookup.size());
	Require(lookup_capacity > 0 && lookup_capacity <= std::numeric_limits<std::size_t>::max(),
	        "official OM encoder returned an invalid lookup buffer size");
	std::vector<std::uint8_t> compressed_lookup(static_cast<std::size_t>(lookup_capacity));
	const auto lookup_size = om_encoder_compress_lut(lookup.data(), lookup.size(), compressed_lookup.data(),
	                                                compressed_lookup.size());
	Require(lookup_size > 0 && lookup_size <= compressed_lookup.size(),
	        "official OM encoder returned an invalid compressed lookup length");
	array.lut_offset = static_cast<std::uint64_t>(file.size());
	array.lut_size = lookup_size;
	Append(file, compressed_lookup.data(), static_cast<std::size_t>(lookup_size));
}

TreeNode &InsertArray(TreeNode &root, TreeArray &array) {
	Require(!array.segments.empty(), "tree array path must have a segment");
	TreeNode *node = &root;
	for (std::size_t segment_index = 0; segment_index < array.segments.size(); segment_index++) {
		const auto &segment = array.segments[segment_index];
		Require(!segment.empty() && segment.find('\0') == std::string::npos,
		        "tree array path contains an empty or NUL name segment");
		auto child = std::find_if(node->children.begin(), node->children.end(), [&](const TreeNode &candidate) {
			return candidate.name == segment;
		});
		if (child == node->children.end()) {
			node->children.push_back(TreeNode{segment});
			child = std::prev(node->children.end());
		}
		node = &*child;
		if (segment_index + 1 == array.segments.size()) {
			Require(node->array == nullptr && node->children.empty(), "duplicate or conflicting array path in fixture tree");
			node->array = &array;
		} else {
			Require(node->array == nullptr, "array path is also used as a parent container");
		}
	}
	return *node;
}

NodeRef WriteTreeMetadata(std::vector<std::uint8_t> &file, TreeNode &node, bool root = false) {
	if (node.array != nullptr) {
		std::vector<std::uint64_t> child_offsets;
		std::vector<std::uint64_t> child_sizes;
		for (const auto &attribute : node.array->string_attributes) {
			AlignForArrayMetadata(file);
			const auto offset = static_cast<std::uint64_t>(file.size());
			const auto size = om_variable_write_scalar_size(AsNameSize(attribute.first), 0, DATA_TYPE_STRING,
			                                                attribute.second.size());
			std::vector<std::uint8_t> metadata(size, 0);
			om_variable_write_scalar(metadata.data(), AsNameSize(attribute.first), 0, nullptr, nullptr,
			                         attribute.first.data(), DATA_TYPE_STRING, attribute.second.data(),
			                         attribute.second.size());
			Append(file, metadata.data(), metadata.size());
			child_offsets.push_back(offset);
			child_sizes.push_back(size);
		}
		AlignForArrayMetadata(file);
		node.metadata_offset = static_cast<std::uint64_t>(file.size());
		const auto &fixture = node.array->fixture;
		const auto children_count = static_cast<std::uint32_t>(child_offsets.size());
		const auto metadata_size = om_variable_write_numeric_array_size(AsNameSize(node.name), children_count,
		                                                              fixture.shape.size());
		std::vector<std::uint8_t> metadata(metadata_size, 0);
		om_variable_write_numeric_array(metadata.data(), AsNameSize(node.name), children_count,
		                                child_offsets.empty() ? nullptr : child_offsets.data(),
		                                child_sizes.empty() ? nullptr : child_sizes.data(), node.name.data(),
		                                DATA_TYPE_FLOAT_ARRAY, fixture.compression, fixture.scale_factor,
		                                fixture.add_offset,
		                                fixture.shape.size(), fixture.shape.data(), fixture.chunks.data(),
		                                node.array->lut_size, node.array->lut_offset);
		Append(file, metadata.data(), metadata.size());
		node.metadata_size = metadata.size();
		return {node.metadata_offset, node.metadata_size};
	}

	std::vector<std::uint64_t> child_offsets;
	std::vector<std::uint64_t> child_sizes;
	child_offsets.reserve(node.children.size());
	child_sizes.reserve(node.children.size());
	for (auto &child : node.children) {
		const auto child_ref = WriteTreeMetadata(file, child);
		child_offsets.push_back(child_ref.offset);
		child_sizes.push_back(child_ref.size);
	}
	Require(node.children.size() <= std::numeric_limits<std::uint32_t>::max(), "too many children in fixture tree");
	const auto name_size = root ? std::uint16_t{4} : AsNameSize(node.name);
	const std::string root_name = "root";
	const auto &name = root ? root_name : node.name;
	const auto metadata_size = om_variable_write_scalar_size(name_size, static_cast<std::uint32_t>(node.children.size()),
	                                                          DATA_TYPE_NONE, 0);
	std::vector<std::uint8_t> metadata(metadata_size, 0);
	om_variable_write_scalar(metadata.data(), name_size, static_cast<std::uint32_t>(node.children.size()),
	                         child_offsets.empty() ? nullptr : child_offsets.data(),
	                         child_sizes.empty() ? nullptr : child_sizes.data(), name.data(), DATA_TYPE_NONE, nullptr, 0);
	AlignForArrayMetadata(file);
	node.metadata_offset = static_cast<std::uint64_t>(file.size());
	Append(file, metadata.data(), metadata.size());
	node.metadata_size = metadata.size();
	return {node.metadata_offset, node.metadata_size};
}

std::vector<std::uint8_t> EncodeTreeFile(std::vector<TreeArray> &arrays) {
	std::vector<std::uint8_t> file(om_header_write_size());
	om_header_write(file.data());
	TreeNode root;
	root.name = "root";
	for (auto &array : arrays) {
		(void)InsertArray(root, array);
	}
	for (auto &array : arrays) {
		EncodeArrayPayload(file, array);
	}
	const auto root_ref = WriteTreeMetadata(file, root, true);
	while (file.size() % alignof(OmTrailer_t) != 0) {
		file.push_back(0);
	}
	const auto trailer_offset = file.size();
	file.resize(trailer_offset + om_trailer_size(), 0);
	om_trailer_write(file.data() + trailer_offset, root_ref.offset, root_ref.size);
	return file;
}

std::uint16_t AsNameSize(const std::string &name) {
	Require(name.size() <= std::numeric_limits<std::uint16_t>::max(), "OM variable name exceeds UInt16 byte length");
	return static_cast<std::uint16_t>(name.size());
}

std::vector<float> DecodeFullRootArray(const std::filesystem::path &path, std::vector<std::uint64_t> *shape_out = nullptr,
                                       std::vector<std::uint64_t> *chunks_out = nullptr) {
	const auto file = ReadFile(path);
	const auto header_size = om_header_write_size();
	const auto trailer_size = om_trailer_size();
	Require(file.size() >= header_size + trailer_size, "file is too short to contain an OM v3 header and trailer: " + path.string());
	Require(om_header_type(file.data()) == OM_HEADER_READ_TRAILER, "official OM reader rejected the v3 header: " + path.string());

	const auto trailer_offset = static_cast<std::uint64_t>(file.size() - trailer_size);
	std::uint64_t root_offset = 0;
	std::uint64_t root_size = 0;
	Require(om_trailer_read(file.data() + trailer_offset, &root_offset, &root_size),
	        "official OM reader rejected the v3 trailer: " + path.string());
	Require(root_offset <= trailer_offset && root_size <= trailer_offset - root_offset,
	        "OM root metadata range is outside the file: " + path.string());
	const auto *root_data = file.data() + static_cast<std::size_t>(root_offset);
	const auto metadata_error = om_variable_validate(root_data, root_size);
	Require(metadata_error == ERROR_OK,
	        "official OM reader rejected root metadata: " + std::string(om_error_string(metadata_error)));
	const auto *variable = om_variable_init(root_data);
	Require(om_variable_get_type(variable) == DATA_TYPE_FLOAT_ARRAY,
	        "independent official oracle accepts only Float32 array roots: " + path.string());
	Require(om_variable_get_compression(variable) == COMPRESSION_FPX_XOR2D ||
	            om_variable_get_compression(variable) == COMPRESSION_PFOR_DELTA2D_INT16,
	        "independent official oracle accepts only FPX or PFOR Float32 roots: " + path.string());

	const auto rank = om_variable_get_dimensions_count(variable);
	Require(rank > 0 && rank <= 8, "official OM reader found unsupported root array rank");
	const auto *dimension_pointer = om_variable_get_dimensions(variable);
	const auto *chunk_pointer = om_variable_get_chunks(variable);
	std::vector<std::uint64_t> dimensions(dimension_pointer, dimension_pointer + rank);
	std::vector<std::uint64_t> chunks(chunk_pointer, chunk_pointer + rank);
	const auto row_count = CheckedProduct(dimensions);
	Require(row_count <= std::numeric_limits<std::size_t>::max() / sizeof(float), "official OM result is too large for this process");
	if (shape_out != nullptr) {
		*shape_out = dimensions;
	}
	if (chunks_out != nullptr) {
		*chunks_out = chunks;
	}

	std::vector<std::uint64_t> read_offset(dimensions.size(), 0);
	std::vector<std::uint64_t> cube_offset(dimensions.size(), 0);
	OmDecoder_t decoder{};
	const auto decoder_error = om_decoder_init(&decoder, variable, rank, read_offset.data(), dimensions.data(),
	                                          cube_offset.data(), dimensions.data(), IO_SIZE_MERGE, IO_SIZE_MAX);
	Require(decoder_error == ERROR_OK,
	        "official OM decoder init failed: " + std::string(om_error_string(decoder_error)));
	Require(decoder.bytes_per_element == sizeof(float), "official OM decoder selected unexpected Float32 byte width");

	std::vector<float> values(static_cast<std::size_t>(row_count), std::numeric_limits<float>::quiet_NaN());
	const auto scratch_size = om_decoder_read_buffer_size(&decoder);
	Require(scratch_size > 0 && scratch_size <= std::numeric_limits<std::size_t>::max(),
	        "official OM decoder returned an invalid scratch buffer size");
	std::vector<std::uint8_t> scratch(static_cast<std::size_t>(scratch_size));
	OmDecoder_indexRead_t index_read{};
	om_decoder_init_index_read(&decoder, &index_read);
	while (om_decoder_next_index_read(&decoder, &index_read)) {
		const auto index_bytes = ReadRange(file, index_read.offset, index_read.count, "index read");
		OmDecoder_dataRead_t data_read{};
		om_decoder_init_data_read(&data_read, &index_read);
		OmError_t error = ERROR_OK;
		while (om_decoder_next_data_read(&decoder, &data_read, index_bytes.data(), index_bytes.size(), &error)) {
			const auto data_bytes = ReadRange(file, data_read.offset, data_read.count, "data read");
			Require(om_decoder_decode_chunks(&decoder, data_read.chunkIndex, data_bytes.data(), data_bytes.size(),
			                                values.data(), scratch.data(), &error),
			        "official OM full-array decode failed: " + std::string(om_error_string(error)));
		}
		Require(error == ERROR_OK, "official OM index/decode request failed: " + std::string(om_error_string(error)));
	}
	return values;
}

std::vector<float> DecodeArrayVariable(const std::vector<std::uint8_t> &file, const OmVariable_t *variable,
                                       std::vector<std::uint64_t> *shape_out = nullptr,
                                       std::vector<std::uint64_t> *chunks_out = nullptr,
                                       std::uint64_t prefix_count = 0) {
	Require(om_variable_get_type(variable) == DATA_TYPE_FLOAT_ARRAY,
	        "independent official oracle accepts only Float32 array variables");
	Require(om_variable_get_compression(variable) == COMPRESSION_FPX_XOR2D ||
	            om_variable_get_compression(variable) == COMPRESSION_PFOR_DELTA2D_INT16,
	        "independent official oracle accepts only FPX or PFOR Float32 variables");
	const auto rank = om_variable_get_dimensions_count(variable);
	Require(rank > 0 && rank <= 8, "official OM reader found unsupported array rank");
	const auto *dimension_pointer = om_variable_get_dimensions(variable);
	const auto *chunk_pointer = om_variable_get_chunks(variable);
	std::vector<std::uint64_t> dimensions(dimension_pointer, dimension_pointer + rank);
	std::vector<std::uint64_t> chunks(chunk_pointer, chunk_pointer + rank);
	std::vector<std::uint64_t> read_count = dimensions;
	const auto total_rows = CheckedProduct(dimensions);
	const auto prefix_rows = prefix_count == 0 ? total_rows : std::min(prefix_count, total_rows);
	if (prefix_count != 0) {
		std::uint64_t trailing_rows = 1;
		for (std::size_t axis = dimensions.size(); axis > 0; axis--) {
			const auto current_axis = axis - 1;
			const auto required = prefix_rows / trailing_rows + (prefix_rows % trailing_rows != 0);
			read_count[current_axis] = std::min(dimensions[current_axis], required);
			trailing_rows *= dimensions[current_axis];
		}
	}
	const auto row_count = CheckedProduct(read_count);
	Require(row_count <= std::numeric_limits<std::size_t>::max() / sizeof(float),
	        "official OM result is too large for this process");
	if (shape_out != nullptr) {
		*shape_out = dimensions;
	}
	if (chunks_out != nullptr) {
		*chunks_out = chunks;
	}

	std::vector<std::uint64_t> read_offset(dimensions.size(), 0);
	std::vector<std::uint64_t> cube_offset(dimensions.size(), 0);
	OmDecoder_t decoder{};
	const auto decoder_error = om_decoder_init(&decoder, variable, rank, read_offset.data(), read_count.data(),
	                                          cube_offset.data(), read_count.data(), IO_SIZE_MERGE, IO_SIZE_MAX);
	Require(decoder_error == ERROR_OK,
	        "official OM decoder init failed: " + std::string(om_error_string(decoder_error)));
	Require(decoder.bytes_per_element == sizeof(float), "official OM decoder selected unexpected Float32 byte width");
	std::vector<float> values(static_cast<std::size_t>(row_count), std::numeric_limits<float>::quiet_NaN());
	const auto scratch_size = om_decoder_read_buffer_size(&decoder);
	Require(scratch_size > 0 && scratch_size <= std::numeric_limits<std::size_t>::max(),
	        "official OM decoder returned an invalid scratch buffer size");
	std::vector<std::uint8_t> scratch(static_cast<std::size_t>(scratch_size));
	OmDecoder_indexRead_t index_read{};
	om_decoder_init_index_read(&decoder, &index_read);
	while (om_decoder_next_index_read(&decoder, &index_read)) {
		const auto index_bytes = ReadRange(file, index_read.offset, index_read.count, "index read");
		OmDecoder_dataRead_t data_read{};
		om_decoder_init_data_read(&data_read, &index_read);
		OmError_t error = ERROR_OK;
		while (om_decoder_next_data_read(&decoder, &data_read, index_bytes.data(), index_bytes.size(), &error)) {
			const auto data_bytes = ReadRange(file, data_read.offset, data_read.count, "data read");
			Require(om_decoder_decode_chunks(&decoder, data_read.chunkIndex, data_bytes.data(), data_bytes.size(),
			                                values.data(), scratch.data(), &error),
			        "official OM full-array decode failed: " + std::string(om_error_string(error)));
		}
		Require(error == ERROR_OK, "official OM index/decode request failed: " + std::string(om_error_string(error)));
	}
	values.resize(static_cast<std::size_t>(prefix_rows));
	return values;
}

std::string CanonicalVariablePath(const std::vector<std::string> &segments) {
	std::string path;
	for (const auto &segment : segments) {
		path.push_back('/');
		for (const auto byte : segment) {
			if (byte == '%') {
				path += "%25";
			} else if (byte == '/') {
				path += "%2F";
			} else {
				path.push_back(byte);
			}
		}
	}
	return path;
}

void CollectTreeVariables(const std::vector<std::uint8_t> &file, std::uint64_t offset, std::uint64_t size,
                          std::vector<std::string> &segments, std::vector<DecodedVariable> &variables,
                          bool is_root = false) {
	const auto metadata = ReadRange(file, offset, size, "variable metadata read");
	const auto metadata_error = om_variable_validate(metadata.data(), metadata.size());
	Require(metadata_error == ERROR_OK,
	        "official OM reader rejected variable metadata: " + std::string(om_error_string(metadata_error)));
	const auto *variable = om_variable_init(metadata.data());
	const auto layout = _om_variable_memory_layout(variable);
	if (layout == OM_MEMORY_LAYOUT_ARRAY) {
		std::vector<std::uint16_t> name_length(1);
		const auto *name_data = om_variable_get_name(variable, name_length.data());
		Require(name_data != nullptr && name_length[0] > 0, "official OM reader found an empty array name");
		segments.emplace_back(name_data, name_length[0]);
		DecodedVariable decoded;
		decoded.path = CanonicalVariablePath(segments);
		decoded.values = DecodeArrayVariable(file, variable, &decoded.shape, &decoded.chunks);
		variables.push_back(std::move(decoded));
		segments.pop_back();
		return;
	}
	Require(layout == OM_MEMORY_LAYOUT_SCALAR && om_variable_get_type(variable) == DATA_TYPE_NONE,
	        "independent official tree reader accepts only NONE containers and Float32 arrays");
	std::uint16_t container_name_length = 0;
	const auto *container_name_data = om_variable_get_name(variable, &container_name_length);
	if (!is_root) {
		Require(container_name_data != nullptr && container_name_length > 0,
		        "official OM reader found an empty container name");
		segments.emplace_back(container_name_data, container_name_length);
	}
	const auto children_count = om_variable_get_children_count(variable);
	if (children_count == 0) {
		if (!is_root) {
			segments.pop_back();
		}
		return;
	}
	std::vector<std::uint64_t> child_offsets(children_count);
	std::vector<std::uint64_t> child_sizes(children_count);
	Require(om_variable_get_children(variable, 0, children_count, child_offsets.data(), child_sizes.data()),
	        "official OM reader rejected container child references");
	for (std::uint32_t child_index = 0; child_index < children_count; child_index++) {
		const auto child_metadata = ReadRange(file, child_offsets[child_index], child_sizes[child_index], "child metadata read");
		const auto child_error = om_variable_validate(child_metadata.data(), child_metadata.size());
		Require(child_error == ERROR_OK,
		        "official OM reader rejected child metadata: " + std::string(om_error_string(child_error)));
		CollectTreeVariables(file, child_offsets[child_index], child_sizes[child_index], segments, variables);
	}
	if (!is_root) {
		segments.pop_back();
	}
}

std::vector<DecodedVariable> DecodeTreeFile(const std::filesystem::path &path) {
	const auto file = ReadFile(path);
	Require(file.size() >= om_header_write_size() + om_trailer_size(),
	        "file is too short to contain an OM v3 header and trailer: " + path.string());
	Require(om_header_type(file.data()) == OM_HEADER_READ_TRAILER,
	        "official OM reader rejected the v3 header: " + path.string());
	const auto trailer_offset = static_cast<std::uint64_t>(file.size() - om_trailer_size());
	std::uint64_t root_offset = 0;
	std::uint64_t root_size = 0;
	Require(om_trailer_read(file.data() + trailer_offset, &root_offset, &root_size),
	        "official OM reader rejected the v3 trailer: " + path.string());
	Require(root_offset <= trailer_offset && root_size <= trailer_offset - root_offset,
	        "OM root metadata range is outside the file: " + path.string());
	std::vector<DecodedVariable> variables;
	std::vector<std::string> segments;
	CollectTreeVariables(file, root_offset, root_size, segments, variables, true);
	return variables;
}

std::uint32_t FloatBits(float value) {
	std::uint32_t bits = 0;
	static_assert(sizeof(bits) == sizeof(value), "Float32 must be 32 bits");
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

void RequireExactRoundtrip(const Fixture &fixture, const std::vector<float> &decoded) {
	Require(decoded.size() == fixture.values.size(), "official full-file decode returned the wrong element count for " + fixture.id);
	for (std::size_t index = 0; index < decoded.size(); index++) {
		if (FloatBits(decoded[index]) != FloatBits(fixture.values[index])) {
			std::ostringstream message;
			message << "official full-file FPX roundtrip mismatch for " << fixture.id << " at row " << index
			        << " (source bits 0x" << std::hex << FloatBits(fixture.values[index]) << ", decoded bits 0x"
			        << FloatBits(decoded[index]) << ")";
			throw std::runtime_error(message.str());
		}
	}
}

std::string FormatFloat(float value) {
	if (std::isnan(value)) {
		return "nan";
	}
	if (std::isinf(value)) {
		return std::signbit(value) ? "-inf" : "inf";
	}
	if (value == 0.0F && std::signbit(value)) {
		return "-0";
	}
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
	return stream.str();
}

void WriteReferenceCsv(const std::filesystem::path &path, const std::vector<float> &values) {
	if (path.has_parent_path()) {
		std::filesystem::create_directories(path.parent_path());
	}
	std::ofstream stream(path, std::ios::trunc);
	if (!stream) {
		throw std::runtime_error("cannot create reference CSV: " + path.string());
	}
	stream.imbue(std::locale::classic());
	stream << "index,value\n";
	for (std::size_t index = 0; index < values.size(); index++) {
		stream << index << ',' << FormatFloat(values[index]) << '\n';
	}
	Require(stream.good(), "failed while writing reference CSV: " + path.string());
}

std::uint32_t RotateRight(std::uint32_t value, unsigned int bits) {
	return (value >> bits) | (value << (32U - bits));
}

class Sha256 final {
public:
	Sha256() : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
	                  0x1f83d9abU, 0x5be0cd19U} {
	}

	void Update(const std::uint8_t *data, std::size_t length) {
		for (std::size_t index = 0; index < length; index++) {
			buffer_[buffer_size_++] = data[index];
			bit_count_ += 8;
			if (buffer_size_ == buffer_.size()) {
				Transform(buffer_.data());
				buffer_size_ = 0;
			}
		}
	}

	std::string FinalHex() {
		const auto original_bit_count = bit_count_;
		buffer_[buffer_size_++] = 0x80;
		if (buffer_size_ > 56) {
			while (buffer_size_ < 64) {
				buffer_[buffer_size_++] = 0;
			}
			Transform(buffer_.data());
			buffer_size_ = 0;
		}
		while (buffer_size_ < 56) {
			buffer_[buffer_size_++] = 0;
		}
		for (int shift = 56; shift >= 0; shift -= 8) {
			buffer_[buffer_size_++] = static_cast<std::uint8_t>(original_bit_count >> shift);
		}
		Transform(buffer_.data());
		std::ostringstream output;
		output << std::hex << std::setfill('0');
		for (const auto word : state_) {
			output << std::setw(8) << word;
		}
		return output.str();
	}

private:
	void Transform(const std::uint8_t *block) {
		static constexpr std::array<std::uint32_t, 64> constants = {
		    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
		    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
		    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
		    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
	    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
	    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
	    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
	    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
	    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
	    0xc67178f2U};
		std::array<std::uint32_t, 64> words{};
		for (std::size_t index = 0; index < 16; index++) {
			const auto offset = index * 4;
			words[index] = (static_cast<std::uint32_t>(block[offset]) << 24) |
			               (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
			               (static_cast<std::uint32_t>(block[offset + 2]) << 8) |
			               static_cast<std::uint32_t>(block[offset + 3]);
		}
		for (std::size_t index = 16; index < words.size(); index++) {
			const auto s0 = RotateRight(words[index - 15], 7) ^ RotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
			const auto s1 = RotateRight(words[index - 2], 17) ^ RotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
			words[index] = words[index - 16] + s0 + words[index - 7] + s1;
		}
		auto a = state_[0];
		auto b = state_[1];
		auto c = state_[2];
		auto d = state_[3];
		auto e = state_[4];
		auto f = state_[5];
		auto g = state_[6];
		auto h = state_[7];
		for (std::size_t index = 0; index < words.size(); index++) {
			const auto sum1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
			const auto choose = (e & f) ^ (~e & g);
			const auto temp1 = h + sum1 + choose + constants[index] + words[index];
			const auto sum0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
			const auto majority = (a & b) ^ (a & c) ^ (b & c);
			const auto temp2 = sum0 + majority;
			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}
		state_[0] += a;
		state_[1] += b;
		state_[2] += c;
		state_[3] += d;
		state_[4] += e;
		state_[5] += f;
		state_[6] += g;
		state_[7] += h;
	}

	std::array<std::uint32_t, 8> state_;
	std::array<std::uint8_t, 64> buffer_{};
	std::size_t buffer_size_ = 0;
	std::uint64_t bit_count_ = 0;
};

std::string Sha256Hex(const std::vector<std::uint8_t> &bytes) {
	Sha256 digest;
	digest.Update(bytes.data(), bytes.size());
	return digest.FinalHex();
}

std::string JsonEscape(const std::string &input) {
	std::ostringstream output;
	for (const auto character : input) {
		switch (character) {
		case '"':
			output << "\\\"";
			break;
		case '\\':
			output << "\\\\";
			break;
	case '\n':
			output << "\\n";
			break;
		case '\r':
			output << "\\r";
			break;
		case '\t':
			output << "\\t";
			break;
		default:
			output << character;
			break;
		}
	}
	return output.str();
}

std::string JsonNumberArray(const std::vector<std::uint64_t> &values) {
	std::ostringstream output;
	output << '[';
	for (std::size_t index = 0; index < values.size(); index++) {
		if (index != 0) {
			output << ',';
		}
		output << values[index];
	}
	output << ']';
	return output.str();
}

std::string JsonFloatArray(const std::vector<float> &values) {
	std::ostringstream output;
	output.imbue(std::locale::classic());
	output << '[';
	for (std::size_t index = 0; index < values.size(); index++) {
		if (index != 0) {
			output << ',';
		}
		Require(std::isfinite(values[index]), "projection scenario expected values must be finite");
		output << std::setprecision(std::numeric_limits<float>::max_digits10) << values[index];
	}
	output << ']';
	return output.str();
}

std::string JsonNullPositions(const std::vector<float> &values) {
	std::ostringstream output;
	output << '[';
	bool first = true;
	for (std::size_t index = 0; index < values.size(); index++) {
		if (std::isnan(values[index])) {
			if (!first) {
				output << ',';
			}
			first = false;
			output << index;
		}
	}
	output << ']';
	return output.str();
}

std::string JsonStringArray(const std::vector<std::string> &values) {
	std::ostringstream output;
	output << '[';
	for (std::size_t index = 0; index < values.size(); index++) {
		if (index != 0) {
			output << ',';
		}
		output << '"' << JsonEscape(values[index]) << '"';
	}
	output << ']';
	return output.str();
}

std::vector<std::string> SortedExpectedSchema(const std::vector<TreeArray> &arrays) {
	std::vector<std::string> schema;
	schema.reserve(arrays.size());
	for (const auto &array : arrays) {
		schema.push_back(CanonicalVariablePath(array.segments).substr(1) + " FLOAT");
	}
	std::sort(schema.begin(), schema.end());
	return schema;
}

std::vector<TreeArray> BuildMultiArrays() {
	TreeArray temperature;
	temperature.fixture.id = "multi.temperature";
	temperature.fixture.shape = {2, 3};
	temperature.fixture.chunks = {1, 2};
	temperature.fixture.values = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
	temperature.segments = {"temperature"};
	temperature.axes = {"row", "column"};
	temperature.reference_name = "multi.temperature.reference.csv";

	TreeArray humidity;
	humidity.fixture.id = "multi.humidity";
	humidity.fixture.shape = {2, 3};
	humidity.fixture.chunks = {1, 2};
	humidity.fixture.values = {100.0F, 101.0F, 102.0F, 103.0F, 104.0F, 105.0F};
	humidity.segments = {"humidity"};
	humidity.axes = {"row", "column"};
	humidity.reference_name = "multi.humidity.reference.csv";
	return {std::move(temperature), std::move(humidity)};
}

std::vector<TreeArray> BuildSpatialConflictArrays() {
	TreeArray latitude;
	latitude.fixture.id = "spatial_conflict.latitude";
	latitude.fixture.shape = {2, 3};
	latitude.fixture.chunks = {1, 2};
	latitude.fixture.values = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
	latitude.segments = {"Latitude"};
	latitude.axes = {"lat", "lon"};
	latitude.reference_name = "spatial_conflict.latitude.reference.csv";
	return {std::move(latitude)};
}

std::vector<TreeArray> BuildSourceConflictArrays() {
	TreeArray source;
	source.fixture.id = "source_conflict.om_source";
	source.fixture.shape = {2, 3};
	source.fixture.chunks = {1, 2};
	source.fixture.values = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
	source.segments = {"om_source"};
	source.axes = {"row", "column"};
	source.reference_name = "source_conflict.om_source.reference.csv";
	return {std::move(source)};
}

std::vector<TreeArray> BuildPforAttributeArrays() {
	TreeArray temperature;
	temperature.fixture.id = "pfor_attributes.temperature";
	temperature.fixture.shape = {2, 3};
	temperature.fixture.chunks = {1, 3};
	temperature.fixture.values = {1.25F, std::numeric_limits<float>::quiet_NaN(), -2.5F, 3.5F, 4.25F, 5.0F};
	temperature.fixture.compression = COMPRESSION_PFOR_DELTA2D_INT16;
	temperature.fixture.scale_factor = 4.0F;
	temperature.segments = {"temperature"};
	temperature.axes = {"row", "column"};
	temperature.reference_name = "pfor_attributes.temperature.reference.csv";
	temperature.string_attributes = {{"coordinates", "row column"}, {"unit", "Celsius"}};

	TreeArray humidity;
	humidity.fixture.id = "pfor_attributes.humidity";
	humidity.fixture.shape = {2, 3};
	humidity.fixture.chunks = {1, 3};
	humidity.fixture.values = {10.0F, 20.0F, 30.0F, 40.0F, 50.0F, 60.0F};
	humidity.fixture.compression = COMPRESSION_PFOR_DELTA2D_INT16;
	humidity.fixture.scale_factor = 4.0F;
	humidity.segments = {"humidity"};
	humidity.axes = {"row", "column"};
	humidity.reference_name = "pfor_attributes.humidity.reference.csv";
	humidity.string_attributes = {{"coordinates", "row column"}, {"unit", "percent"}};
	return {std::move(temperature), std::move(humidity)};
}

std::vector<TreeArray> BuildNestedArrays() {
	TreeArray first;
	first.fixture.id = "nested.temperature";
	first.fixture.shape = {2, 3};
	first.fixture.chunks = {1, 2};
	first.fixture.values = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
	first.segments = {"temperature"};
	first.axes = {"row", "column"};
	first.reference_name = "nested.temperature.reference.csv";

	TreeArray second;
	second.fixture.id = "nested.layer_b.temperature";
	second.fixture.shape = {2, 3};
	second.fixture.chunks = {2, 2};
	second.fixture.values = {200.0F, 201.0F, 202.0F, 203.0F, 204.0F, 205.0F};
	second.segments = {"layer_b", "temperature"};
	second.axes = {"row", "column"};
	second.reference_name = "nested.layer_b.temperature.reference.csv";

	TreeArray third;
	third.fixture.id = "nested.layer_a.value";
	third.fixture.shape = {2, 3};
	third.fixture.chunks = {1, 2};
	third.fixture.values = {100.0F, 101.0F, 102.0F, 103.0F, 104.0F, 105.0F};
	third.segments = {"layer_a", "value"};
	third.axes = {"row", "column"};
	third.reference_name = "nested.layer_a.value.reference.csv";

	TreeArray fourth;
	fourth.fixture.id = "nested.layer_b.value";
	fourth.fixture.shape = {2, 3};
	fourth.fixture.chunks = {2, 2};
	fourth.fixture.values = {300.0F, 301.0F, 302.0F, 303.0F, 304.0F, 305.0F};
	fourth.segments = {"layer_b", "value"};
	fourth.axes = {"row", "column"};
	fourth.reference_name = "nested.layer_b.value.reference.csv";

	TreeArray encoded_name;
	encoded_name.fixture.id = "nested.encoded_name.value";
	encoded_name.fixture.shape = {2, 3};
	encoded_name.fixture.chunks = {1, 2};
	encoded_name.fixture.values = {400.0F, 401.0F, 402.0F, 403.0F, 404.0F, 405.0F};
	encoded_name.segments = {"weird/name%", "value"};
	encoded_name.axes = {"row", "column"};
	encoded_name.reference_name = "nested.encoded_name.value.reference.csv";
	return {std::move(first), std::move(second), std::move(third), std::move(fourth), std::move(encoded_name)};
}

std::vector<TreeArray> BuildProjectionArrays() {
	constexpr std::uint64_t ROWS = 83;
	constexpr std::uint64_t COLUMNS = 127;
	constexpr std::uint64_t VALUE_COUNT = ROWS * COLUMNS;
	TreeArray humidity;
	humidity.fixture.id = "projection.humidity";
	humidity.fixture.shape = {ROWS, COLUMNS};
	humidity.fixture.chunks = {11, 17};
	humidity.fixture.values.reserve(static_cast<std::size_t>(VALUE_COUNT));
	for (std::uint64_t index = 0; index < VALUE_COUNT; index++) {
		humidity.fixture.values.push_back(static_cast<float>(index % 97));
	}
	humidity.segments = {"humidity"};
	humidity.axes = {"row", "column"};
	humidity.reference_name = "projection.humidity.reference.csv";

	TreeArray pressure;
	pressure.fixture.id = "projection.pressure";
	pressure.fixture.shape = {ROWS, COLUMNS};
	pressure.fixture.chunks = {13, 19};
	pressure.fixture.values.reserve(static_cast<std::size_t>(VALUE_COUNT));
	for (std::uint64_t index = 0; index < VALUE_COUNT; index++) {
		pressure.fixture.values.push_back(1000.0F + static_cast<float>(index) * 0.5F);
	}
	pressure.segments = {"pressure"};
	pressure.axes = {"row", "column"};
	pressure.reference_name = "projection.pressure.reference.csv";

	TreeArray temperature;
	temperature.fixture.id = "projection.temperature";
	temperature.fixture.shape = {ROWS, COLUMNS};
	temperature.fixture.chunks = {7, 23};
	temperature.fixture.values.reserve(static_cast<std::size_t>(VALUE_COUNT));
	for (std::uint64_t index = 0; index < VALUE_COUNT; index++) {
		temperature.fixture.values.push_back(static_cast<float>(index) * 0.25F - 100.0F);
	}
	temperature.segments = {"temperature"};
	temperature.axes = {"row", "column"};
	temperature.reference_name = "projection.temperature.reference.csv";
	return {std::move(humidity), std::move(pressure), std::move(temperature)};
}

struct NegativeAsset final {
	std::string fixture_id;
	std::string file_name;
	std::string source;
	std::string method;
	std::string expected_rejection;
	std::string sha256;
};

std::uint64_t RootMetadataOffset(const std::vector<std::uint8_t> &file, std::uint64_t *size_out = nullptr) {
	Require(file.size() >= om_trailer_size(), "cannot locate root metadata in a file shorter than its trailer");
	std::uint64_t root_offset = 0;
	std::uint64_t root_size = 0;
	Require(om_trailer_read(file.data() + file.size() - om_trailer_size(), &root_offset, &root_size),
	        "cannot locate root metadata in a non-v3 file");
	Require(root_offset <= file.size() - om_trailer_size() &&
	            root_size <= file.size() - om_trailer_size() - root_offset,
	        "root metadata is out of bounds while applying fixture mutation");
	if (size_out != nullptr) {
		*size_out = root_size;
	}
	return root_offset;
}

NodeRef FindMetadataNode(std::vector<std::uint8_t> &file, const std::vector<std::string> &target_segments) {
	std::uint64_t root_size = 0;
	const auto root_offset = RootMetadataOffset(file, &root_size);
	if (target_segments.empty()) {
		return {root_offset, root_size};
	}
	const OmVariable_t *current = om_variable_init(file.data() + static_cast<std::size_t>(root_offset));
	NodeRef current_ref{root_offset, root_size};
	for (const auto &target : target_segments) {
		const auto children_count = om_variable_get_children_count(current);
		std::vector<std::uint64_t> offsets(children_count);
		std::vector<std::uint64_t> sizes(children_count);
		Require(children_count > 0 && om_variable_get_children(current, 0, children_count, offsets.data(), sizes.data()),
		        "cannot find requested mutation target in metadata tree");
		bool found = false;
		for (std::uint32_t index = 0; index < children_count; index++) {
			Require(offsets[index] <= file.size() && sizes[index] <= file.size() - offsets[index],
			        "child metadata is out of bounds while locating mutation target");
			const auto *candidate = om_variable_init(file.data() + static_cast<std::size_t>(offsets[index]));
			std::uint16_t name_size = 0;
			const auto *name = om_variable_get_name(candidate, &name_size);
			if (name != nullptr && std::string(name, name_size) == target) {
				current = candidate;
				current_ref = {offsets[index], sizes[index]};
				found = true;
				break;
			}
		}
		Require(found, "mutation target path segment is absent from metadata tree: " + target);
	}
	return current_ref;
}

std::vector<float> DecodePrefixFile(const std::filesystem::path &path, const std::string &variable_path,
                                    std::uint64_t count) {
	Require(count > 0 && !variable_path.empty() && variable_path.front() == '/',
	        "oracle prefix requires a positive count and an absolute variable path");
	auto file = ReadFile(path);
	std::vector<std::string> segments;
	std::size_t start = 1;
	while (start < variable_path.size()) {
		const auto end = variable_path.find('/', start);
		segments.emplace_back(variable_path.substr(start, end == std::string::npos ? end : end - start));
		Require(!segments.back().empty(), "oracle prefix contains an empty path segment");
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}
	const auto ref = FindMetadataNode(file, segments);
	Require(ref.offset <= file.size() && ref.size <= file.size() - ref.offset,
	        "oracle variable metadata is outside the file");
	const auto *variable = om_variable_init(file.data() + static_cast<std::size_t>(ref.offset));
	Require(om_variable_validate(variable, ref.size) == ERROR_OK, "official OM reader rejected variable metadata");
	return DecodeArrayVariable(file, variable, nullptr, nullptr, count);
}

std::size_t MetadataNameOffset(const std::vector<std::uint8_t> &file, NodeRef ref) {
	const auto *variable = om_variable_init(file.data() + static_cast<std::size_t>(ref.offset));
	if (_om_variable_memory_layout(variable) == OM_MEMORY_LAYOUT_ARRAY) {
		const auto *metadata = reinterpret_cast<const OmVariableArrayV3_t *>(variable);
	return static_cast<std::size_t>(ref.offset + sizeof(OmVariableArrayV3_t) +
		                                16 * metadata->children_count + 16 * metadata->dimension_count);
	}
	const auto *metadata = reinterpret_cast<const OmVariableV3_t *>(variable);
	return static_cast<std::size_t>(ref.offset + sizeof(OmVariableV3_t) + 16 * metadata->children_count);
}

void MutateByte(std::vector<std::uint8_t> &file, std::size_t offset, std::uint8_t value) {
	Require(offset < file.size(), "fixture mutation byte offset is outside the file");
	file[offset] = value;
}

void MutateUInt64(std::vector<std::uint8_t> &file, std::size_t offset, std::uint64_t value) {
	Require(offset <= file.size() && sizeof(value) <= file.size() - offset,
	        "fixture mutation UInt64 offset is outside the file");
	std::memcpy(file.data() + offset, &value, sizeof(value));
}

NegativeAsset WriteNegativeAsset(const std::filesystem::path &directory, const std::string &fixture_id,
	                               const std::string &file_name, const std::string &source,
	                               const std::string &method, const std::string &expected_rejection,
	                               std::vector<std::uint8_t> bytes) {
	WriteFile(directory / file_name, bytes);
	return {fixture_id, file_name, source, method, expected_rejection, Sha256Hex(bytes)};
}

std::vector<NegativeAsset> GenerateNegativeAssets(const std::filesystem::path &directory,
	                                               const std::filesystem::path &multi_path,
	                                               const std::filesystem::path &nested_path) {
	std::vector<NegativeAsset> assets;
	{
		auto bytes = ReadFile(multi_path);
		MutateByte(bytes, 2, 4);
		assets.push_back(WriteNegativeAsset(directory, "unsupported_version", "negative/unsupported_version.om", "multi.om",
		                                    "set OM header version byte at offset 2 from 3 to 4",
		                                    "reject unknown/unsupported OM file version before metadata scan", std::move(bytes)));
	}
	{
		auto bytes = ReadFile(multi_path);
		const auto array = FindMetadataNode(bytes, {"temperature"});
		MutateByte(bytes, static_cast<std::size_t>(array.offset) + offsetof(OmVariableArrayV3_t, data_type),
		           DATA_TYPE_DOUBLE_ARRAY);
		assets.push_back(WriteNegativeAsset(directory, "unsupported_type", "negative/unsupported_type.om", "multi.om",
		                                    "set /temperature metadata data_type to DATA_TYPE_DOUBLE_ARRAY",
		                                    "reject non-Float32 array type at bind time", std::move(bytes)));
	}
	{
		auto bytes = ReadFile(multi_path);
		const auto array = FindMetadataNode(bytes, {"temperature"});
		MutateByte(bytes, static_cast<std::size_t>(array.offset) + offsetof(OmVariableArrayV3_t, compression_type),
		           COMPRESSION_NONE);
		assets.push_back(WriteNegativeAsset(directory, "unsupported_compression", "negative/unsupported_compression.om",
		                                    "multi.om", "set /temperature metadata compression_type to COMPRESSION_NONE",
		                                    "reject compression other than FPX_XOR2D or PFOR_DELTA2D_INT16 at bind time", std::move(bytes)));
	}
	{
		auto bytes = ReadFile(multi_path);
		const auto array = FindMetadataNode(bytes, {"temperature"});
		const auto *metadata = reinterpret_cast<const OmVariableArrayV3_t *>(bytes.data() + array.offset);
		const auto dimension_offset = static_cast<std::size_t>(array.offset) + sizeof(OmVariableArrayV3_t) +
		                              16 * metadata->children_count;
		MutateUInt64(bytes, dimension_offset, 0);
		assets.push_back(WriteNegativeAsset(directory, "empty_array_zero_axis", "negative/empty_array_zero_axis.om",
		                                    "multi.om", "set /temperature dimension[0] from 2 to 0",
		                                    "reject an array with a zero-length axis (empty array)", std::move(bytes)));
	}
	{
		auto bytes = ReadFile(nested_path);
		const auto container = FindMetadataNode(bytes, {"layer_b"});
		const auto name_offset = MetadataNameOffset(bytes, container);
		const std::string replacement = "layer_a";
		Require(replacement.size() == 7, "duplicate-name mutation assumes equal-length layer names");
		std::memcpy(bytes.data() + name_offset, replacement.data(), replacement.size());
		assets.push_back(WriteNegativeAsset(directory, "duplicate_name", "negative/duplicate_name.om", "nested.om",
		                                    "replace /layer_b metadata name bytes with layer_a (both are seven bytes)",
	                                    "reject duplicate canonical paths including /layer_a/value", std::move(bytes)));
	}
	{
		auto bytes = ReadFile(nested_path);
		const auto container = FindMetadataNode(bytes, {"layer_a"});
		const auto *metadata = reinterpret_cast<const OmVariableV3_t *>(bytes.data() + container.offset);
		const auto child_offset_field = static_cast<std::size_t>(container.offset) + sizeof(OmVariableV3_t) +
		                                sizeof(std::uint64_t) * metadata->children_count;
		MutateUInt64(bytes, child_offset_field, static_cast<std::uint64_t>(bytes.size()) + 4096);
		assets.push_back(WriteNegativeAsset(directory, "illegal_reference", "negative/illegal_reference.om", "nested.om",
		                                    "set /layer_a child[0] metadata offset to file_size + 4096",
		                                    "reject an out-of-file metadata child reference", std::move(bytes)));
	}
	{
		auto bytes = ReadFile(multi_path);
		const auto array = FindMetadataNode(bytes, {"humidity"});
		const auto *metadata = reinterpret_cast<const OmVariableArrayV3_t *>(bytes.data() + array.offset);
		const auto dimension_offset = static_cast<std::size_t>(array.offset) + sizeof(OmVariableArrayV3_t) +
		                              16 * metadata->children_count;
		MutateUInt64(bytes, dimension_offset, 3);
		assets.push_back(WriteNegativeAsset(directory, "shape_mismatch", "negative/shape_mismatch.om", "multi.om",
		                                    "set /humidity dimension[0] from 2 to 3 while /temperature remains [2,3]",
		                                    "reject arrays whose shapes differ", std::move(bytes)));
	}
	return assets;
}

const DecodedVariable &FindDecodedVariable(const std::vector<DecodedVariable> &decoded, const std::string &path) {
	const auto found = std::find_if(decoded.begin(), decoded.end(), [&](const DecodedVariable &variable) {
		return variable.path == path;
	});
	Require(found != decoded.end(), "official oracle did not return variable " + path);
	return *found;
}

void WriteTreeFixtureManifestEntry(std::ostringstream &manifest, const std::filesystem::path &directory,
	                               const std::string &fixture_id, const std::string &file_name,
	                               const std::vector<TreeArray> &arrays, const std::vector<DecodedVariable> &decoded) {
	const auto file_bytes = ReadFile(directory / file_name);
	std::vector<const TreeArray *> sorted_arrays;
	for (const auto &array : arrays) {
		sorted_arrays.push_back(&array);
	}
	std::sort(sorted_arrays.begin(), sorted_arrays.end(), [](const TreeArray *left, const TreeArray *right) {
		return CanonicalVariablePath(left->segments) < CanonicalVariablePath(right->segments);
	});

	manifest << "    {\n"
	         << "      \"fixture_id\": \"" << JsonEscape(fixture_id) << "\",\n"
	         << "      \"path\": \"" << JsonEscape(file_name) << "\",\n"
	         << "      \"sha256\": \"" << Sha256Hex(file_bytes) << "\",\n"
	         << "      \"source\": \"official OM C encoder and metadata writer\",\n"
	         << "      \"format_version\": 3,\n"
	         << "      \"variable_path\": \"/\",\n"
	         << "      \"type\": \"NONE container\",\n"
	         << "      \"compression\": \"NONE\",\n"
	         << "      \"shape\": null,\n"
	         << "      \"chunk_shape\": null,\n"
	         << "      \"axes\": {";
	for (std::size_t index = 0; index < sorted_arrays.size(); index++) {
		const auto &array = *sorted_arrays[index];
		if (index != 0) {
			manifest << ',';
		}
		manifest << "\"" << JsonEscape(CanonicalVariablePath(array.segments)) << "\":" << JsonStringArray(array.axes);
	}
	manifest << "},\n"
	         << "      \"expected_schema\": " << JsonStringArray(SortedExpectedSchema(arrays)) << ",\n"
	         << "      \"variables\": [\n";
	for (std::size_t index = 0; index < sorted_arrays.size(); index++) {
		const auto &array = *sorted_arrays[index];
		const auto variable_path = CanonicalVariablePath(array.segments);
		const auto &reference = FindDecodedVariable(decoded, variable_path);
		const auto reference_bytes = ReadFile(directory / array.reference_name);
		manifest << "        {\"variable_path\": \"" << JsonEscape(variable_path) << "\", \"type\": \"Float32\", "
		         << "\"compression\": \""
		         << (array.fixture.compression == COMPRESSION_PFOR_DELTA2D_INT16 ? "PFOR_DELTA2D_INT16" : "FPX_XOR2D")
		         << "\", \"shape\": " << JsonNumberArray(reference.shape)
		         << ", \"chunk_shape\": " << JsonNumberArray(reference.chunks)
		         << ", \"axes\": " << JsonStringArray(array.axes)
		         << ", \"expected_column\": \"" << JsonEscape(variable_path.substr(1) + " FLOAT") << "\""
		         << ", \"null_positions\": " << JsonNullPositions(reference.values)
		         << ", \"reference_csv\": \"" << JsonEscape(array.reference_name) << "\""
		         << ", \"reference_csv_sha256\": \"" << Sha256Hex(reference_bytes) << "\""
		         << ", \"comparison\": {\"finite_float32_tolerance\": 0, \"roundtrip\": \"bitwise official decode\"}}"
		         << (index + 1 == sorted_arrays.size() ? "\n" : ",\n");
	}
	manifest << "      ]\n"
	         << "    }";
}

std::string ProjectionReadOmSql(const std::string &select_list, const std::string &suffix = "") {
	return "SELECT " + select_list +
	       " FROM read_om('test/data/projection.om', dimensions := map("
	       "['humidity', 'pressure', 'temperature'], "
	       "[['row', 'column'], ['row', 'column'], ['row', 'column']]))" + suffix;
}

void WriteProjectionScenarioManifest(std::ostringstream &manifest, const std::vector<TreeArray> &arrays,
	                                 const std::vector<DecodedVariable> &decoded) {
	Require(arrays.size() == 3, "projection scenario expectations require three variables");
	const auto &humidity = FindDecodedVariable(decoded, "/humidity");
	const auto &temperature = FindDecodedVariable(decoded, "/temperature");
	Require(humidity.shape == temperature.shape, "projection scenario variables must share a shape");
	std::vector<std::uint64_t> matching_positions;
	std::vector<float> filtered_temperature;
	for (std::size_t index = 0; index < humidity.values.size(); index++) {
		if (humidity.values[index] == 96.0F) {
			matching_positions.push_back(index);
			filtered_temperature.push_back(temperature.values[index]);
		}
	}
	Require(!matching_positions.empty(), "projection filter scenario must produce rows");

	const auto row_count = CheckedProduct(humidity.shape);
	manifest << "  \"projection_scenarios\": {\n"
	         << "    \"fixture_id\": \"projection\",\n"
	         << "    \"shape\": " << JsonNumberArray(humidity.shape) << ",\n"
	         << "    \"standard_vector_size\": 2048,\n"
	         << "    \"row_count\": " << row_count << ",\n"
	         << "    \"scenarios\": [\n"
	         << "      {\"scenario_id\": \"full_scan\", \"sql\": \""
	         << JsonEscape(ProjectionReadOmSql("\"humidity\", \"pressure\", \"temperature\"",
	                                             " ORDER BY \"temperature\""))
	         << "\", \"result_row_count\": " << row_count
	         << ", \"output_variables\": [\"humidity\", \"pressure\", \"temperature\"]"
	         << ", \"expected_values_reference_csvs\": {"
	         << "\"humidity\": \"projection.humidity.reference.csv\", "
	         << "\"pressure\": \"projection.pressure.reference.csv\", "
	         << "\"temperature\": \"projection.temperature.reference.csv\"}},\n"
	         << "      {\"scenario_id\": \"single_variable\", \"sql\": \""
	         << JsonEscape(ProjectionReadOmSql("\"temperature\"", " ORDER BY \"temperature\""))
	         << "\", \"result_row_count\": " << row_count
	         << ", \"output_variables\": [\"temperature\"]"
	         << ", \"expected_values_reference_csv\": \"projection.temperature.reference.csv\"},\n"
	         << "      {\"scenario_id\": \"output_plus_filter\", \"sql\": \""
	         << JsonEscape(ProjectionReadOmSql("\"temperature\"", " WHERE \"humidity\" = 96 ORDER BY \"temperature\""))
	         << "\", \"result_row_count\": " << filtered_temperature.size()
	         << ", \"output_variables\": [\"temperature\"], \"filter_variables\": [\"humidity\"]"
	         << ", \"predicate\": \"humidity = 96\", \"expected_row_positions\": "
	         << JsonNumberArray(matching_positions) << ", \"expected_output_values\": "
	         << JsonFloatArray(filtered_temperature) << "},\n"
	         << "      {\"scenario_id\": \"count\", \"sql\": \""
	         << JsonEscape(ProjectionReadOmSql("COUNT(*)")) << "\", \"result_row_count\": 1"
	         << ", \"count_value\": " << row_count << ", \"expected_values\": [" << row_count << "]}\n"
	         << "    ]\n"
	         << "  },\n";
}

void WriteManifest(const std::filesystem::path &directory, const std::vector<Fixture> &fixtures,
	               const std::vector<TreeArray> &multi_arrays, const std::vector<DecodedVariable> &multi_decoded,
	               const std::vector<TreeArray> &spatial_conflict_arrays,
	               const std::vector<DecodedVariable> &spatial_conflict_decoded,
	               const std::vector<TreeArray> &source_conflict_arrays,
	               const std::vector<DecodedVariable> &source_conflict_decoded,
	               const std::vector<TreeArray> &pfor_arrays, const std::vector<DecodedVariable> &pfor_decoded,
	               const std::vector<TreeArray> &nested_arrays, const std::vector<DecodedVariable> &nested_decoded,
	               const std::vector<TreeArray> &projection_arrays,
	               const std::vector<DecodedVariable> &projection_decoded,
	               const std::vector<NegativeAsset> &negative_assets) {
	std::ostringstream manifest;
	manifest.imbue(std::locale::classic());
	manifest << "{\n"
	         << "  \"schema_version\": 1,\n"
	         << "  \"upstream_om_commit\": \"" << OM_UPSTREAM_COMMIT << "\",\n"
	         << "  \"generation_command\": \"" << JsonEscape(GENERATION_COMMAND) << "\",\n"
	         << "  \"oracle\": \"fixed OM C API: om_variable_init + om_decoder_* full-array decode\",\n"
	         << "  \"fixtures\": [\n";
	for (std::size_t index = 0; index < fixtures.size(); index++) {
		const auto &fixture = fixtures[index];
		const auto om_bytes = ReadFile(directory / fixture.file_name);
		const auto reference_bytes = ReadFile(directory / fixture.reference_name);
		std::vector<std::uint64_t> decoded_shape;
		std::vector<std::uint64_t> decoded_chunks;
		(void)DecodeFullRootArray(directory / fixture.file_name, &decoded_shape, &decoded_chunks);
		manifest << "    {\n"
		         << "      \"fixture_id\": \"" << JsonEscape(fixture.id) << "\",\n"
		         << "      \"path\": \"" << JsonEscape(fixture.file_name) << "\",\n"
		         << "      \"sha256\": \"" << Sha256Hex(om_bytes) << "\",\n"
		         << "      \"source\": \"official OM C encoder and metadata writer\",\n"
	         << "      \"format_version\": 3,\n"
	         << "      \"variable_path\": \"/\",\n"
	         << "      \"type\": \"Float32\",\n"
	         << "      \"compression\": \"FPX_XOR2D\",\n"
	         << "      \"shape\": " << JsonNumberArray(decoded_shape) << ",\n"
	         << "      \"chunk_shape\": " << JsonNumberArray(decoded_chunks) << ",\n"
	         << "      \"axes\": " << JsonStringArray(fixture.axes) << ",\n"
	         << "      \"expected_schema\": [\"value FLOAT\"],\n"
	         << "      \"null_positions\": " << JsonNullPositions(fixture.values) << ",\n"
	         << "      \"reference_csv\": \"" << JsonEscape(fixture.reference_name) << "\",\n"
	         << "      \"reference_csv_sha256\": \"" << Sha256Hex(reference_bytes) << "\",\n"
			         << "      \"comparison\": {\"finite_float32_tolerance\": 0, \"roundtrip\": \"bitwise full-file\"}\n"
			         << "    },\n";
	}
	// Keep every valid OM input in one manifest collection for downstream validators.
	WriteTreeFixtureManifestEntry(manifest, directory, "multi", "multi.om", multi_arrays, multi_decoded);
	manifest << ",\n";
	WriteTreeFixtureManifestEntry(manifest, directory, "spatial_conflict", "spatial_conflict.om",
	                              spatial_conflict_arrays, spatial_conflict_decoded);
	manifest << ",\n";
	WriteTreeFixtureManifestEntry(manifest, directory, "source_conflict", "source-conflict.om",
	                              source_conflict_arrays, source_conflict_decoded);
	manifest << ",\n";
	WriteTreeFixtureManifestEntry(manifest, directory, "pfor_attributes", "pfor_attributes.om", pfor_arrays,
	                              pfor_decoded);
	manifest << ",\n";
	WriteTreeFixtureManifestEntry(manifest, directory, "nested", "nested.om", nested_arrays, nested_decoded);
	manifest << ",\n";
	WriteTreeFixtureManifestEntry(manifest, directory, "projection", "projection.om", projection_arrays,
	                              projection_decoded);
	manifest << "\n  ],\n";
	WriteProjectionScenarioManifest(manifest, projection_arrays, projection_decoded);
	manifest
	         << "  \"negative_assets\": [\n";
	for (std::size_t index = 0; index < negative_assets.size(); index++) {
		const auto &asset = negative_assets[index];
		manifest << "    {\"fixture_id\": \"" << JsonEscape(asset.fixture_id) << "\", "
		         << "\"path\": \"" << JsonEscape(asset.file_name) << "\", "
		         << "\"sha256\": \"" << asset.sha256 << "\", "
		         << "\"source_fixture\": \"" << JsonEscape(asset.source) << "\", "
		         << "\"mutation\": \"" << JsonEscape(asset.method) << "\", "
		         << "\"expected_rejection\": \"" << JsonEscape(asset.expected_rejection) << "\"}"
		         << (index + 1 == negative_assets.size() ? "\n" : ",\n");
	}
	manifest << "  ],\n"
	         << "  \"negative_cases\": [\n"
	         << "    {\"fixture_id\": \"missing_axis_mapping\", \"source_fixture\": \"multi.om\", \"method\": \"omit /humidity from dimensions map while retaining /temperature\", \"expected_rejection\": \"dimensions keys must exactly cover all array paths\"},\n"
	         << "    {\"fixture_id\": \"different_axis_identity\", \"source_fixture\": \"multi.om\", \"method\": \"declare /humidity axes as [row,other] and /temperature axes as [row,column]\", \"expected_rejection\": \"ordered axis identities differ\"}\n"
	         << "  ],\n"
	         << "  \"truncation_mutations\": [\n"
	         << "    {\"source\": \"raw.om\", \"method\": \"copy the file and remove its final byte\", \"expected_error\": \"truncated OM trailer\"}\n"
	         << "  ]\n"
	         << "}\n";
	const auto text = manifest.str();
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	WriteFile(directory / "manifest.json", bytes);

	// This oracle is authored from the fixture generator's logical coordinate
	// definition, independently of read_om's axis parser and selection code.
	std::ostringstream coordinates;
	coordinates.imbue(std::locale::classic());
	coordinates << "fixture_id,logical_position,time_index,valid_time,level_index,level,lead_time_index,lead_time,"
	                "member_index,member,run_index,run,value,is_missing\n";
	for (std::uint64_t position = 0; position < 6; position++) {
		const auto time = position / 3;
		const auto member = position % 3;
		coordinates << "dimensions," << position << ',' << time << ",2026-09-30 " << (time == 0 ? "00" : "01")
		            << ":00:00,,,,," << member << ',' << (10 + member * 10) << ",,," << (time * 3 + member)
		            << ",false\n";
	}
	for (std::uint64_t position = 0; position < 6; position++) {
		const auto member = position / 2;
		const auto time = position % 2;
		coordinates << "dimensions_axis_order," << position << ',' << time << ",2026-09-30 "
		            << (time == 0 ? "00" : "01") << ":00:00,,,,," << member << ',' << (10 + member * 10)
		            << ",,," << (time * 3 + member) << ",false\n";
	}
	for (std::uint64_t position = 0; position < 32; position++) {
		const auto time = position / 16;
		const auto level = (position / 8) % 2;
		const auto lead = (position / 4) % 2;
		const auto member = (position / 2) % 2;
		const auto run = position % 2;
		const bool missing = position == 7;
		coordinates << "dimensions_five_axes," << position << ',' << time << ",2026-09-30 "
		            << (time == 0 ? "00" : "01") << ":00:00," << level << ','
		            << (level == 0 ? "500.25" : "850.5") << ',' << lead << ','
		            << (lead == 0 ? "-01:00:00" : "00:00:00") << ',' << member << ','
		            << (member == 0 ? "10" : "9223372036854775807") << ',' << run << ",2026-09-29 "
		            << (run == 0 ? "00:00:00" : "12:00:00") << ',';
		if (missing) coordinates << "NULL";
		else coordinates << position;
		coordinates << ',' << (missing ? "true" : "false") << '\n';
	}
	const auto coordinate_text = coordinates.str();
	WriteFile(directory / "dimensions-coordinates.csv",
	          std::vector<std::uint8_t>(coordinate_text.begin(), coordinate_text.end()));

	const auto perf = std::find_if(fixtures.begin(), fixtures.end(), [](const Fixture &fixture) {
		return fixture.id == "dimensions_perf";
	});
	Require(perf != fixtures.end(), "dimensions performance fixture is missing from the fixture list");
	const auto perf_bytes = ReadFile(directory / perf->file_name);
	const auto perf_reference = ReadFile(directory / perf->reference_name);
	std::ostringstream perf_manifest;
	perf_manifest << "{\n"
	              << "  \"schema_version\": 1,\n"
	              << "  \"fixture_id\": \"dimensions_perf\",\n"
	              << "  \"path\": \"" << JsonEscape(perf->file_name) << "\",\n"
	              << "  \"sha256\": \"" << Sha256Hex(perf_bytes) << "\",\n"
	              << "  \"reference_csv\": \"" << JsonEscape(perf->reference_name) << "\",\n"
	              << "  \"reference_csv_sha256\": \"" << Sha256Hex(perf_reference) << "\",\n"
	              << "  \"shape\": " << JsonNumberArray(perf->shape) << ",\n"
	              << "  \"chunk_shape\": " << JsonNumberArray(perf->chunks) << ",\n"
	              << "  \"row_count\": " << CheckedProduct(perf->shape) << ",\n"
	              << "  \"task_window_positions\": 65536,\n"
	              << "  \"queries\": {\n"
	              << "    \"full\": \"SELECT sum(value) FROM read_om('dimensions_perf.om', dimensions := map(['value'], [['time','member']]), axes := {'time': {'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'}, 'member': {'axis':'member','start':0,'step':1}})\",\n"
	              << "    \"local\": \"SELECT sum(value) FROM read_om('dimensions_perf.om', dimensions := map(['value'], [['time','member']]), axes := {'time': {'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'}, 'member': {'axis':'member','start':0,'step':1}}) WHERE valid_time = TIMESTAMP '2026-01-01 00:00:00' AND member BETWEEN 100 AND 103\",\n"
	              << "    \"parallel_threads\": [1, 2, 4]\n"
	              << "  },\n"
	              << "  \"acceptance\": {\"local_value_bytes_less_than_full\": true, \"local_decoded_chunks_less_than_full\": true, \"parallel_worker_evidence_required\": true}\n"
	              << "}\n";
	const auto perf_text = perf_manifest.str();
	WriteFile(directory / "dimensions-perf-manifest.json",
	          std::vector<std::uint8_t>(perf_text.begin(), perf_text.end()));
}

std::vector<Fixture> BuildInitialFixtures() {
	Fixture raw{"raw", "raw.om", "raw.reference.csv", {2, 3}, {1, 2}, {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F}};
	Fixture special{"special", "special.om", "special.reference.csv", {2, 4}, {1, 3},
	                {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
	                 -std::numeric_limits<float>::infinity(), 0.0F, -0.0F, 42.25F, -3.5F, 123.125F}};
	Fixture raw_large;
	raw_large.id = "raw_large";
	raw_large.file_name = "raw_large.om";
	raw_large.reference_name = "raw_large.reference.csv";
	raw_large.shape = {73, 61};
	raw_large.chunks = {7, 13};
	raw_large.values.reserve(static_cast<std::size_t>(CheckedProduct(raw_large.shape)));
	for (std::uint64_t row = 0; row < raw_large.shape[0]; row++) {
		for (std::uint64_t column = 0; column < raw_large.shape[1]; column++) {
			const auto ordinal = row * raw_large.shape[1] + column;
			raw_large.values.push_back(static_cast<float>(ordinal) * 0.25F - 100.0F);
		}
	}
	Fixture dimensions;
	dimensions.id = "dimensions";
	dimensions.file_name = "dimensions.om";
	dimensions.reference_name = "dimensions.reference.csv";
	dimensions.shape = {2, 3};
	dimensions.chunks = {1, 2};
	dimensions.axes = {"time", "member"};
	dimensions.values = {0, 1, 2, 3, 4, 5};
	Fixture dimensions_axis_order;
	dimensions_axis_order.id = "dimensions_axis_order";
	dimensions_axis_order.file_name = "dimensions_axis_order.om";
	dimensions_axis_order.reference_name = "dimensions_axis_order.reference.csv";
	dimensions_axis_order.shape = {3, 2};
	dimensions_axis_order.chunks = {2, 1};
	dimensions_axis_order.axes = {"member", "time"};
	for (std::uint64_t member = 0; member < 3; member++) {
		for (std::uint64_t time = 0; time < 2; time++) {
			dimensions_axis_order.values.push_back(static_cast<float>(time * 3 + member));
		}
	}
	Fixture dimensions_five_axes;
	dimensions_five_axes.id = "dimensions_five_axes";
	dimensions_five_axes.file_name = "dimensions_five_axes.om";
	dimensions_five_axes.reference_name = "dimensions_five_axes.reference.csv";
	dimensions_five_axes.shape = {2, 2, 2, 2, 2};
	dimensions_five_axes.chunks = {1, 2, 1, 2, 1};
	dimensions_five_axes.axes = {"time", "level", "lead_time", "member", "run"};
	for (std::uint64_t ordinal = 0; ordinal < CheckedProduct(dimensions_five_axes.shape); ordinal++) {
		dimensions_five_axes.values.push_back(ordinal == 7 ? std::numeric_limits<float>::quiet_NaN()
		                                                   : static_cast<float>(ordinal));
	}
	Fixture dimensions_perf;
	dimensions_perf.id = "dimensions_perf";
	dimensions_perf.file_name = "dimensions_perf.om";
	dimensions_perf.reference_name = "dimensions_perf.reference.csv";
	dimensions_perf.shape = {512, 256};
	dimensions_perf.chunks = {32, 32};
	dimensions_perf.axes = {"time", "member"};
	dimensions_perf.values.reserve(static_cast<std::size_t>(CheckedProduct(dimensions_perf.shape)));
	for (std::uint64_t ordinal = 0; ordinal < CheckedProduct(dimensions_perf.shape); ordinal++) {
		dimensions_perf.values.push_back(static_cast<float>(ordinal) * 0.25F - 100.0F);
	}
	Fixture spatial_flat;
	spatial_flat.id = "spatial_flat";
	spatial_flat.file_name = "spatial_flat.om";
	spatial_flat.reference_name = "spatial_flat.reference.csv";
	spatial_flat.shape = {2, 6};
	spatial_flat.chunks = {1, 2};
	spatial_flat.axes = {"sample", "point"};
	for (std::uint64_t sample = 0; sample < 2; sample++) {
		for (std::uint64_t point = 0; point < 6; point++) {
			spatial_flat.values.push_back(static_cast<float>(sample * 100 + point));
		}
	}
	Fixture spatial_axes;
	spatial_axes.id = "spatial_axes";
	spatial_axes.file_name = "spatial_axes.om";
	spatial_axes.reference_name = "spatial_axes.reference.csv";
	spatial_axes.shape = {2, 2, 3};
	spatial_axes.chunks = {1, 1, 2};
	spatial_axes.axes = {"member", "latitude_axis", "longitude_axis"};
	for (std::uint64_t index = 0; index < CheckedProduct(spatial_axes.shape); index++) {
		spatial_axes.values.push_back(index == 5 ? std::numeric_limits<float>::quiet_NaN()
		                                         : static_cast<float>(index) * 1.25F - 4.0F);
	}
	Fixture spatial_single;
	spatial_single.id = "spatial_single";
	spatial_single.file_name = "spatial_single.om";
	spatial_single.reference_name = "spatial_single.reference.csv";
	spatial_single.shape = {1, 1};
	spatial_single.chunks = {1, 1};
	spatial_single.axes = {"latitude_axis", "longitude_axis"};
	spatial_single.values = {42.0F};
	return {std::move(raw), std::move(special), std::move(spatial_flat), std::move(spatial_axes),
	        std::move(spatial_single), std::move(raw_large), std::move(dimensions),
	        std::move(dimensions_axis_order), std::move(dimensions_five_axes), std::move(dimensions_perf)};
}

TreeArray MakeGridArray(const std::string &id, std::vector<std::string> segments, std::vector<std::string> axes,
                        std::vector<std::uint64_t> shape, std::vector<std::uint64_t> chunks,
                        const std::string &reference_name) {
	TreeArray array;
	array.fixture.id = id;
	array.fixture.shape = std::move(shape);
	array.fixture.chunks = std::move(chunks);
	array.segments = std::move(segments);
	array.axes = std::move(axes);
	array.reference_name = reference_name;
	return array;
}

std::vector<TreeArray> BuildGridLayoutArrays() {
	// The value at every logical location identifies its row and column.  The
	// sample manifest supplies negative native steps for this [y,x] payload.
	auto reverse = MakeGridArray("grids.reverse", {"reverse", "value"},
	                             {"latitude_axis", "longitude_axis"}, {3, 4}, {2, 2},
	                             "grids/layouts.reverse.csv");
	for (std::uint64_t y = 0; y < 3; y++) {
		for (std::uint64_t x = 0; x < 4; x++) {
			reverse.fixture.values.push_back(static_cast<float>(y * 100 + x));
		}
	}

	auto x_fastest = MakeGridArray("grids.x_fastest", {"flattened_x_fastest", "value"}, {"point"},
	                               {12}, {5}, "grids/layouts.x_fastest.csv");
	for (std::uint64_t point = 0; point < 12; point++) {
		const auto y = point / 4;
		const auto x = point % 4;
		x_fastest.fixture.values.push_back(static_cast<float>(y * 100 + x));
	}

	auto y_fastest = MakeGridArray("grids.y_fastest", {"flattened_y_fastest", "value"}, {"point"},
	                               {12}, {5}, "grids/layouts.y_fastest.csv");
	for (std::uint64_t point = 0; point < 12; point++) {
		const auto y = point % 3;
		const auto x = point / 3;
		y_fastest.fixture.values.push_back(static_cast<float>(y * 100 + x));
	}

	// Keep the spatial axes apart in source order while carrying every semantic
	// axis category through the same multi-variable source layout.
	constexpr std::uint64_t TIME = 2;
	constexpr std::uint64_t NY = 3;
	constexpr std::uint64_t LEVEL = 2;
	constexpr std::uint64_t NX = 4;
	constexpr std::uint64_t LEAD_TIME = 2;
	constexpr std::uint64_t MEMBER = 2;
	constexpr std::uint64_t RUN = 2;
	const std::vector<std::uint64_t> interleaved_shape{TIME, NY, LEVEL, NX, LEAD_TIME, MEMBER, RUN};
	const std::vector<std::uint64_t> interleaved_chunks{1, 2, 1, 3, 1, 2, 1};
	const std::vector<std::string> interleaved_axes{
	    "time", "latitude_axis", "level", "longitude_axis", "lead_time", "member", "run"};
	auto temperature = MakeGridArray("grids.interleaved.temperature", {"interleaved", "temperature"},
	                                 interleaved_axes, interleaved_shape, interleaved_chunks,
	                                 "grids/layouts.interleaved.temperature.csv");
	auto humidity = MakeGridArray("grids.interleaved.humidity", {"interleaved", "humidity"},
	                              interleaved_axes, interleaved_shape, interleaved_chunks,
	                              "grids/layouts.interleaved.humidity.csv");
	const auto interleaved_rows = CheckedProduct(interleaved_shape);
	for (std::uint64_t logical = 0; logical < interleaved_rows; logical++) {
		auto remainder = logical;
		const auto run = remainder % RUN;
		remainder /= RUN;
		const auto member = remainder % MEMBER;
		remainder /= MEMBER;
		const auto lead_time = remainder % LEAD_TIME;
		remainder /= LEAD_TIME;
		const auto x = remainder % NX;
		remainder /= NX;
		const auto level = remainder % LEVEL;
		remainder /= LEVEL;
		const auto y = remainder % NY;
		const auto time = remainder / NY;
		const auto tagged_position = time * 100000 + y * 10000 + level * 1000 + x * 100 +
		                             lead_time * 10 + member * 2 + run;
		temperature.fixture.values.push_back(static_cast<float>(tagged_position) - 1000.0F);
		humidity.fixture.values.push_back(static_cast<float>(tagged_position) + 2000.0F);
	}
	return {std::move(reverse), std::move(x_fastest), std::move(y_fastest), std::move(temperature),
	        std::move(humidity)};
}

std::vector<TreeArray> BuildGridLongLineArrays() {
	constexpr std::uint64_t POINTS = 262145;
	auto line = MakeGridArray("grids.long_line", {"long_line", "value"},
	                          {"latitude_axis", "longitude_axis"}, {1, POINTS}, {1, 4096},
	                          "grids/long-line.reference.csv");
	line.fixture.values.reserve(static_cast<std::size_t>(POINTS));
	for (std::uint64_t point = 0; point < POINTS; point++) {
		line.fixture.values.push_back(static_cast<float>(point) * 0.25F - 32768.0F);
	}
	return {std::move(line)};
}

std::vector<TreeArray> BuildGridUnknownCrsArrays() {
	auto array = MakeGridArray("grids.unknown_crs", {"unknown_crs", "value"},
	                           {"latitude_axis", "longitude_axis"}, {2, 3}, {1, 2},
	                           "grids/crs-conflict.reference.csv");
	array.fixture.values = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
	array.string_attributes = {{"crs_wkt", "LOCAL_CS[\"unrecognized test profile\"]"}};
	return {std::move(array)};
}

std::vector<TreeArray> BuildGridGaussianWgs84CrsArrays() {
	auto array = MakeGridArray("grids.gaussian_wgs84_crs", {"gaussian_wgs84", "value"}, {"point"},
	                           {12}, {6}, "grids/crs-wgs84-gaussian.reference.csv");
	for (std::uint64_t point = 0; point < 12; point++) {
		array.fixture.values.push_back(static_cast<float>(point));
	}
	array.string_attributes = {{"crs_wkt",
	    "GEOGCRS[\"Reduced Gaussian Grid\", DATUM[\"World Geodetic System 1984\", "
	    "ELLIPSOID[\"WGS 84\",6378137,298.257223563]], CS[ellipsoidal,2], "
	    "AXIS[\"latitude\",north], AXIS[\"longitude\",east], "
	    "ANGLEUNIT[\"degree\",0.0174532925199433], "
	    "USAGE[SCOPE[\"grid\"],BBOX[-90,-180.0,90,180]]]"}};
	return {std::move(array)};
}

std::vector<TreeArray> BuildGridN160IdentityArrays() {
	constexpr std::uint64_t POINT_COUNT = 138346;
	auto array = MakeGridArray("grids.n160_identity", {"n160_identity", "value"}, {"point"},
	                           {POINT_COUNT}, {1024}, "grids/gaussian-n160-identity.reference.csv");
	array.fixture.values.reserve(static_cast<std::size_t>(POINT_COUNT));
	for (std::uint64_t point = 0; point < POINT_COUNT; point++) {
		array.fixture.values.push_back(static_cast<float>((point * 17) % 1009));
	}
	return {std::move(array)};
}

std::vector<TreeArray> BuildGridMemoryArrays(bool large) {
	constexpr std::uint64_t CHUNK = 32;
	const std::uint64_t side = large ? 1024 : 256;
	const std::string id = large ? "grids.memory_large" : "grids.memory_small";
	const std::string name = large ? "memory-large" : "memory-small";
	auto array = MakeGridArray(id, {"value"}, {"latitude_axis", "longitude_axis"},
	                           {side, side}, {CHUNK, CHUNK}, "grids/" + name + ".reference.csv");
	const auto point_count = CheckedProduct(array.fixture.shape);
	array.fixture.values.reserve(static_cast<std::size_t>(point_count));
	for (std::uint64_t row = 0; row < side; row++) {
		for (std::uint64_t column = 0; column < side; column++) {
			// Every fixed 32x32 chunk has identical values in both objects. This
			// freezes the same maximum compressed chunk size for the work-peak pair.
			const auto within_chunk = (row % CHUNK) * CHUNK + (column % CHUNK);
			array.fixture.values.push_back(static_cast<float>(within_chunk) * 0.25F);
		}
	}
	return {std::move(array)};
}

std::vector<TreeArray> BuildGaussianWorkArrays(bool large) {
	constexpr std::uint64_t SMALL_POINTS = 256;
	constexpr std::uint64_t LARGE_POINTS = 3968;
	constexpr std::uint64_t CHUNK = 32;
	const auto point_count = large ? LARGE_POINTS : SMALL_POINTS;
	const auto id = large ? "grids.gaussian_work_large" : "grids.gaussian_work_small";
	const auto name = large ? "gaussian-work-large" : "gaussian-work-small";
	auto array = MakeGridArray(id, {"gaussian_work", "value"}, {"point"}, {point_count}, {CHUNK},
	                           "grids/" + std::string(name) + ".reference.csv");
	array.fixture.values.reserve(static_cast<std::size_t>(point_count));
	for (std::uint64_t point = 0; point < point_count; point++) {
		// Each 32-point compressed chunk repeats the same sequence in both files.
		array.fixture.values.push_back(static_cast<float>(point % CHUNK) * 0.25F);
	}
	return {std::move(array)};
}

std::vector<TreeArray> BuildSpatialRelationGaussianArrays() {
	auto array = MakeGridArray("grids.spatial_relation_gaussian", {"spatial_relations", "gaussian_value"},
	                           {"point"}, {8}, {4}, "grids/spatial-relations-gaussian.reference.csv");
	for (std::uint64_t point = 0; point < 8; point++) {
		array.fixture.values.push_back(static_cast<float>(point));
	}
	return {std::move(array)};
}

std::vector<TreeArray> BuildGridLargeChunkArrays() {
	constexpr std::uint64_t SIDE = 256;
	auto array = MakeGridArray("grids.large_chunk", {"large_chunk", "value"},
	                           {"latitude_axis", "longitude_axis"}, {SIDE, SIDE}, {SIDE, SIDE},
	                           "grids/large-value-chunk.reference.csv");
	const auto point_count = CheckedProduct(array.fixture.shape);
	array.fixture.values.reserve(static_cast<std::size_t>(point_count));
	for (std::uint64_t point = 0; point < point_count; point++) {
		array.fixture.values.push_back(static_cast<float>(point) * 0.25F - 8192.0F);
	}
	return {std::move(array)};
}

void WriteGridSampleManifest(const std::filesystem::path &output_directory) {
	const auto grids_directory = output_directory / "grids";
	const std::string selection_cases =
	    "{\n"
	    "  \"schema_version\": 1,\n"
	    "  \"classification\": \"synthetic selector stress inputs; not a real grid or source-order oracle\",\n"
	    "  \"cases\": [\n"
	    "    {\"id\": \"fragmented_4096\", \"fixture\": \"long-line.om\", \"point_count\": 262145, \"positions\": \"2*i for i in [0,4096)\", \"expected_disjoint_ranges\": 4096},\n"
	    "    {\"id\": \"fragmented_4097\", \"fixture\": \"long-line.om\", \"point_count\": 262145, \"positions\": \"2*i for i in [0,4097)\", \"expected_disjoint_ranges\": 4097},\n"
	    "    {\"id\": \"rotated_north_pole\", \"fixture\": null, \"native_only\": true, \"grid\": {\"type\": \"rotated_latlon\", \"nx\": 1, \"ny\": 1, \"x0\": 0, \"y0\": 90, \"dx\": 1, \"dy\": -1, \"north_pole_latitude\": 90, \"north_pole_longitude\": 0, \"rotation\": 0}, \"position\": [0,0], \"expected_coordinate\": [90,0]},\n"
	    "    {\"id\": \"lambert_cone_apex\", \"fixture\": null, \"native_only\": true, \"grid\": {\"type\": \"lambert_conformal_conic\", \"nx\": 1, \"ny\": 1, \"x0\": 0, \"y0\": 1.8649332100338825, \"dx\": 1, \"dy\": 1, \"longitude_of_false_origin\": 0, \"latitude_of_false_origin\": 0, \"standard_parallel_1\": 45, \"standard_parallel_2\": 45, \"radius_m\": 1}, \"expected_error\": \"InvalidShape\"},\n"
	    "    {\"id\": \"stereographic_pole_center\", \"fixture\": null, \"native_only\": true, \"grid\": {\"type\": \"stereographic\", \"nx\": 1, \"ny\": 1, \"x0\": 0, \"y0\": 0, \"dx\": 1, \"dy\": 1, \"latitude_of_origin\": 90, \"longitude_of_origin\": 37, \"radius_m\": 6371229, \"scale_factor\": 1}, \"position\": [0,0], \"expected_coordinate\": [90,37]}\n"
	    "  ]\n"
	    "}\n";
	WriteFile(grids_directory / "selection-cases.json",
	          std::vector<std::uint8_t>(selection_cases.begin(), selection_cases.end()));

	struct FixtureRecord final {
		const char *id;
		const char *file;
		std::vector<std::vector<std::uint64_t>> shapes;
		std::vector<std::vector<std::uint64_t>> chunks;
		std::vector<std::string> variables;
		const char *layout;
	};
	const std::vector<FixtureRecord> records = {
	    {"layouts", "layouts.om", {{3, 4}, {12}, {12}, {2, 3, 2, 4, 2, 2, 2}, {2, 3, 2, 4, 2, 2, 2}},
	     {{2, 2}, {5}, {5}, {1, 2, 1, 3, 1, 2, 1}, {1, 2, 1, 3, 1, 2, 1}},
	     {"reverse/value", "flattened_x_fastest/value", "flattened_y_fastest/value",
	      "interleaved/temperature", "interleaved/humidity"}, "reverse, both flattened orders, five semantic axes, and two aligned variables"},
	    {"reverse_layout", "reverse.om", {{3, 4}}, {{2, 2}}, {"reverse/value"},
	     "separate [latitude,longitude] axes with negative source direction"},
	    {"x_fastest_layout", "x-fastest.om", {{12}}, {{5}}, {"flattened_x_fastest/value"},
	     "flattened longitude-fastest source order"},
	    {"y_fastest_layout", "y-fastest.om", {{12}}, {{5}}, {"flattened_y_fastest/value"},
	     "flattened latitude-fastest source order"},
	    {"interleaved_axes", "interleaved.om", {{2, 3, 2, 4, 2, 2, 2}, {2, 3, 2, 4, 2, 2, 2}},
	     {{1, 2, 1, 3, 1, 2, 1}, {1, 2, 1, 3, 1, 2, 1}},
	     {"interleaved/temperature", "interleaved/humidity"},
	     "five semantic axes interleaved with two spatial axes and two aligned variables"},
	    {"unknown_crs", "crs-conflict.om", {{2, 3}}, {{1, 2}}, {"unknown_crs/value"},
	     "explicit unknown CRS profile is retained and rejected by version-one geographic binding"},
	    {"gaussian_wgs84_crs", "crs-wgs84-gaussian.om", {{12}}, {{6}}, {"gaussian_wgs84/value"},
	     "closed WGS84 Reduced Gaussian source profile with axis and angular-unit evidence"},
	    {"gaussian_n160_identity", "gaussian-n160-identity.om", {{138346}}, {{1024}}, {"n160_identity/value"},
	     "synthetic point-axis fixture for N160 registered-versus-explicit SQL identity checks"},
	    {"long_line", "long-line.om", {{1, 262145}}, {{1, 4096}}, {"long_line/value"},
	     "one scanline larger than four 65536-position selector windows"},
		{"memory_small", "memory-small.om", {{256, 256}}, {{32, 32}}, {"value"},
		 "16x baseline for fixed 32x32 chunk, four projected-grid work-memory comparisons"},
		{"memory_large", "memory-large.om", {{1024, 1024}}, {{32, 32}}, {"value"},
		 "16x spatial point count with identical 32x32 compressed chunks"},
	    {"gaussian_work_small", "gaussian-work-small.om", {{256}}, {{32}}, {"gaussian_work/value"},
	     "reduced-Gaussian work-memory baseline; explicit two-row grid, 256 points"},
	    {"gaussian_work_large", "gaussian-work-large.om", {{3968}}, {{32}}, {"gaussian_work/value"},
	     "15.5x reduced-Gaussian spatial point count with the same chunk and narrow output row"},
	    {"spatial_relation_gaussian", "spatial-relations-gaussian.om", {{8}}, {{4}},
	     {"spatial_relations/gaussian_value"},
	     "eight-point synthetic Gaussian source matching the independent full point/polygon relation example"},
	    {"large_value_chunk", "large-value-chunk.om", {{256, 256}}, {{256, 256}}, {"large_chunk/value"},
		 "one 256 KiB value chunk for coordinate-only decoder isolation"},
	};
	const std::string public_open_meteo_samples = R"json({
  "registry": {
    "name": "Open-Meteo AWS Open Data",
    "repository": "https://github.com/open-meteo/open-data",
    "commit": "4fd52ad16c417c49bff45fab4bf175e5ea5760f2",
    "readme_path": "README.md",
    "readme_sha256": "85c2b2bee77119dfaa7b09ff96fac49bd06b8bb7cbad54e50537fbb7267c75a1",
    "bucket": "openmeteo",
    "region": "us-west-2",
    "registry_url": "https://registry.opendata.aws/open-meteo/",
    "bucket_explorer_url": "https://openmeteo.s3.amazonaws.com/index.html#data/",
    "shape_convention": "data/ arrays are [ny,nx,ntime]; data_spatial/ arrays are [ny,nx]"
  },
  "coverage": {
    "status": "four_grid_family_samples_acquired; required_target_coordinates_and_values_not_validated",
    "required": ["rotated_v3", "lambert_v3", "stereographic_v3", "gaussian_n160_v3", "gaussian_n320_v3", "gaussian_n320_region_v3"],
    "acquired_metadata_checked": ["rotated_v3", "lambert_v3", "stereographic_v3", "gaussian_o1280_supplemental", "gaussian_hres_o1280_timeseries_supplemental"],
    "not_acquired": [
      {"id":"gaussian_n160_v3","reason":"No matching N160 OM v3 sample was found in the audited public prefixes; current ECMWF SEAS5 metadata identifies O320, not N160."},
      {"id":"gaussian_n320_v3","reason":"No full-grid N320 producer object is fixed in the pinned source; current public ECMWF IFS metadata identifies O1280 and SEAS5/EC46 metadata identifies O320."},
      {"id":"gaussian_n320_region_v3","reason":"Public ecmwf_aifs025_ensemble objects exist, but their current metadata identifies WGS 84 world coverage; the exact ecmwf_aifs_europe_ensemble prefixes are empty and no matching N320 Gaussian-region object was found."}
    ],
    "coordinate_reference": "not-run_for_required_target_grids",
    "official_value_decode_reference": "partial_supplemental_o1280_only; see test/data/grids/value-reference-manifest.json",
    "spatial_read_benefit": "not-run",
    "signed_s3_authorization_and_server_audit": "not-run; public anonymous bucket access is not a substitute"
  },
  "objects": [
    {
      "id":"rotated_cmc_gem_rdps_10km_cape_chunk_4337",
      "grid_definition_id":"gem_rdps_10km",
      "classification":"real_public_open_meteo_om_v3",
      "source_object":{"s3_uri":"s3://openmeteo/data/cmc_gem_rdps_10km/cape/chunk_4337.om","https_url":"https://openmeteo.s3.us-west-2.amazonaws.com/data/cmc_gem_rdps_10km/cape/chunk_4337.om","key":"data/cmc_gem_rdps_10km/cape/chunk_4337.om","public_read":true,"version_id":null,"etag":"\"a8983c4d6c56028dd1e3e7aa34ca8b36-9\"","last_modified":"2026-06-01T10:05:30Z"},
      "size_bytes":68016448,
      "sha256":"3db637197d27c65051e417b850f8c3656d1c5371ab78dcbd9fc05d0e440956f7",
      "format_version":3,
      "external_variable":"cape",
      "array":{"variable_path":"/","shape":[1045,1140,114],"chunk_shape":[1,26,114],"axes":["latitude","longitude","time"],"axes_embedded_in_array_metadata":false,"axis_order_evidence":"pinned open-meteo/open-data README [ny,nx,ntime] convention"},
      "grid_source":{"repository":"https://github.com/open-meteo/open-meteo","commit":"b06f4760fd1f997e5559bb380f64c5e496b4a509","path":"Sources/App/Gem/GemDomain.swift"},
      "metadata_validation":"OM v3 header/trailer and root array validated with the official OM C metadata API; DuckDB binding accepts the local object; full value decode and coordinate comparison not-run",
      "local_copy":{"path":"build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om","fetch_command":"python3 scripts/fetch-openmeteo-grid-samples.py"},
      "remote_identity":"No S3 VersionId; multipart ETag is not a content hash. SHA-256 pins the downloaded local bytes; remote identity remains weak."
    },
    {
      "id":"stereographic_cmc_gem_rdps_cape_chunk_4337",
      "grid_definition_id":"gem_regional",
      "classification":"real_public_open_meteo_om_v3",
      "source_object":{"s3_uri":"s3://openmeteo/data/cmc_gem_rdps/cape/chunk_4337.om","https_url":"https://openmeteo.s3.us-west-2.amazonaws.com/data/cmc_gem_rdps/cape/chunk_4337.om","key":"data/cmc_gem_rdps/cape/chunk_4337.om","public_read":true,"version_id":null,"etag":"\"41caf313c49cbcddf586d29ca251e8b0-3\"","last_modified":"2026-05-26T10:02:24Z"},
      "size_bytes":23835720,
      "sha256":"1d701b188c8c264532f0d8fa917400a66cf28aa1c8e53a7b40781a775333c7c1",
      "format_version":3,
      "external_variable":"cape",
      "array":{"variable_path":"/","shape":[824,935,114],"chunk_shape":[1,26,114],"axes":["latitude","longitude","time"],"axes_embedded_in_array_metadata":false,"axis_order_evidence":"pinned open-meteo/open-data README [ny,nx,ntime] convention"},
      "grid_source":{"repository":"https://github.com/open-meteo/open-meteo","commit":"b06f4760fd1f997e5559bb380f64c5e496b4a509","path":"Sources/App/Gem/GemDomain.swift"},
      "metadata_validation":"OM v3 header/trailer and root array validated with the official OM C metadata API; DuckDB binding accepts the local object; full value decode and coordinate comparison not-run",
      "local_copy":{"path":"build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om","fetch_command":"python3 scripts/fetch-openmeteo-grid-samples.py"},
      "remote_identity":"No S3 VersionId; multipart ETag is not a content hash. SHA-256 pins the downloaded local bytes; remote identity remains weak."
    },
    {
      "id":"lambert_chmi_aladin_central_europe_2km_cape_chunk_4135",
      "grid_definition_id":"aladin_central_europe_2km",
      "classification":"real_public_open_meteo_om_v3",
      "source_object":{"s3_uri":"s3://openmeteo/data/chmi_aladin_central_europe_2km/cape/chunk_4135.om","https_url":"https://openmeteo.s3.us-west-2.amazonaws.com/data/chmi_aladin_central_europe_2km/cape/chunk_4135.om","key":"data/chmi_aladin_central_europe_2km/cape/chunk_4135.om","public_read":true,"version_id":null,"etag":"\"fc8408a0e10f2d569522dc95848781c9-6\"","last_modified":"2026-08-14T22:50:39Z"},
      "size_bytes":48727672,
      "sha256":"cef407b0aae43498485b8f6e25907c1ecf8a82c6cb8aa2185fe860f0c491132c",
      "format_version":3,
      "external_variable":"cape",
      "array":{"variable_path":"/","shape":[837,1053,120],"chunk_shape":[1,25,120],"axes":["latitude","longitude","time"],"axes_embedded_in_array_metadata":false,"axis_order_evidence":"pinned open-meteo/open-data README [ny,nx,ntime] convention"},
      "grid_source":{"repository":"https://github.com/open-meteo/open-meteo","commit":"b06f4760fd1f997e5559bb380f64c5e496b4a509","path":"Sources/App/Chmi/ChmiDomain.swift"},
      "metadata_validation":"OM v3 header/trailer and root array validated with the official OM C metadata API; DuckDB binding accepts the local object; full value decode and coordinate comparison not-run",
      "local_copy":{"path":"build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om","fetch_command":"python3 scripts/fetch-openmeteo-grid-samples.py"},
      "remote_identity":"No S3 VersionId; multipart ETag is not a content hash. SHA-256 pins the downloaded local bytes; remote identity remains weak."
    },
    {
      "id":"ecmwf_ifs_static_hsurf_reduced_gaussian_o1280",
      "grid_definition_id":null,
      "scope":"supplemental_gaussian_family_example; O1280 is not the required N160/N320/N-region and O/F grids are excluded from 004 acceptance",
      "classification":"real_public_open_meteo_om_v3",
      "source_object":{"s3_uri":"s3://openmeteo/data/ecmwf_ifs/static/HSURF.om","https_url":"https://openmeteo.s3.us-west-2.amazonaws.com/data/ecmwf_ifs/static/HSURF.om","key":"data/ecmwf_ifs/static/HSURF.om","public_read":true,"version_id":null,"etag":"\"91286afefb256fd70a8e34b18fd4ddf7\"","last_modified":"2025-10-21T09:36:01Z"},
      "size_bytes":2482560,
      "sha256":"2e8279f8bd12052ba5be5a0bcba2293dc4316097e434d6691f9e6711249e1fd3",
      "format_version":3,
      "external_variable":"HSURF",
      "array":{"variable_path":"/","shape":[1,6599680],"chunk_shape":[1,400],"axes":null,"axes_embedded_in_array_metadata":false,"mapping_status":"not mapped; do not infer O1280 point order from shape"},
      "grid_metadata_object":{"s3_uri":"s3://openmeteo/data/ecmwf_ifs/static/meta.json","sha256":"c8dd4aa8e6eb07e1708742fc0e28e23e2516857ca156f55596e97bad65d86bd0","size_bytes":658,"last_modified":"2026-10-06T06:50:24Z","etag":"\"203586194241279275a8cff6dcbb54fb\"","crs_wkt_remark":"Reduced Gaussian Grid O1280 (ECMWF)"},
      "metadata_validation":"OM v3 header/trailer and root array validated; Open-Meteo WKT metadata identifies O1280; the full value array matches an official OM C API decode by logical index; O1280 row/point mapping and coordinates remain not-run",
      "full_value_reference":{"manifest":"test/data/grids/value-reference-manifest.json","official_csv":"build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.official-reference.csv","official_csv_sha256":"083300d4593e326236b76dfb2d55485058fdbf360a27bfab6a04a64d01eab91a","duckomo_csv":"build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.duckomo-output.csv","duckomo_csv_sha256":"558dcb725586cb638bf129c2666c2c06be7f651bd0808e159f71ae095c9201be","positions_compared":6599680,"mismatches":0},
      "local_copy":{"path":"build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.om","fetch_command":"python3 scripts/fetch-openmeteo-grid-samples.py"},
      "remote_identity":"No S3 VersionId; SHA-256 pins the downloaded local bytes. This sample is supplemental Gaussian-family evidence and does not satisfy N160/N320/N-region requirements."
    },
    {
      "id":"ecmwf_ifs_hres_temperature_2m_chunk_817_o1280_supplemental",
      "grid_definition_id":null,
      "scope":"supplemental_real_hres_timeseries; ECMWF IFS HRES O1280 is a reduced Gaussian grid but not the required N160/N320/N-region",
      "classification":"real_public_open_meteo_om_v3",
      "source_object":{"s3_uri":"s3://openmeteo/data/ecmwf_ifs/temperature_2m/chunk_817.om","https_url":"https://openmeteo.s3.us-west-2.amazonaws.com/data/ecmwf_ifs/temperature_2m/chunk_817.om","key":"data/ecmwf_ifs/temperature_2m/chunk_817.om","public_read":true,"version_id":null,"etag":"\"8aacc014058aca39a7a1ae9800fbef58-43\"","last_modified":"2025-04-03T01:11:58Z"},
      "size_bytes":353999720,
      "sha256":"483dd0be2096d3e3fff6731509400f97591ebcfc237a941a37460f42a7a10e7f",
      "format_version":3,
      "external_variable":"temperature_2m",
      "array":{"variable_path":"/","shape":[1,6599680,504],"chunk_shape":[1,6,504],"axes":null,"axes_embedded_in_array_metadata":false,"mapping_status":"OM metadata shape/chunks checked; axis semantics and O1280 row/point order not mapped"},
      "grid_metadata_object":{"s3_uri":"s3://openmeteo/data/ecmwf_ifs/static/meta.json","sha256":"c8dd4aa8e6eb07e1708742fc0e28e23e2516857ca156f55596e97bad65d86bd0","size_bytes":658,"last_modified":"2026-10-06T06:50:24Z","etag":"\"203586194241279275a8cff6dcbb54fb\"","crs_wkt_remark":"Reduced Gaussian Grid O1280 (ECMWF); WGS84 ellipsoid 6378137/298.257223563"},
      "metadata_validation":"OM v3 header/trailer and root array metadata validated with official OM C API: shape [1,6599680,504], chunks [1,6,504]; DuckDB read_om binds as value FLOAT; no value decode, coordinate, or axis-order reference claimed",
      "local_copy":{"path":"build/s3-samples/openmeteo-v3/ecmwf-ifs-temperature-2m-chunk-817.om","fetch_command":"python3 scripts/fetch-openmeteo-grid-samples.py"},
      "remote_identity":"No S3 VersionId; multipart ETag is not a content hash. SHA-256 pins the downloaded local bytes; remote identity remains weak."
    }
  ]
})json";
	std::ostringstream manifest;
	manifest.imbue(std::locale::classic());
	manifest << "{\n"
	         << "  \"schema_version\": 1,\n"
	         << "  \"classification\": \"synthetic_fixtures_plus_public_real_samples\",\n"
	         << "  \"fixture_classification\": \"synthetic_only\",\n"
	         << "  \"generated_by\": \"duckomo_fixture_tool --output\",\n"
	         << "  \"om_format_version\": 3,\n"
	         << "  \"oracle\": \"fixed OM C API full-array decode; generated values are not producer data\",\n"
	         << "  \"real_sample_coverage\": {\"status\": \"four_grid_family_samples_frozen; required_n_grid_coordinates_and_values_not_validated\", \"required\": [\"rotated_v3\", \"lambert_v3\", \"stereographic_v3\", \"gaussian_n160_v3\", \"gaussian_n320_v3\", \"gaussian_n320_region_v3\"], \"acquired_metadata_checked\": [\"rotated_v3\", \"lambert_v3\", \"stereographic_v3\", \"gaussian_o1280_supplemental\", \"gaussian_hres_o1280_timeseries_supplemental\"], \"family_samples\": {\"rotated_latlon\": \"rotated_cmc_gem_rdps_10km_cape_chunk_4337\", \"lambert_conformal_conic\": \"lambert_chmi_aladin_central_europe_2km_cape_chunk_4135\", \"stereographic\": \"stereographic_cmc_gem_rdps_cape_chunk_4337\", \"reduced_gaussian\": \"ecmwf_ifs_hres_temperature_2m_chunk_817_o1280_supplemental\"}, \"required_gaussian_coverage\": {\"n160\": \"not-run: no matching native OM v3 object\", \"n320_full\": \"not-run: O1280 HRES is a different grid identity\", \"n320_region\": \"not-run: public AIFS product is WGS84 world grid; N320 local-to-parent order absent\"}, \"skip_block_candidates\": [\"rotated_cmc_gem_rdps_10km_cape_chunk_4337\", \"stereographic_cmc_gem_rdps_cape_chunk_4337\", \"lambert_chmi_aladin_central_europe_2km_cape_chunk_4135\", \"ecmwf_ifs_static_hsurf_reduced_gaussian_o1280\"], \"skip_block_performance\": \"not-run; candidate chunks are frozen but spatial mapping/cold full-local comparison is incomplete\", \"supplemental_out_of_scope\": [\"ecmwf_ifs_static_hsurf_reduced_gaussian_o1280\", \"ecmwf_ifs_hres_temperature_2m_chunk_817_o1280_supplemental\"], \"coordinate_reference\": \"not-run_for_required_target_grids\", \"official_value_decode_reference\": \"partial_supplemental_o1280_only; see test/data/grids/value-reference-manifest.json\"},\n"
	         << "  \"public_open_meteo_samples\": " << public_open_meteo_samples << ",\n"
	         << "  \"fixtures\": [\n";
	for (std::size_t index = 0; index < records.size(); index++) {
		const auto &record = records[index];
		const auto file_bytes = ReadFile(grids_directory / record.file);
		manifest << "    {\"id\": \"" << JsonEscape(record.id) << "\", \"path\": \"" << JsonEscape(record.file)
		         << "\", \"sha256\": \"" << Sha256Hex(file_bytes) << "\", \"size_bytes\": " << file_bytes.size()
		         << ", \"grid_kind\": \"synthetic_layout_input\", \"description\": \"" << JsonEscape(record.layout)
		         << "\", \"variables\": [";
		for (std::size_t variable_index = 0; variable_index < record.variables.size(); variable_index++) {
			if (variable_index != 0) manifest << ", ";
			manifest << "{\"path\": \"/" << JsonEscape(record.variables[variable_index]) << "\", \"shape\": "
			         << JsonNumberArray(record.shapes[variable_index]) << ", \"chunk_shape\": "
			         << JsonNumberArray(record.chunks[variable_index]) << "}";
		}
		manifest << "]}" << (index + 1 == records.size() ? "\n" : ",\n");
	}
	const auto selection_bytes = ReadFile(grids_directory / "selection-cases.json");
	manifest << "  ],\n"
	         << "  \"selection_cases\": {\"path\": \"selection-cases.json\", \"sha256\": \""
	         << Sha256Hex(selection_bytes) << "\", \"size_bytes\": " << selection_bytes.size()
	         << ", \"scope\": \"synthetic selector inputs; pole/singularity vectors are native-only\"},\n"
	         << "  \"coordinate_reference\": null,\n"
	         << "  \"value_reference\": \"per-fixture CSVs in the enclosing test/data/manifest.json\",\n"
	         << "  \"limitations\": [\"real public samples are metadata-checked only; independent coordinate and official full-value references remain not-run\", "
	            "\"public ECMWF IFS metadata identifies an O1280 Gaussian sample, which cannot substitute for required N160/N320/N320-region samples\", "
	            "\"public anonymous S3 access does not satisfy signed-S3 authorization or server-audit gates\", "
	            "\"synthetic fixtures do not satisfy real-sample acceptance\", "
	            "\"selection-case position formulas do not prove SQL candidate completeness\"]\n"
	         << "}\n";
	const auto text = manifest.str();
	WriteFile(grids_directory / "sample-manifest.json", std::vector<std::uint8_t>(text.begin(), text.end()));
}

void GenerateGridTreeFixture(const std::filesystem::path &output_directory, const std::string &file_name,
	                         std::vector<TreeArray> &arrays) {
	const auto bytes = EncodeTreeFile(arrays);
	const auto path = output_directory / file_name;
	WriteFile(path, bytes);
	const auto decoded = DecodeTreeFile(path);
	Require(decoded.size() == arrays.size(), "official grid fixture oracle returned the wrong array count for " + file_name);
	for (const auto &array : arrays) {
		const auto variable_path = CanonicalVariablePath(array.segments);
		const auto &reference = FindDecodedVariable(decoded, variable_path);
		Require(reference.shape == array.fixture.shape && reference.chunks == array.fixture.chunks,
		        "official grid fixture oracle returned different metadata for " + variable_path);
		RequireExactRoundtrip(array.fixture, reference.values);
		const auto reference_path = output_directory / array.reference_name;
		WriteReferenceCsv(reference_path, reference.values);
		const auto reference_bytes = ReadFile(reference_path);
		std::cout << "generated " << file_name << " variable=" << variable_path
		          << " shape=" << JsonNumberArray(reference.shape)
		          << " chunks=" << JsonNumberArray(reference.chunks)
		          << " rows=" << reference.values.size()
		          << " oracle=passed csv_sha256=" << Sha256Hex(reference_bytes) << '\n';
	}
	std::cout << "generated " << file_name << " variables=" << arrays.size()
	          << " oracle=passed sha256=" << Sha256Hex(bytes) << '\n';
}

void Generate(const std::filesystem::path &output_directory) {
	std::filesystem::create_directories(output_directory);
	auto fixtures = BuildInitialFixtures();
	for (const auto &fixture : fixtures) {
		const auto encoded = EncodeRootArray(fixture);
		WriteFile(output_directory / fixture.file_name, encoded);
		std::vector<std::uint64_t> decoded_shape;
		std::vector<std::uint64_t> decoded_chunks;
		const auto decoded = DecodeFullRootArray(output_directory / fixture.file_name, &decoded_shape, &decoded_chunks);
		Require(decoded_shape == fixture.shape, "official oracle returned a different shape for " + fixture.id);
		Require(decoded_chunks == fixture.chunks, "official oracle returned different chunk dimensions for " + fixture.id);
		RequireExactRoundtrip(fixture, decoded);
		WriteReferenceCsv(output_directory / fixture.reference_name, decoded);
		std::cout << "generated " << fixture.file_name << " shape=" << JsonNumberArray(fixture.shape)
		          << " chunks=" << JsonNumberArray(fixture.chunks) << " rows=" << decoded.size()
	          << " oracle=passed sha256=" << Sha256Hex(encoded) << '\n';
	}
	const auto raw_large = std::find_if(fixtures.begin(), fixtures.end(), [](const Fixture &fixture) {
		return fixture.id == "raw_large";
	});
	Require(raw_large != fixtures.end(), "raw_large fixture is missing from the fixture list");
	const auto large_rows = CheckedProduct(raw_large->shape);
	Require(large_rows > 2 * 2048, "raw_large fixture no longer crosses more than two DuckDB vector batches");
	Require(raw_large->shape[0] != raw_large->shape[1], "raw_large fixture must be non-square");

	auto multi_arrays = BuildMultiArrays();
	const auto multi_bytes = EncodeTreeFile(multi_arrays);
	const auto multi_path = output_directory / "multi.om";
	WriteFile(multi_path, multi_bytes);
	const auto multi_decoded = DecodeTreeFile(multi_path);
	Require(multi_decoded.size() == multi_arrays.size(), "official tree oracle returned the wrong multi.om array count");
	for (const auto &array : multi_arrays) {
		const auto variable_path = CanonicalVariablePath(array.segments);
		const auto &decoded = FindDecodedVariable(multi_decoded, variable_path);
		Require(decoded.shape == array.fixture.shape && decoded.chunks == array.fixture.chunks,
		        "official tree oracle returned different metadata for " + variable_path);
		RequireExactRoundtrip(array.fixture, decoded.values);
		WriteReferenceCsv(output_directory / array.reference_name, decoded.values);
		std::cout << "generated multi.om variable=" << variable_path << " rows=" << decoded.values.size()
		          << " oracle=passed sha256=" << Sha256Hex(multi_bytes) << '\n';
	}

	auto spatial_conflict_arrays = BuildSpatialConflictArrays();
	const auto spatial_conflict_bytes = EncodeTreeFile(spatial_conflict_arrays);
	const auto spatial_conflict_path = output_directory / "spatial_conflict.om";
	WriteFile(spatial_conflict_path, spatial_conflict_bytes);
	const auto spatial_conflict_decoded = DecodeTreeFile(spatial_conflict_path);
	Require(spatial_conflict_decoded.size() == spatial_conflict_arrays.size(),
	        "official oracle returned the wrong spatial-conflict fixture array count");
	for (const auto &array : spatial_conflict_arrays) {
		const auto variable_path = CanonicalVariablePath(array.segments);
		const auto &decoded = FindDecodedVariable(spatial_conflict_decoded, variable_path);
		Require(decoded.shape == array.fixture.shape && decoded.chunks == array.fixture.chunks,
		        "official oracle returned different spatial-conflict metadata for " + variable_path);
		RequireExactRoundtrip(array.fixture, decoded.values);
		WriteReferenceCsv(output_directory / array.reference_name, decoded.values);
		std::cout << "generated spatial_conflict.om variable=" << variable_path << " rows=" << decoded.values.size()
		          << " oracle=passed sha256=" << Sha256Hex(spatial_conflict_bytes) << '\n';
	}

	auto source_conflict_arrays = BuildSourceConflictArrays();
	GenerateGridTreeFixture(output_directory, "source-conflict.om", source_conflict_arrays);
	const auto source_conflict_decoded = DecodeTreeFile(output_directory / "source-conflict.om");
	Require(source_conflict_decoded.size() == source_conflict_arrays.size(),
	        "official oracle returned the wrong source-conflict fixture array count");

	auto pfor_arrays = BuildPforAttributeArrays();
	const auto pfor_bytes = EncodeTreeFile(pfor_arrays);
	const auto pfor_path = output_directory / "pfor_attributes.om";
	WriteFile(pfor_path, pfor_bytes);
	const auto pfor_decoded = DecodeTreeFile(pfor_path);
	Require(pfor_decoded.size() == pfor_arrays.size(), "official tree oracle returned the wrong PFOR array count");
	for (const auto &array : pfor_arrays) {
		const auto variable_path = CanonicalVariablePath(array.segments);
		const auto &decoded = FindDecodedVariable(pfor_decoded, variable_path);
		Require(decoded.shape == array.fixture.shape && decoded.chunks == array.fixture.chunks,
		        "official tree oracle returned different PFOR metadata for " + variable_path);
		WriteReferenceCsv(output_directory / array.reference_name, decoded.values);
		std::cout << "generated pfor_attributes.om variable=" << variable_path << " rows=" << decoded.values.size()
		          << " oracle=passed sha256=" << Sha256Hex(pfor_bytes) << '\n';
	}

	auto nested_arrays = BuildNestedArrays();
	const auto nested_bytes = EncodeTreeFile(nested_arrays);
	const auto nested_path = output_directory / "nested.om";
	WriteFile(nested_path, nested_bytes);
	const auto nested_decoded = DecodeTreeFile(nested_path);
	Require(nested_decoded.size() == nested_arrays.size(), "official tree oracle returned the wrong nested.om array count");
	for (const auto &array : nested_arrays) {
		const auto variable_path = CanonicalVariablePath(array.segments);
		const auto &decoded = FindDecodedVariable(nested_decoded, variable_path);
		Require(decoded.shape == array.fixture.shape && decoded.chunks == array.fixture.chunks,
		        "official tree oracle returned different metadata for " + variable_path);
		RequireExactRoundtrip(array.fixture, decoded.values);
		WriteReferenceCsv(output_directory / array.reference_name, decoded.values);
		std::cout << "generated nested.om variable=" << variable_path << " rows=" << decoded.values.size()
		          << " oracle=passed sha256=" << Sha256Hex(nested_bytes) << '\n';
	}

	auto projection_arrays = BuildProjectionArrays();
	const auto projection_bytes = EncodeTreeFile(projection_arrays);
	const auto projection_path = output_directory / "projection.om";
	WriteFile(projection_path, projection_bytes);
	const auto projection_decoded = DecodeTreeFile(projection_path);
	Require(projection_decoded.size() == projection_arrays.size(),
	        "official tree oracle returned the wrong projection.om array count");
	for (std::size_t index = 0; index < projection_arrays.size(); index++) {
		const auto &array = projection_arrays[index];
		const auto variable_path = CanonicalVariablePath(array.segments);
		const auto &decoded = FindDecodedVariable(projection_decoded, variable_path);
		Require(decoded.shape == array.fixture.shape && decoded.chunks == array.fixture.chunks,
		        "official tree oracle returned different projection metadata for " + variable_path);
		RequireExactRoundtrip(array.fixture, decoded.values);
		WriteReferenceCsv(output_directory / array.reference_name, decoded.values);
		if (index > 0) {
			Require(projection_arrays[index - 1].lut_offset != array.lut_offset,
			        "projection variables must use independently addressable FPX lookup payloads");
		}
		std::cout << "generated projection.om variable=" << variable_path << " rows=" << decoded.values.size()
		          << " chunks=" << JsonNumberArray(decoded.chunks)
		          << " oracle=passed sha256=" << Sha256Hex(projection_bytes) << '\n';
	}
	const auto projection_rows = CheckedProduct(projection_arrays.front().fixture.shape);
	Require(projection_rows > 2 * 2048,
	        "projection fixture must contain more than two DuckDB STANDARD_VECTOR_SIZE batches");
	Require(projection_arrays.front().fixture.shape[0] != projection_arrays.front().fixture.shape[1],
	        "projection fixture must use a non-square shape");

	const auto grid_output_directory = output_directory / "grids";
	std::filesystem::create_directories(grid_output_directory);
	auto grid_layout_arrays = BuildGridLayoutArrays();
	GenerateGridTreeFixture(output_directory, "grids/layouts.om", grid_layout_arrays);
	std::vector<TreeArray> reverse_layout_arrays{grid_layout_arrays.at(0)};
	std::vector<TreeArray> x_fastest_layout_arrays{grid_layout_arrays.at(1)};
	std::vector<TreeArray> y_fastest_layout_arrays{grid_layout_arrays.at(2)};
	std::vector<TreeArray> interleaved_layout_arrays{grid_layout_arrays.at(3), grid_layout_arrays.at(4)};
	GenerateGridTreeFixture(output_directory, "grids/reverse.om", reverse_layout_arrays);
	GenerateGridTreeFixture(output_directory, "grids/x-fastest.om", x_fastest_layout_arrays);
	GenerateGridTreeFixture(output_directory, "grids/y-fastest.om", y_fastest_layout_arrays);
	GenerateGridTreeFixture(output_directory, "grids/interleaved.om", interleaved_layout_arrays);
	auto grid_unknown_crs_arrays = BuildGridUnknownCrsArrays();
	GenerateGridTreeFixture(output_directory, "grids/crs-conflict.om", grid_unknown_crs_arrays);
	auto grid_gaussian_wgs84_crs_arrays = BuildGridGaussianWgs84CrsArrays();
	GenerateGridTreeFixture(output_directory, "grids/crs-wgs84-gaussian.om", grid_gaussian_wgs84_crs_arrays);
	auto grid_n160_identity_arrays = BuildGridN160IdentityArrays();
	GenerateGridTreeFixture(output_directory, "grids/gaussian-n160-identity.om", grid_n160_identity_arrays);
	auto grid_long_line_arrays = BuildGridLongLineArrays();
	GenerateGridTreeFixture(output_directory, "grids/long-line.om", grid_long_line_arrays);
	auto grid_memory_small_arrays = BuildGridMemoryArrays(false);
	GenerateGridTreeFixture(output_directory, "grids/memory-small.om", grid_memory_small_arrays);
	auto grid_memory_large_arrays = BuildGridMemoryArrays(true);
	GenerateGridTreeFixture(output_directory, "grids/memory-large.om", grid_memory_large_arrays);
	auto grid_gaussian_work_small_arrays = BuildGaussianWorkArrays(false);
	GenerateGridTreeFixture(output_directory, "grids/gaussian-work-small.om", grid_gaussian_work_small_arrays);
	auto grid_gaussian_work_large_arrays = BuildGaussianWorkArrays(true);
	GenerateGridTreeFixture(output_directory, "grids/gaussian-work-large.om", grid_gaussian_work_large_arrays);
	auto grid_spatial_relation_gaussian_arrays = BuildSpatialRelationGaussianArrays();
	GenerateGridTreeFixture(output_directory, "grids/spatial-relations-gaussian.om",
	                        grid_spatial_relation_gaussian_arrays);
	auto grid_large_chunk_arrays = BuildGridLargeChunkArrays();
	GenerateGridTreeFixture(output_directory, "grids/large-value-chunk.om", grid_large_chunk_arrays);
	Require(CheckedProduct(grid_memory_large_arrays.front().fixture.shape) >=
	            10 * CheckedProduct(grid_memory_small_arrays.front().fixture.shape),
	        "grid memory comparison must expand by at least 10x points");
	Require(grid_memory_large_arrays.front().fixture.chunks == grid_memory_small_arrays.front().fixture.chunks,
	        "grid memory comparison must use matching chunk shapes");
	Require(CheckedProduct(grid_gaussian_work_large_arrays.front().fixture.shape) >=
	            10 * CheckedProduct(grid_gaussian_work_small_arrays.front().fixture.shape),
	        "Gaussian work-memory comparison must expand by at least 10x spatial points");
	Require(grid_gaussian_work_large_arrays.front().fixture.chunks ==
	            grid_gaussian_work_small_arrays.front().fixture.chunks,
	        "Gaussian work-memory comparison must use matching chunk shapes");
	WriteGridSampleManifest(output_directory);

	const auto negative_assets = GenerateNegativeAssets(output_directory, multi_path, nested_path);
	WriteManifest(output_directory, fixtures, multi_arrays, multi_decoded, spatial_conflict_arrays,
	              spatial_conflict_decoded, source_conflict_arrays, source_conflict_decoded,
	              pfor_arrays, pfor_decoded,
	              nested_arrays, nested_decoded,
	              projection_arrays, projection_decoded, negative_assets);
}

void PrintUsage(std::ostream &output) {
	output << "Usage:\n"
	       << "  duckomo_fixture_tool --output DIR\n"
	       << "  duckomo_fixture_tool --oracle INPUT.om --csv REFERENCE.csv\n"
	       << "  duckomo_fixture_tool --oracle-prefix INPUT.om --variable /PATH --count N --csv REFERENCE.csv\n"
	       << "\n--output generates raw.om, special.om, raw_large.om, spatial_flat.om, spatial_axes.om, spatial_single.om, spatial_conflict.om, source-conflict.om, multi.om, pfor_attributes.om, nested.om, projection.om, grids/layouts.om and per-layout grid OM files, grids/long-line.om, fixed-chunk projected and Gaussian memory pairs, their oracle CSV files, "
	          "negative mutation assets, and manifest.json.\n"
	       << "--oracle runs the independent fixed official OM reader over a full root array and exports index,value CSV.\n";
}

} // namespace

int main(int argc, char **argv) {
	try {
		std::filesystem::path output_directory;
		std::filesystem::path oracle_input;
		std::filesystem::path oracle_prefix_input;
		std::filesystem::path oracle_csv;
		std::string variable_path;
		std::uint64_t prefix_count = 0;
		for (int index = 1; index < argc; index++) {
			const std::string argument(argv[index]);
			if (argument == "--help" || argument == "-h") {
				PrintUsage(std::cout);
				return 0;
			}
			if (argument == "--output" && index + 1 < argc) {
				output_directory = argv[++index];
				continue;
			}
			if (argument == "--oracle" && index + 1 < argc) {
				oracle_input = argv[++index];
				continue;
			}
			if (argument == "--oracle-prefix" && index + 1 < argc) {
				oracle_prefix_input = argv[++index];
				continue;
			}
			if (argument == "--variable" && index + 1 < argc) {
				variable_path = argv[++index];
				continue;
			}
			if (argument == "--count" && index + 1 < argc) {
				prefix_count = std::stoull(argv[++index]);
				continue;
			}
			if (argument == "--csv" && index + 1 < argc) {
				oracle_csv = argv[++index];
				continue;
			}
			PrintUsage(std::cerr);
			throw std::runtime_error("unknown or incomplete argument: " + argument);
		}

		if (!output_directory.empty() && oracle_input.empty() && oracle_prefix_input.empty() && oracle_csv.empty()) {
			Generate(output_directory);
			return 0;
		}
		if (output_directory.empty() && !oracle_input.empty() && oracle_prefix_input.empty() && !oracle_csv.empty()) {
			const auto decoded = DecodeFullRootArray(oracle_input);
			WriteReferenceCsv(oracle_csv, decoded);
			std::cout << "official OM oracle exported " << decoded.size() << " rows to " << oracle_csv.string() << '\n';
			return 0;
		}
		if (output_directory.empty() && oracle_input.empty() && !oracle_prefix_input.empty() && !oracle_csv.empty() &&
		    !variable_path.empty() && prefix_count != 0) {
			const auto decoded = DecodePrefixFile(oracle_prefix_input, variable_path, prefix_count);
			WriteReferenceCsv(oracle_csv, decoded);
			std::cout << "official OM oracle exported " << decoded.size() << " prefix rows to " << oracle_csv.string() << '\n';
			return 0;
		}
		PrintUsage(std::cerr);
		throw std::runtime_error("choose either --output DIR or both --oracle INPUT.om and --csv REFERENCE.csv");
	} catch (const std::exception &error) {
		std::cerr << "duckomo_fixture_tool: " << error.what() << '\n';
		return 1;
	}
}
