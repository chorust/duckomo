#pragma once

#include <string>
#include <vector>

#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckomo/semantic_axes.hpp"

namespace duckdb {
class Expression;
class LogicalGet;

namespace duckomo {

enum class AxisComparison : std::uint8_t { Equal, Less, LessEqual, Greater, GreaterEqual };

struct AxisConstraint final {
	idx_t axis_index = 0;
	AxisComparison comparison = AxisComparison::Equal;
	Value constant;
};

struct AxisPredicate final {
	std::vector<AxisConstraint> necessary_conditions;
	std::vector<std::string> fallback_reasons;
	bool has_unsupported_condition = false;
};

AxisPredicate ExtractAxisPredicate(const LogicalGet &get, const vector<unique_ptr<Expression>> &filters,
	                              idx_t output_column_base, const SemanticAxes &semantic_axes);

} // namespace duckomo
} // namespace duckdb
