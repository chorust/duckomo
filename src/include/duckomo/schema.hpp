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
};

// Builds the immutable, stable-column schema from every supported array in an
// already validated metadata tree.
BoundSchema BuildBoundSchema(const OmMetadataTree &tree);

} // namespace duckomo
} // namespace duckdb
