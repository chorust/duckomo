#include "duckomo/metadata.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace duckdb {
namespace duckomo {

namespace {

bool IsValidUtf8Name(const std::string &name) {
	std::size_t index = 0;
	while (index < name.size()) {
		const auto first = static_cast<unsigned char>(name[index]);
		if (first == 0) {
			return false;
		}
		if (first <= 0x7f) {
			index++;
			continue;
		}

		std::size_t continuation_count = 0;
		unsigned char second_min = 0x80;
		unsigned char second_max = 0xbf;
		if (first >= 0xc2 && first <= 0xdf) {
			continuation_count = 1;
		} else if (first >= 0xe0 && first <= 0xef) {
			continuation_count = 2;
			if (first == 0xe0) {
				second_min = 0xa0;
			} else if (first == 0xed) {
				second_max = 0x9f;
			}
		} else if (first >= 0xf0 && first <= 0xf4) {
			continuation_count = 3;
			if (first == 0xf0) {
				second_min = 0x90;
			} else if (first == 0xf4) {
				second_max = 0x8f;
			}
		} else {
			return false;
		}
		if (continuation_count > name.size() - index - 1) {
			return false;
		}
		const auto second = static_cast<unsigned char>(name[index + 1]);
		if (second < second_min || second > second_max) {
			return false;
		}
		for (std::size_t part = 2; part <= continuation_count; part++) {
			const auto next = static_cast<unsigned char>(name[index + part]);
			if (next < 0x80 || next > 0xbf) {
				return false;
			}
		}
		index += continuation_count + 1;
	}
	return true;
}

std::string ReadNodeName(const OmVariable_t *variable, const std::string &path) {
	std::uint16_t length = 0;
	const auto *bytes = om_variable_get_name(variable, &length);
	if (length == 0 || bytes == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM metadata node has an empty name at path '" + (path.empty() ? "/" : path) + "'");
	}
	std::string result(bytes, bytes + length);
	if (!IsValidUtf8Name(result)) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM metadata node name is not valid non-NUL UTF-8 at path '" +
	                      (path.empty() ? "/" : path) + "'");
	}
	return result;
}

std::vector<std::uint64_t> CopyShape(const std::uint64_t *shape, std::size_t rank) {
	return std::vector<std::uint64_t>(shape, shape + rank);
}

void AddArray(const OmVariable_t *variable, std::uint64_t offset, std::uint64_t size,
              const std::shared_ptr<const OwnedMetadataBuffer> &owner, const std::string &path,
              OmMetadataTree &tree) {
	if (om_variable_get_type(variable) != DATA_TYPE_FLOAT_ARRAY) {
		throw ReaderError(ReaderErrorCode::UnsupportedDataType,
		                  "only Float32 OM arrays are supported at '" + path + "'");
	}
	const auto compression = om_variable_get_compression(variable);
	if (compression != COMPRESSION_FPX_XOR2D && compression != COMPRESSION_PFOR_DELTA2D_INT16) {
		throw ReaderError(ReaderErrorCode::UnsupportedCompression,
		                  "only FPX_XOR2D and PFOR_DELTA2D_INT16 OM arrays are supported at '" + path + "'");
	}
	const auto rank = om_variable_get_dimensions_count(variable);
	if (rank == 0 || rank > OM_MAX_RANK) {
		throw ReaderError(ReaderErrorCode::InvalidShape,
		                  "OM Float32 array rank must be between 1 and 8 at '" + path + "'");
	}
	const auto *dimensions = om_variable_get_dimensions(variable);
	const auto *chunks = om_variable_get_chunks(variable);
	if (dimensions == nullptr || chunks == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM Float32 array is missing dimensions or chunk shape at '" + path + "'");
	}
	const auto dimension_count = static_cast<std::size_t>(rank);
	const auto row_count = CheckedShapeProduct(dimensions, dimension_count);
	(void)CheckedShapeProduct(chunks, dimension_count);
	for (std::size_t axis = 0; axis < dimension_count; axis++) {
		if (chunks[axis] > dimensions[axis]) {
			throw ReaderError(ReaderErrorCode::InvalidShape,
			                  "OM chunk shape exceeds an array axis at '" + path + "'");
		}
	}
	if (!owner || owner->Size() != size) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM array metadata ownership is incomplete at '" + path + "'");
	}
	MetadataVariable result;
	result.canonical_path = path.empty() ? "/" : path;
	result.shape = CopyShape(dimensions, dimension_count);
	result.chunk_shape = CopyShape(chunks, dimension_count);
	result.row_count = row_count;
	result.metadata_offset = offset;
	result.metadata_size = size;
	result.metadata_owner = owner;
	tree.arrays.emplace_back(std::move(result));
}

