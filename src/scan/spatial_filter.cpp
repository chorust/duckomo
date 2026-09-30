#include "duckomo/spatial_filter.hpp"

#include <cmath>
#include <string>

#include "duckdb/common/enums/expression_type.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/planner/expression/bound_cast_expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/operator/logical_get.hpp"

namespace duckdb {
namespace duckomo {
namespace {

bool ReadFiniteNumericConstant(const Expression &expression, double &value, idx_t depth = 0) {
	if (depth > 16) {
		return false;
	}
	if (expression.expression_class == ExpressionClass::BOUND_CONSTANT) {
		const auto &constant = expression.Cast<BoundConstantExpression>().value;
		if (constant.IsNull() || !constant.type().IsNumeric()) {
			return false;
		}
	try {
			value = constant.DefaultCastAs(LogicalType::DOUBLE, true).GetValue<double>();
	} catch (const std::exception &) {
		return false;
	}
	return std::isfinite(value);
	}
	if (expression.expression_class == ExpressionClass::BOUND_CAST) {
		const auto &cast = expression.Cast<BoundCastExpression>();
		if (cast.return_type.id() != LogicalTypeId::DOUBLE || !cast.child || cast.child->HasParameter()) {
			return false;
		}
		return ReadFiniteNumericConstant(*cast.child, value, depth + 1);
	}
	return false;
}

bool ReadSpatialColumn(const LogicalGet &get, const Expression &expression, idx_t coordinate_column_base,
	                   SpatialAxis &axis) {
	if (expression.expression_class != ExpressionClass::BOUND_COLUMN_REF) {
		return false;
	}
	const auto &column = expression.Cast<BoundColumnRefExpression>();
	if (column.depth != 0 || column.binding.table_index != get.table_index ||
	    column.binding.column_index >= get.GetColumnIds().size()) {
		return false;
	}
	// LogicalGet bindings use positions in GetColumnIds(), which can be a
	// compact projection rather than the table function's original output ids.
	// Resolve through the current get before comparing against coordinate slots.
	const auto output_column = get.GetColumnIds()[column.binding.column_index].GetPrimaryIndex();
	if (output_column == coordinate_column_base) {
		axis = SpatialAxis::Latitude;
		return true;
	}
	if (output_column == coordinate_column_base + 1) {
		axis = SpatialAxis::Longitude;
		return true;
	}
	return false;
}

bool ReadComparison(ExpressionType type, SpatialComparison &comparison) {
	switch (type) {
	case ExpressionType::COMPARE_EQUAL:
		comparison = SpatialComparison::Equal;
		return true;
	case ExpressionType::COMPARE_LESSTHAN:
		comparison = SpatialComparison::Less;
		return true;
	case ExpressionType::COMPARE_LESSTHANOREQUALTO:
		comparison = SpatialComparison::LessEqual;
		return true;
	case ExpressionType::COMPARE_GREATERTHAN:
		comparison = SpatialComparison::Greater;
		return true;
	case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
		comparison = SpatialComparison::GreaterEqual;
		return true;
	default:
		return false;
	}
}

SpatialComparison ReverseComparison(SpatialComparison comparison) {
	switch (comparison) {
	case SpatialComparison::Less:
		return SpatialComparison::Greater;
	case SpatialComparison::LessEqual:
		return SpatialComparison::GreaterEqual;
	case SpatialComparison::Greater:
		return SpatialComparison::Less;
	case SpatialComparison::GreaterEqual:
		return SpatialComparison::LessEqual;
	case SpatialComparison::Equal:
		return SpatialComparison::Equal;
	}
	return comparison;
}

struct PredicateCollector final {
	const LogicalGet &get;
	idx_t coordinate_column_base;
	SpatialPredicate result;

	void Unsupported(std::string reason) {
		result.has_unsupported_condition = true;
		result.fallback_reasons.emplace_back(std::move(reason));
	}

	void Visit(const Expression &expression, idx_t depth = 0) {
		if (depth > 64) {
			Unsupported("expression_depth_limit");
			return;
		}
		if (expression.expression_class == ExpressionClass::BOUND_CONJUNCTION &&
		    expression.type == ExpressionType::CONJUNCTION_AND) {
			const auto &conjunction = expression.Cast<BoundConjunctionExpression>();
			for (const auto &child : conjunction.children) {
				Visit(*child, depth + 1);
			}
			return;
		}
		if (expression.expression_class == ExpressionClass::BOUND_COMPARISON) {
			const auto &comparison_expression = expression.Cast<BoundComparisonExpression>();
			SpatialComparison comparison;
			if (!ReadComparison(expression.type, comparison)) {
				Unsupported("unsupported_comparison_operator");
				return;
			}
			SpatialAxis axis;
			double constant = 0;
			if (ReadSpatialColumn(get, *comparison_expression.left, coordinate_column_base, axis) &&
			    ReadFiniteNumericConstant(*comparison_expression.right, constant)) {
				result.necessary_conditions.push_back({axis, comparison, constant});
				return;
			}
			if (ReadSpatialColumn(get, *comparison_expression.right, coordinate_column_base, axis) &&
			    ReadFiniteNumericConstant(*comparison_expression.left, constant)) {
				result.necessary_conditions.push_back({axis, ReverseComparison(comparison), constant});
				return;
			}
			Unsupported("unsupported_comparison_operand");
			return;
		}
		if (expression.expression_class == ExpressionClass::BOUND_BETWEEN) {
			const auto &between = expression.Cast<BoundBetweenExpression>();
			SpatialAxis axis;
			double lower = 0;
			double upper = 0;
			if (!ReadSpatialColumn(get, *between.input, coordinate_column_base, axis)) {
				Unsupported("unsupported_between_coordinate_binding");
				return;
			}
			if (!ReadFiniteNumericConstant(*between.lower, lower)) {
				Unsupported("unsupported_between_lower_bound");
				return;
			}
			if (!ReadFiniteNumericConstant(*between.upper, upper)) {
				Unsupported("unsupported_between_upper_bound");
				return;
			}
			result.necessary_conditions.push_back(
			    {axis, between.lower_inclusive ? SpatialComparison::GreaterEqual : SpatialComparison::Greater, lower});
			result.necessary_conditions.push_back(
			    {axis, between.upper_inclusive ? SpatialComparison::LessEqual : SpatialComparison::Less, upper});
			return;
		}
		if (expression.expression_class == ExpressionClass::BOUND_CONJUNCTION) {
			Unsupported(expression.type == ExpressionType::CONJUNCTION_OR ? "unsupported_or_expression"
			                                                           : "unsupported_boolean_expression");
			return;
		}
		Unsupported("unsupported_filter_expression");
	}
};

} // namespace

SpatialPredicate ExtractSpatialPredicate(const LogicalGet &get,
	                                    const vector<unique_ptr<Expression>> &filters,
	                                    idx_t coordinate_column_base) {
	PredicateCollector collector{get, coordinate_column_base, {}};
	for (const auto &filter : filters) {
		if (filter) {
			collector.Visit(*filter);
		}
	}
	return std::move(collector.result);
}

} // namespace duckomo
} // namespace duckdb
