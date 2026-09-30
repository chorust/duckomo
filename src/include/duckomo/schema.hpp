#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "duckdb/common/types.hpp"
#include "duckomo/metadata.hpp"

namespace duckdb {
namespace duckomo {

struct BoundVariable final {
	std::string canonical_path;
	std::string column_name;
	std::vector<std::string> inferred_axes;
	LogicalType type = LogicalType::FLOAT;
	std::vector<std::uint64_t> shape;
	std::vector<std::uint64_t> chunk_shape;
	std::uint64_t row_count = 0;
	std::uint64_t metadata_offset = 0;
	std::uint64_t metadata_size = 0;
	std::shared_ptr<const OwnedMetadataBuffer> metadata_owner;
};

struct BoundSchema final {
	std::vector<BoundVariable> variables;
	std::vector<std::uint64_t> shape;
	std::uint64_t row_count = 0;
	std::string crs_wkt;
};

// Builds the immutable, stable-column schema from every supported array in an
// already validated metadata tree.
BoundSchema BuildBoundSchema(const OmMetadataTree &tree);

// Append the optional, synthetic coordinate columns after every value array.
// Name conflicts follow DuckDB's case-insensitive identifier rules.
void AppendSpatialOutputColumns(const BoundSchema &schema, std::vector<LogicalType> &return_types,
	                            std::vector<std::string> &names);

} // namespace duckomo
} // namespace duckdb