std::vector<std::string> ParseCoordinates(const OmVariable_t *variable) {
	void *value = nullptr;
	std::uint64_t size = 0;
	if (om_variable_get_scalar(variable, &value, &size) != ERROR_OK || value == nullptr || size == 0) {
		return {};
	}
	const std::string coordinates(static_cast<const char *>(value), static_cast<std::size_t>(size));
	if (!IsValidUtf8Name(coordinates)) {
		return {};
	}
	std::istringstream input(coordinates);
	std::vector<std::string> axes;
	std::unordered_set<std::string> seen;
	std::string axis;
	while (input >> axis) {
		if (!seen.emplace(axis).second) {
			return {};
		}
		axes.emplace_back(std::move(axis));
	}
	return axes;
}

void Traverse(const OmV3Reader &reader, std::uint64_t offset, std::uint64_t size, const std::string &parent_path,
              bool root_node, bool array_attribute, std::unordered_set<std::uint64_t> &active_offsets,
              std::unordered_map<std::string, std::vector<std::string>> &coordinates_by_path,
              std::unordered_map<std::string, OmTimeCoordinate> &time_by_path, OmMetadataTree &tree) {
	if (!active_offsets.emplace(offset).second) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "cycle detected in OM metadata references at file offset " + std::to_string(offset));
	}
	struct ActiveOffsetGuard final {
		std::unordered_set<std::uint64_t> &offsets;
		std::uint64_t offset;
		~ActiveOffsetGuard() {
			offsets.erase(offset);
		}
	} guard{active_offsets, offset};

	auto borrowed = root_node ? reader.ReadRootVariable() : reader.ReadVariable(offset, size);
	const auto *variable = borrowed.Get();
	if (variable == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM metadata node is empty at offset " + std::to_string(offset));
	}
	const auto type = om_variable_get_type(variable);
	const auto children_count = om_variable_get_children_count(variable);
	auto node_path = parent_path;
	std::string name;
	if (!root_node) {
		name = ReadNodeName(variable, parent_path);
		if (parent_path.empty()) {
			node_path = "/" + EncodeMetadataName(name);
		} else {
			node_path = parent_path + "/" + EncodeMetadataName(name);
		}
	}

	const auto is_array = type >= DATA_TYPE_INT8_ARRAY && type <= DATA_TYPE_STRING_ARRAY;
	if ((name == "time" && is_array && (array_attribute || type == DATA_TYPE_INT64_ARRAY)) ||
	    (name == "valid_time" && !is_array && type != DATA_TYPE_NONE)) {
		OmTimeCoordinate time;
		time.EnableMemoryAccounting(reader.File().Metrics());
		if (is_array) {
			const auto *shape = om_variable_get_dimensions(variable);
			if (type != DATA_TYPE_INT64_ARRAY || om_variable_get_dimensions_count(variable) != 1 || shape == nullptr) {
				throw ReaderError(ReaderErrorCode::InvalidMetadata,
				                  "OM time coordinate must be a one-dimensional Int64 array");
			}
			const auto count = CheckedShapeProduct(shape, 1);
			if (count > std::numeric_limits<std::size_t>::max() / sizeof(std::int64_t)) {
				throw ReaderError(ReaderErrorCode::Allocation, "OM time coordinate exceeds addressable memory");
			}
			time.epoch_seconds.resize(static_cast<std::size_t>(count));
			time.RefreshMemoryAccount();
			OmDecoderState decoder(borrowed);
			reader.DecodeSelection(decoder, node_path, {0}, {count}, {0}, {count}, time.epoch_seconds.data(),
			                       count * sizeof(std::int64_t), 512, 64 * 1024, ScanDecodePurpose::Coordinate);
		} else {
			void *value = nullptr;
			std::uint64_t value_size = 0;
			if (type != DATA_TYPE_INT64 || om_variable_get_scalar(variable, &value, &value_size) != ERROR_OK ||
			    value == nullptr || value_size != sizeof(std::int64_t)) {
				throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM valid_time must be Int64 UTC Unix seconds");
			}
			std::int64_t seconds;
			std::memcpy(&seconds, value, sizeof(seconds));
			time.epoch_seconds.push_back(seconds);
			time.RefreshMemoryAccount();
			time.scalar = true;
		}
		const auto owner_path = parent_path.empty() ? "/" : parent_path;
		if (!time_by_path.emplace(owner_path, std::move(time)).second) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata, "duplicate time coordinates at '" + owner_path + "'");
		}
	} else if (type == DATA_TYPE_FLOAT_ARRAY && !array_attribute) {
		const auto owner = borrowed.MetadataOwner();
		AddArray(variable, offset, owner->Size(), owner, node_path, tree);
	} else if (is_array && !array_attribute) {
		throw ReaderError(ReaderErrorCode::UnsupportedNode,
		                  "only Float32 value arrays are supported in OM metadata at '" +
	                      (node_path.empty() ? "/" : node_path) + "'");
	} else if (type == DATA_TYPE_STRING && name == "coordinates") {
		const auto axes = ParseCoordinates(variable);
		if (!axes.empty()) {
			const auto owner_path = parent_path.empty() ? "/" : parent_path;
			if (!coordinates_by_path.emplace(owner_path, axes).second) {
				throw ReaderError(ReaderErrorCode::InvalidMetadata,
				                  "duplicate coordinates metadata at '" + owner_path + "'");
			}
		}
	} else if (type == DATA_TYPE_STRING && name == "crs_wkt") {
		void *value = nullptr;
		std::uint64_t value_size = 0;
		if (om_variable_get_scalar(variable, &value, &value_size) == ERROR_OK && value != nullptr && value_size != 0) {
			const std::string wkt(static_cast<const char *>(value), static_cast<std::size_t>(value_size));
			if (!tree.crs_wkt.empty() && tree.crs_wkt != wkt) {
				throw ReaderError(ReaderErrorCode::InvalidMetadata, "conflicting crs_wkt metadata in OM file");
			}
			tree.crs_wkt = wkt;
		}
	}
	if (children_count == 0) {
		return;
	}

	const auto owner = borrowed.MetadataOwner();
	if (!owner || children_count > owner->Size() / (2 * sizeof(std::uint64_t))) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM child references do not fit within metadata at '" +
	                      (node_path.empty() ? "/" : node_path) + "'");
	}
	if (static_cast<std::uint64_t>(children_count) >
	    static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(std::uint64_t))) {
		throw ReaderError(ReaderErrorCode::Allocation, "OM child reference count exceeds addressable memory");
	}
	std::vector<std::uint64_t> child_offsets(children_count);
	std::vector<std::uint64_t> child_sizes(children_count);
	if (!om_variable_get_children(variable, 0, children_count, child_offsets.data(), child_sizes.data())) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "official OM API could not enumerate child metadata at '" +
	                      (node_path.empty() ? "/" : node_path) + "'");
	}
	for (std::uint32_t child = 0; child < children_count; child++) {
		Traverse(reader, child_offsets[child], child_sizes[child], node_path, false,
		         array_attribute || type != DATA_TYPE_NONE, active_offsets, coordinates_by_path, time_by_path, tree);
	}
}

} // namespace

