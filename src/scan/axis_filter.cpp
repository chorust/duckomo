#include "duckomo/axis_filter.hpp"

#include <algorithm>
#include <cmath>

#include "duckdb/common/enums/expression_type.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression_iterator.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/semantic_axes.hpp"
#include "duckomo/selection_budget.hpp"

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
	                               const SemanticAxes &semantic_axes, const SemanticAxis *metadata_time_axis) {
	if (ExpressionClassOf(expression) != ExpressionClass::BOUND_COLUMN_REF) return nullptr;
	const auto &column = expression.Cast<BoundColumnRefExpression>();
	const auto binding = BoundColumnBinding(column);
	if (BoundColumnDepth(column) != 0 || binding.table_index != get.table_index ||
	    binding.column_index >= get.GetColumnIds().size()) {
		return nullptr;
	}
	const auto output_column = get.GetColumnIds()[binding.column_index].GetPrimaryIndex();
	idx_t next_column = output_column_base;
	for (const auto &axis : semantic_axes) {
		idx_t axis_column = next_column;
		if (axis.kind == SemanticAxisKind::Time) {
			if (output_column_base == 0) return nullptr;
			axis_column = output_column_base - 1;
		} else {
			next_column++;
		}
		if (output_column == axis_column && ExpressionReturnType(column) == axis.output_type) return &axis;
	}
	if (metadata_time_axis != nullptr && output_column_base > 0 && output_column == output_column_base - 1 &&
	    ExpressionReturnType(column) == metadata_time_axis->output_type) {
		return metadata_time_axis;
	}
	return nullptr;
}

bool ReadConstant(const Expression &expression, const SemanticAxis &axis, Value &constant) {
	if (ExpressionClassOf(expression) != ExpressionClass::BOUND_CONSTANT) return false;
	constant = BoundConstantValue(expression.Cast<BoundConstantExpression>());
	if (constant.IsNull() || constant.type() != axis.output_type) return false;
	if (axis.kind == SemanticAxisKind::Level && !std::isfinite(constant.GetValue<double>())) return false;
	if (axis.kind == SemanticAxisKind::Time || axis.kind == SemanticAxisKind::Run) {
		if (!Value::IsFinite(constant.GetValue<timestamp_t>())) return false;
	}
	if (axis.kind == SemanticAxisKind::LeadTime && constant.GetValue<interval_t>().months != 0) return false;
	return true;
}

bool ContainsAxisReference(const LogicalGet &get, const Expression &expression, idx_t output_column_base,
	                       const SemanticAxes &semantic_axes, const SemanticAxis *metadata_time_axis,
	                       idx_t depth = 0) {
	if (depth > 64) return true;
	if (ReadAxisColumn(get, expression, output_column_base, semantic_axes, metadata_time_axis)) return true;
	bool found = false;
	ExpressionIterator::EnumerateChildren(expression, [&](const Expression &child) {
		if (!found) {
			found = ContainsAxisReference(get, child, output_column_base, semantic_axes, metadata_time_axis, depth + 1);
		}
	});
	return found;
}

struct Collector final {
	const LogicalGet &get;
	idx_t output_column_base;
	const SemanticAxes &semantic_axes;
	const SemanticAxis *metadata_time_axis;
	AxisPredicate result;
	bool copying_disabled = false;

	void Unsupported(const std::string &reason) {
		result.has_unsupported_condition = true;
		if (std::find(result.fallback_reasons.begin(), result.fallback_reasons.end(), reason) ==
	    result.fallback_reasons.end() &&
	    (result.fallback_reasons.size() + 1) * sizeof(std::string) + reason.size() <=
	        MAX_SELECTION_DIAGNOSTIC_BYTES) {
			result.fallback_reasons.push_back(reason);
		}
	}

