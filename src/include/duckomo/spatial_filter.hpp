#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "duckdb/common/typedefs.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/vector.hpp"
#include "duckomo/regular_grid.hpp"

namespace duckdb {

class Expression;
class LogicalGet;

namespace duckomo {

enum class SpatialAxis : std::uint8_t { Latitude, Longitude };
enum class SpatialComparison : std::uint8_t { Equal, Less, LessEqual, Greater, GreaterEqual };

struct SpatialConstraint final {
	SpatialAxis axis = SpatialAxis::Latitude;
	SpatialComparison comparison = SpatialComparison::Equal;
	double constant = 0;
};

struct SpatialPredicate final {
	std::vector<SpatialConstraint> necessary_conditions;
	bool residual_filter_retained = true;
	bool has_unsupported_condition = false;
	std::vector<std::string> fallback_reasons;
};

struct AxisRange final {
	std::uint64_t begin = 0;
	std::uint64_t end = 0; // half-open [begin, end)
};

// Copy only safe coordinate constraints from the callback's bound expression
// trees. The expressions themselves remain owned by DuckDB and are never
// rewritten or removed by this helper.
SpatialPredicate ExtractSpatialPredicate(const LogicalGet &get,
	                                    const vector<unique_ptr<Expression>> &filters,
	                                    idx_t coordinate_column_base);

} // namespace duckomo
} // namespace duckdb