std::string EncodeMetadataName(const std::string &name) {
	if (name.empty() || !IsValidUtf8Name(name)) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "OM metadata path segments must be nonempty, non-NUL UTF-8 strings");
	}
	std::string result;
	result.reserve(name.size());
	for (const auto character : name) {
		if (character == '%') {
			result += "%25";
		} else if (character == '/') {
			result += "%2F";
		} else {
			result += character;
		}
	}
	return result;
}

OmMetadataTree ReadMetadataTree(const OmV3Reader &reader) {
	OmMetadataTree result;
	std::unordered_set<std::uint64_t> active_offsets;
	std::unordered_map<std::string, std::vector<std::string>> coordinates_by_path;
	std::unordered_map<std::string, OmTimeCoordinate> time_by_path;
	Traverse(reader, reader.RootOffset(), reader.RootSize(), "", true, false, active_offsets, coordinates_by_path,
	         time_by_path, result);
	for (auto &array : result.arrays) {
		auto time_owner_path = array.canonical_path;
		while (true) {
			const auto found = time_by_path.find(time_owner_path);
			if (found != time_by_path.end()) {
				array.time = found->second;
				break;
			}
			if (time_owner_path == "/") {
				break;
			}
			const auto separator = time_owner_path.find_last_of('/');
			time_owner_path = separator == 0 ? "/" : time_owner_path.substr(0, separator);
		}
		auto owner_path = array.canonical_path;
		while (true) {
			const auto found = coordinates_by_path.find(owner_path);
			if (found != coordinates_by_path.end()) {
				if (found->second.size() == array.shape.size()) {
					array.inferred_axes = found->second;
				}
				break;
			}
			if (owner_path == "/") {
				break;
			}
			const auto separator = owner_path.find_last_of('/');
			owner_path = separator == 0 ? "/" : owner_path.substr(0, separator);
		}
	}
	if (result.arrays.empty()) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM file contains no supported Float32 arrays");
	}
	return result;
}

} // namespace duckomo
} // namespace duckdb
