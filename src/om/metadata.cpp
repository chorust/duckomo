#include "duckomo/metadata.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <string>
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
	if (om_variable_get_children_count(variable) != 0) {
		throw ReaderError(ReaderErrorCode::UnsupportedNode,
		                  "OM array nodes with children are not supported at '" + path + "'");
	}
	if (om_variable_get_type(variable) != DATA_TYPE_FLOAT_ARRAY) {
		throw ReaderError(ReaderErrorCode::UnsupportedDataType,
		                  "only Float32 OM arrays are supported at '" + path + "'");
	}
	if (om_variable_get_compression(variable) != COMPRESSION_FPX_XOR2D) {
		throw ReaderError(ReaderErrorCode::UnsupportedCompression,
		                  "only FPX_XOR2D OM arrays are supported at '" + path + "'");
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

void Traverse(const OmV3Reader &reader, std::uint64_t offset, std::uint64_t size, const std::string &parent_path,
              bool root_node, std::unordered_set<std::uint64_t> &active_offsets, OmMetadataTree &tree) {
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
	if (!root_node) {
		auto name = ReadNodeName(variable, parent_path);
		if (parent_path.empty()) {
			node_path = "/" + EncodeMetadataName(name);
		} else {
			node_path = parent_path + "/" + EncodeMetadataName(name);
		}
	}

	if (type >= DATA_TYPE_INT8_ARRAY && type <= DATA_TYPE_STRING_ARRAY) {
		const auto owner = borrowed.MetadataOwner();
		AddArray(variable, offset, owner->Size(), owner, node_path, tree);
		return;
	}
	if (type != DATA_TYPE_NONE) {
		throw ReaderError(ReaderErrorCode::UnsupportedNode,
		                  "only NONE containers and Float32 arrays are supported in OM metadata at '" +
	                      (node_path.empty() ? "/" : node_path) + "'");
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
		Traverse(reader, child_offsets[child], child_sizes[child], node_path, false, active_offsets, tree);
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
	Traverse(reader, reader.RootOffset(), reader.RootSize(), "", true, active_offsets, result);
	if (result.arrays.empty()) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM file contains no supported Float32 arrays");
	}
	return result;
}

} // namespace duckomo
} // namespace duckdb
