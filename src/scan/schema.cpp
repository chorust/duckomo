#include "duckomo/schema.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "duckdb/common/string_util.hpp"

namespace duckdb {
namespace duckomo {

namespace {

bool Utf8ByteLess(const std::string &left, const std::string &right) {
	return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
	                                    [](char left_byte, char right_byte) {
		                                    return static_cast<unsigned char>(left_byte) <
		                                           static_cast<unsigned char>(right_byte);
	                                    });
}

} // namespace

BoundSchema BuildBoundSchema(const OmMetadataTree &tree) {
	if (tree.arrays.empty()) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM file contains no supported Float32 arrays");
	}

	auto arrays = tree.arrays;
	std::sort(arrays.begin(), arrays.end(), [](const MetadataVariable &left, const MetadataVariable &right) {
		return Utf8ByteLess(left.canonical_path, right.canonical_path);
	});

	BoundSchema result;
	result.variables.reserve(arrays.size());
	std::vector<std::string> seen_names;
	seen_names.reserve(arrays.size());
	for (const auto &array : arrays) {
		if (array.canonical_path.empty() || array.shape.empty() || array.shape.size() > OM_MAX_RANK ||
		    array.shape.size() != array.chunk_shape.size()) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM metadata contains an incomplete array descriptor at '" + array.canonical_path + "'");
		}
		const auto row_count = CheckedShapeProduct(array.shape);
		if (row_count != array.row_count) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM array row count changed while building schema at '" + array.canonical_path + "'");
		}

		BoundVariable variable;
		variable.canonical_path = array.canonical_path;
		variable.column_name = array.canonical_path == "/" ? "value" : array.canonical_path;
		variable.shape = array.shape;
		variable.chunk_shape = array.chunk_shape;
		variable.row_count = row_count;
		variable.metadata_offset = array.metadata_offset;
		variable.metadata_size = array.metadata_size;
		variable.metadata_owner = array.metadata_owner;

		for (const auto &existing_name : seen_names) {
			if (StringUtil::CIEquals(existing_name, variable.column_name)) {
				throw ReaderError(ReaderErrorCode::InvalidMetadata,
				                  "OM metadata paths produce duplicate DuckDB column name '" + variable.column_name +
				                      "' (conflicts with '" + existing_name + "')");
			}
		}
		seen_names.emplace_back(variable.column_name);
		result.variables.emplace_back(std::move(variable));
	}
	result.shape = result.variables.front().shape;
	result.row_count = result.variables.front().row_count;
	return result;
}

} // namespace duckomo
} // namespace duckdb
