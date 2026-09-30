#include "duckomo/axis_filter.hpp"

#include <algorithm>
#include <cmath>

#include "duckdb/common/enums/expression_type.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckomo/semantic_axes.hpp"

namespace duckdb {
namespace duckomo {
namespace {

bool ReadComparison(ExpressionType type, AxisComparison &comparison) {
	switch (type) {
	case ExpressionType::COMPARE_EQUAL: comparison = AxisComparison::Equal; return true;
	case ExpressionType::COMPARE_LESSTHAN: comparison = AxisComparison::Less; return true;
	case ExpressionType::COMPARE_LESSTHANOREQUALTO: comparison = AxisComparison::LessEqual; return true;
	case ExpressionType::COMPARE_GREATERTHAN: comparison = AxisComparison::Greater; return true;
	case ExpressionType::COMPARE_GREATERTHANOREQUALTO: comparison = AxisComparison::GreaterEqual; return true;
	default: return false;
	}
}

AxisComparison Reverse(AxisComparison comparison) {
	switch (comparison) {
	case AxisComparison::Less: return AxisComparison::Greater;
	case AxisComparison::LessEqual: return AxisComparison::GreaterEqual;
	case AxisComparison::Greater: return AxisComparison::Less;
	case AxisComparison::GreaterEqual: return AxisComparison::LessEqual;
	case AxisComparison::Equal: return AxisComparison::Equal;
	}
	return comparison;
}

const SemanticAxis *ReadAxisColumn(const LogicalGet &get, const Expression &expression, idx_t output_column_base,
	                               const SemanticAxes &semantic_axes) {
	if (expression.expression_class != ExpressionClass::BOUND_COLUMN_REF) return nullptr;
	const auto &column = expression.Cast<BoundColumnRefExpression>();
	if (column.depth != 0 || column.binding.table_index != get.table_index ||
	    column.binding.column_index >= get.GetColumnIds().size()) {
		return nullptr;
	}
	const auto output_column = get.GetColumnIds()[column.binding.column_index].GetPrimaryIndex();
	idx_t next_column = output_column_base;
	for (const auto &axis : semantic_axes) {
		idx_t axis_column = next_column;
		if (axis.kind == SemanticAxisKind::Time) {
			if (output_column_base == 0) return nullptr;
			axis_column = output_column_base - 1;
		} else {
			next_column++;
		}
		if (output_column == axis_column && column.return_type == axis.output_type) return &axis;
	}
	return nullptr;
}

bool ReadConstant(const Expression &expression, const SemanticAxis &axis, Value &constant) {
	if (expression.expression_class != ExpressionClass::BOUND_CONSTANT) return false;
	constant = expression.Cast<BoundConstantExpression>().value;
	if (constant.IsNull() || constant.type() != axis.output_type) return false;
	if (axis.kind == SemanticAxisKind::Level && !std::isfinite(constant.GetValue<double>())) return false;
	if (axis.kind == SemanticAxisKind::Time || axis.kind == SemanticAxisKind::Run) {
		if (!Timestamp::IsFinite(constant.GetValue<timestamp_t>())) return false;
	}
	if (axis.kind == SemanticAxisKind::LeadTime && constant.GetValue<interval_t>().months != 0) return false;
	return true;
}

struct Collector final {
	const LogicalGet &get;
	idx_t output_column_base;
	const SemanticAxes &semantic_axes;
	AxisPredicate result;

	void Unsupported(const std::string &reason) {
		result.has_unsupported_condition = true;
		result.fallback_reasons.push_back(reason);
	}

	void Add(const SemanticAxis &axis, AxisComparison comparison, const Value &constant) {
		if (axis.kind == SemanticAxisKind::Member && comparison != AxisComparison::Equal) {
			Unsupported("member_supports_equality_only");
			return;
		}
		result.necessary_conditions.push_back({axis.axis_index, comparison, constant.Copy()});
	}

	void Visit(const Expression &expression, idx_t depth = 0) {
		if (depth > 64) {
			Unsupported("expression_depth_limit");
			return;
		}
		if (expression.expression_class == ExpressionClass::BOUND_CONJUNCTION &&
		    expression.type == ExpressionType::CONJUNCTION_AND) {
			for (const auto &child : expression.Cast<BoundConjunctionExpression>().children) Visit(*child, depth + 1);
			return;
		}
		if (expression.expression_class == ExpressionClass::BOUND_COMPARISON) {
			const auto &comparison_expression = expression.Cast<BoundComparisonExpression>();
			AxisComparison comparison;
			if (!ReadComparison(expression.type, comparison)) {
				Unsupported("unsupported_comparison_operator");
				return;
			}
			const auto *left_axis = ReadAxisColumn(get, *comparison_expression.left, output_column_base, semantic_axes);
			const auto *right_axis = ReadAxisColumn(get, *comparison_expression.right, output_column_base, semantic_axes);
			Value constant;
			if (left_axis && ReadConstant(*comparison_expression.right, *left_axis, constant)) {
				Add(*left_axis, comparison, constant);
				return;
			}
			if (right_axis && ReadConstant(*comparison_expression.left, *right_axis, constant)) {
				Add(*right_axis, Reverse(comparison), constant);
				return;
			}
			if (left_axis || right_axis) Unsupported("unsupported_axis_comparison_operand");
			return;
		}
		if (expression.expression_class == ExpressionClass::BOUND_BETWEEN) {
			const auto &between = expression.Cast<BoundBetweenExpression>();
			const auto *axis = ReadAxisColumn(get, *between.input, output_column_base, semantic_axes);
			if (!axis || axis->kind == SemanticAxisKind::Member) {
				if (axis) Unsupported("member_supports_equality_only");
				return;
			}
			Value lower, upper;
			if (!ReadConstant(*between.lower, *axis, lower) || !ReadConstant(*between.upper, *axis, upper)) {
				Unsupported("unsupported_between_bounds");
				return;
			}
			Add(*axis, between.lower_inclusive ? AxisComparison::GreaterEqual : AxisComparison::Greater, lower);
			Add(*axis, between.upper_inclusive ? AxisComparison::LessEqual : AxisComparison::Less, upper);
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

AxisPredicate ExtractAxisPredicate(const LogicalGet &get, const vector<unique_ptr<Expression>> &filters,
	                              idx_t output_column_base, const SemanticAxes &semantic_axes) {
	Collector collector{get, output_column_base, semantic_axes, {}};
	for (const auto &filter : filters) if (filter) collector.Visit(*filter);
	return std::move(collector.result);
}

} // namespace duckomo
} // namespace duckdb
