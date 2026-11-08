#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/metadata.hpp"

namespace duckdb {
namespace duckomo {

struct BoundVariable final {
	std::string canonical_path;
	std::string column_name;
	std::vector<std::string> inferred_axes;
	std::optional<OmTimeCoordinate> time;
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
	SourceCrsProfile crs_profile;
};

struct DeclaredSeriesArray final {
	std::string canonical_path;
	std::string column_name;
	LogicalType type = LogicalType::FLOAT;
	std::vector<std::string> dimension_names;
	std::size_t time_axis_index = 0;
};

struct DeclaredSeriesSchema final {
	std::vector<DeclaredSeriesArray> arrays;
	std::vector<LogicalType> column_types;
	std::vector<std::string> column_names;
	bool include_source = false;
};

// Declaration-only schema for typed empty output. A shared list declares the
// root array; a map declares the complete selected canonical array set.
DeclaredSeriesSchema BuildDeclaredSeriesSchema(const Value *dimensions, bool include_source,
                                               const std::optional<std::string> &variable = std::nullopt);

// Builds the immutable, stable-column schema from every supported array in an
// already validated metadata tree.
BoundSchema BuildBoundSchema(const OmMetadataTree &tree);

// Apply the same naming/type rules to arrays with metadata and declaration-only
// schemas. The latter supports a stable typed empty series result.
std::string ArrayOutputColumnName(const std::string &canonical_path);
void AppendArrayOutputColumn(const std::string &canonical_path, const LogicalType &type,
                             std::vector<LogicalType> &return_types, TableFunctionColumnNames &names);
void AppendValueOutputColumns(const BoundSchema &schema, std::vector<LogicalType> &return_types,
                              TableFunctionColumnNames &names);

// Append the optional synthetic columns after value arrays. These helpers are
// shared by ordinary and series binders so their signatures stay identical.
void AppendSpatialOutputColumns(const BoundSchema &schema, std::vector<LogicalType> &return_types,
	                            TableFunctionColumnNames &names);
void AppendValidTimeOutputColumn(const std::string &function_name,
                                 std::vector<LogicalType> &return_types, TableFunctionColumnNames &names);
LogicalType OmSourceType();
void AppendOmSourceOutputColumn(const BoundSchema &schema, const std::string &function_name,
                                std::vector<LogicalType> &return_types, TableFunctionColumnNames &names);

} // namespace duckomo
} // namespace duckdb