	void Add(const SemanticAxis &axis, AxisComparison comparison, const Value &constant) {
		if (copying_disabled) return;
		const auto maximum = MAX_SELECTION_PREDICATE_BYTES / sizeof(AxisConstraint);
		if (result.necessary_conditions.size() >= maximum) {
			std::vector<AxisConstraint>().swap(result.necessary_conditions);
			copying_disabled = true;
			Unsupported("axis_predicate_payload_limit");
			return;
		}
		if (axis.kind == SemanticAxisKind::Member && comparison != AxisComparison::Equal) {
			Unsupported("member_supports_equality_only");
			return;
		}
		if (result.necessary_conditions.size() == result.necessary_conditions.capacity()) {
			const auto current = result.necessary_conditions.capacity();
			const auto next = std::min<std::size_t>(maximum, current == 0 ? 1 : current * 2);
			result.necessary_conditions.reserve(next);
		}
		result.necessary_conditions.push_back({axis.axis_index, comparison, constant.Copy()});
	}

	void Visit(const Expression &expression, idx_t depth = 0) {
		if (depth > 64) {
			Unsupported("expression_depth_limit");
			return;
		}
		if (ExpressionClassOf(expression) == ExpressionClass::BOUND_CONJUNCTION &&
	    ExpressionTypeOf(expression) == ExpressionType::CONJUNCTION_AND) {
			for (const auto &child : BoundConjunctionChildren(expression.Cast<BoundConjunctionExpression>()))
				Visit(*child, depth + 1);
			return;
		}
		if (IsBoundComparisonExpression(expression)) {
			AxisComparison comparison;
			if (!ReadComparison(ExpressionTypeOf(expression), comparison)) {
				if (ContainsAxisReference(get, expression, output_column_base, semantic_axes, metadata_time_axis)) {
					Unsupported("unsupported_comparison_operator");
				}
				return;
			}
			const auto *left_axis = ReadAxisColumn(get, BoundComparisonLeft(expression), output_column_base,
			                                       semantic_axes, metadata_time_axis);
			const auto *right_axis = ReadAxisColumn(get, BoundComparisonRight(expression), output_column_base,
			                                        semantic_axes, metadata_time_axis);
			Value constant;
			if (left_axis && ReadConstant(BoundComparisonRight(expression), *left_axis, constant)) {
				Add(*left_axis, comparison, constant);
				return;
			}
			if (right_axis && ReadConstant(BoundComparisonLeft(expression), *right_axis, constant)) {
				Add(*right_axis, Reverse(comparison), constant);
				return;
			}
			if (left_axis || right_axis) Unsupported("unsupported_axis_comparison_operand");
			return;
		}
		if (IsBoundBetweenExpression(expression)) {
			const auto *axis = ReadAxisColumn(get, BoundBetweenInput(expression), output_column_base, semantic_axes,
			                                  metadata_time_axis);
			if (!axis || axis->kind == SemanticAxisKind::Member) {
				if (axis) Unsupported("member_supports_equality_only");
				return;
			}
			Value lower, upper;
			if (!ReadConstant(BoundBetweenLower(expression), *axis, lower) ||
			    !ReadConstant(BoundBetweenUpper(expression), *axis, upper)) {
				Unsupported("unsupported_between_bounds");
				return;
			}
			Add(*axis, BoundBetweenLowerInclusive(expression) ? AxisComparison::GreaterEqual : AxisComparison::Greater,
			    lower);
			Add(*axis, BoundBetweenUpperInclusive(expression) ? AxisComparison::LessEqual : AxisComparison::Less, upper);
			return;
		}
		if (ExpressionClassOf(expression) == ExpressionClass::BOUND_CONJUNCTION) {
			if (ContainsAxisReference(get, expression, output_column_base, semantic_axes, metadata_time_axis)) {
				Unsupported(ExpressionTypeOf(expression) == ExpressionType::CONJUNCTION_OR ? "unsupported_or_expression"
				                                                                      : "unsupported_boolean_expression");
			}
			return;
		}
		if (ContainsAxisReference(get, expression, output_column_base, semantic_axes, metadata_time_axis)) {
			Unsupported("unsupported_filter_expression");
		}
	}
};

} // namespace

AxisPredicate ExtractAxisPredicate(const LogicalGet &get, const vector<unique_ptr<Expression>> &filters,
	                              idx_t output_column_base, const SemanticAxes &semantic_axes,
	                              const SemanticAxis *metadata_time_axis) {
	Collector collector{get, output_column_base, semantic_axes, metadata_time_axis, {}};
	for (const auto &filter : filters) if (filter) collector.Visit(*filter);
	return std::move(collector.result);
}

} // namespace duckomo
} // namespace duckdb
