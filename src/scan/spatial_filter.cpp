#include "duckomo/spatial_filter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

#include "duckdb/common/enums/expression_type.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/planner/expression/bound_cast_expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/selection_budget.hpp"

namespace duckdb {
namespace duckomo {
namespace {

struct NumericInterval final {
	double lower = -std::numeric_limits<double>::infinity();
	double upper = std::numeric_limits<double>::infinity();
	bool lower_inclusive = true;
	bool upper_inclusive = true;

	void AddLower(double value, bool inclusive) noexcept {
		if (value > lower) {
			lower = value;
			lower_inclusive = inclusive;
		} else if (value == lower && !inclusive) {
			lower_inclusive = false;
		}
	}

	void AddUpper(double value, bool inclusive) noexcept {
		if (value < upper) {
			upper = value;
			upper_inclusive = inclusive;
		} else if (value == upper && !inclusive) {
			upper_inclusive = false;
		}
	}

	bool Empty() const noexcept {
		return lower > upper || (lower == upper && (!lower_inclusive || !upper_inclusive));
	}
};

void AddConstraint(NumericInterval &interval, SpatialComparison comparison, double value) noexcept {
	switch (comparison) {
	case SpatialComparison::Equal:
		interval.AddLower(value, true);
		interval.AddUpper(value, true);
		break;
	case SpatialComparison::Less:
		interval.AddUpper(value, false);
		break;
	case SpatialComparison::LessEqual:
		interval.AddUpper(value, true);
		break;
	case SpatialComparison::Greater:
		interval.AddLower(value, false);
		break;
	case SpatialComparison::GreaterEqual:
		interval.AddLower(value, true);
		break;
	}
}

bool ReadFiniteNumericConstant(const Expression &expression, double &value, idx_t depth = 0) {
	if (depth > 16) {
		return false;
	}
	if (ExpressionClassOf(expression) == ExpressionClass::BOUND_CONSTANT) {
		const auto &constant = BoundConstantValue(expression.Cast<BoundConstantExpression>());
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
	if (IsBoundCastExpression(expression)) {
		const auto &child = BoundCastChild(expression);
		if (ExpressionReturnType(expression).id() != LogicalTypeId::DOUBLE || child.HasParameter()) {
			return false;
		}
		return ReadFiniteNumericConstant(child, value, depth + 1);
	}
	return false;
}

bool ReadSpatialColumn(const LogicalGet &get, const Expression &expression, idx_t coordinate_column_base,
	                   SpatialAxis &axis) {
	if (ExpressionClassOf(expression) != ExpressionClass::BOUND_COLUMN_REF) {
		return false;
	}
	const auto &column = expression.Cast<BoundColumnRefExpression>();
	const auto binding = BoundColumnBinding(column);
	if (BoundColumnDepth(column) != 0 || binding.table_index != get.table_index ||
	    binding.column_index >= get.GetColumnIds().size()) {
		return false;
	}
	// LogicalGet bindings use positions in GetColumnIds(), which can be a
	// compact projection rather than the table function's original output ids.
	// Resolve through the current get before comparing against coordinate slots.
	const auto output_column = get.GetColumnIds()[binding.column_index].GetPrimaryIndex();
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

struct IntervalAccumulator final {
	bool has_lower = false;
	bool has_upper = false;
	SpatialInterval interval;
	std::uint64_t conditions = 0;

	void AddLower(double value, bool inclusive) {
		if (!has_lower || value > interval.lower) {
			interval.lower = value;
			interval.lower_inclusive = inclusive;
			has_lower = true;
		} else if (value == interval.lower && !inclusive) {
			interval.lower_inclusive = false;
		}
		conditions++;
	}

	void AddUpper(double value, bool inclusive) {
		if (!has_upper || value < interval.upper) {
			interval.upper = value;
			interval.upper_inclusive = inclusive;
			has_upper = true;
		} else if (value == interval.upper && !inclusive) {
			interval.upper_inclusive = false;
		}
		conditions++;
	}

	void Add(SpatialAxis axis, SpatialComparison comparison, double value) {
		if (axis != SpatialAxis::Longitude) {
			conditions = std::numeric_limits<std::uint64_t>::max();
			return;
		}
		switch (comparison) {
		case SpatialComparison::Equal:
			AddLower(value, true);
			AddUpper(value, true);
			break;
		case SpatialComparison::Greater:
			AddLower(value, false);
			break;
		case SpatialComparison::GreaterEqual:
			AddLower(value, true);
			break;
		case SpatialComparison::Less:
			AddUpper(value, false);
			break;
		case SpatialComparison::LessEqual:
			AddUpper(value, true);
			break;
		}
	}
};

bool CollectLongitudeInterval(const Expression &expression, const LogicalGet &get, idx_t coordinate_column_base,
	                          IntervalAccumulator &interval, idx_t depth = 0) {
	if (depth > 16) return false;
	if (ExpressionClassOf(expression) == ExpressionClass::BOUND_CONJUNCTION &&
	    ExpressionTypeOf(expression) == ExpressionType::CONJUNCTION_AND) {
		const auto &conjunction = expression.Cast<BoundConjunctionExpression>();
		const auto &children = BoundConjunctionChildren(conjunction);
		if (children.empty()) return false;
		for (const auto &child : children) {
			if (!CollectLongitudeInterval(*child, get, coordinate_column_base, interval, depth + 1)) return false;
		}
		return true;
	}
	if (IsBoundComparisonExpression(expression)) {
		SpatialComparison comparison;
		SpatialAxis axis;
		double constant = 0;
		if (!ReadComparison(ExpressionTypeOf(expression), comparison)) return false;
		if (ReadSpatialColumn(get, BoundComparisonLeft(expression), coordinate_column_base, axis) &&
		    ReadFiniteNumericConstant(BoundComparisonRight(expression), constant)) {
			interval.Add(axis, comparison, constant);
			return axis == SpatialAxis::Longitude;
		}
		if (ReadSpatialColumn(get, BoundComparisonRight(expression), coordinate_column_base, axis) &&
		    ReadFiniteNumericConstant(BoundComparisonLeft(expression), constant)) {
			interval.Add(axis, ReverseComparison(comparison), constant);
			return axis == SpatialAxis::Longitude;
		}
		return false;
	}
	if (IsBoundBetweenExpression(expression)) {
		SpatialAxis axis;
		double lower = 0;
		double upper = 0;
		if (!ReadSpatialColumn(get, BoundBetweenInput(expression), coordinate_column_base, axis) ||
		    axis != SpatialAxis::Longitude || !ReadFiniteNumericConstant(BoundBetweenLower(expression), lower) ||
		    !ReadFiniteNumericConstant(BoundBetweenUpper(expression), upper)) {
			return false;
		}
		interval.Add(axis, BoundBetweenLowerInclusive(expression) ? SpatialComparison::GreaterEqual
		                                                         : SpatialComparison::Greater,
		             lower);
		interval.Add(axis, BoundBetweenUpperInclusive(expression) ? SpatialComparison::LessEqual
		                                                         : SpatialComparison::Less,
		             upper);
		return true;
	}
	return false;
}

bool ReadCompleteLongitudeInterval(const Expression &expression, const LogicalGet &get, idx_t coordinate_column_base,
	                               SpatialInterval &output) {
	IntervalAccumulator interval;
	if (!CollectLongitudeInterval(expression, get, coordinate_column_base, interval) || interval.conditions == 0 ||
	    interval.conditions == std::numeric_limits<std::uint64_t>::max()) {
		return false;
	}
	output = interval.interval;
	// Geographic longitudes are normalized to the half-open domain [-180, 180).
	// A one-sided predicate is therefore a complete interval when closed at the
	// corresponding normalized-domain edge.
	if (!interval.has_lower) {
		output.lower = -180.0;
		output.lower_inclusive = true;
	}
	if (!interval.has_upper) {
		output.upper = 180.0;
		output.upper_inclusive = false;
	}
	return true;
}

struct PredicateCollector final {
	const LogicalGet &get;
	idx_t coordinate_column_base;
	SpatialPredicate result;
	bool copying_disabled = false;

	void Unsupported(std::string reason) {
		result.has_unsupported_condition = true;
		if (std::find(result.fallback_reasons.begin(), result.fallback_reasons.end(), reason) ==
	        result.fallback_reasons.end() &&
	        (result.fallback_reasons.size() + 1) * sizeof(std::string) + reason.size() <=
	            MAX_SELECTION_DIAGNOSTIC_BYTES) {
			result.fallback_reasons.emplace_back(std::move(reason));
		}
	}

	void Add(SpatialAxis axis, SpatialComparison comparison, double constant) {
		if (copying_disabled) return;
		const auto maximum = MAX_SELECTION_PREDICATE_BYTES / sizeof(SpatialConstraint);
		if (result.necessary_conditions.size() >= maximum) {
			std::vector<SpatialConstraint>().swap(result.necessary_conditions);
			copying_disabled = true;
			Unsupported("spatial_predicate_payload_limit");
			return;
		}
		if (result.necessary_conditions.size() == result.necessary_conditions.capacity()) {
			const auto current = result.necessary_conditions.capacity();
			const auto next = std::min<std::size_t>(maximum, current == 0 ? 1 : current * 2);
			result.necessary_conditions.reserve(next);
		}
		result.necessary_conditions.push_back({axis, comparison, constant});
	}

	void Visit(const Expression &expression, idx_t depth = 0) {
		if (depth > 64) {
			Unsupported("expression_depth_limit");
			return;
		}
		if (ExpressionClassOf(expression) == ExpressionClass::BOUND_CONJUNCTION &&
		    ExpressionTypeOf(expression) == ExpressionType::CONJUNCTION_AND) {
			const auto &conjunction = expression.Cast<BoundConjunctionExpression>();
			for (const auto &child : BoundConjunctionChildren(conjunction)) {
				Visit(*child, depth + 1);
			}
			return;
		}
		if (ExpressionClassOf(expression) == ExpressionClass::BOUND_CONJUNCTION &&
		    ExpressionTypeOf(expression) == ExpressionType::CONJUNCTION_OR) {
			const auto &conjunction = expression.Cast<BoundConjunctionExpression>();
			const auto &children = BoundConjunctionChildren(conjunction);
			if (children.size() != 2 || !result.longitude_union.empty() ||
			    2 * sizeof(SpatialInterval) > MAX_SELECTION_PREDICATE_BYTES) {
				Unsupported("unsupported_or_expression");
				return;
			}
			std::vector<SpatialInterval> intervals;
			intervals.reserve(2);
			for (const auto &child : children) {
				SpatialInterval interval;
				if (!ReadCompleteLongitudeInterval(*child, get, coordinate_column_base, interval)) {
					Unsupported("unsafe_or_expression");
					return;
				}
				intervals.push_back(interval);
			}
			result.longitude_union = std::move(intervals);
			return;
		}
		if (IsBoundComparisonExpression(expression)) {
			SpatialComparison comparison;
			if (!ReadComparison(ExpressionTypeOf(expression), comparison)) {
				Unsupported("unsupported_comparison_operator");
				return;
			}
			SpatialAxis axis;
			double constant = 0;
			if (ReadSpatialColumn(get, BoundComparisonLeft(expression), coordinate_column_base, axis) &&
			    ReadFiniteNumericConstant(BoundComparisonRight(expression), constant)) {
				Add(axis, comparison, constant);
				return;
			}
			if (ReadSpatialColumn(get, BoundComparisonRight(expression), coordinate_column_base, axis) &&
			    ReadFiniteNumericConstant(BoundComparisonLeft(expression), constant)) {
				Add(axis, ReverseComparison(comparison), constant);
				return;
			}
			Unsupported("unsupported_comparison_operand");
			return;
		}
		if (IsBoundBetweenExpression(expression)) {
			SpatialAxis axis;
			double lower = 0;
			double upper = 0;
			if (!ReadSpatialColumn(get, BoundBetweenInput(expression), coordinate_column_base, axis)) {
				Unsupported("unsupported_between_coordinate_binding");
				return;
			}
			if (!ReadFiniteNumericConstant(BoundBetweenLower(expression), lower)) {
				Unsupported("unsupported_between_lower_bound");
				return;
			}
			if (!ReadFiniteNumericConstant(BoundBetweenUpper(expression), upper)) {
				Unsupported("unsupported_between_upper_bound");
				return;
			}
			Add(axis, BoundBetweenLowerInclusive(expression) ? SpatialComparison::GreaterEqual
			                                                 : SpatialComparison::Greater,
			    lower);
			Add(axis, BoundBetweenUpperInclusive(expression) ? SpatialComparison::LessEqual
			                                                 : SpatialComparison::Less,
			    upper);
			return;
		}
		if (ExpressionClassOf(expression) == ExpressionClass::BOUND_CONJUNCTION) {
			Unsupported(ExpressionTypeOf(expression) == ExpressionType::CONJUNCTION_OR ? "unsupported_or_expression"
			                                                                       : "unsupported_boolean_expression");
			return;
		}
		Unsupported("unsupported_filter_expression");
	}
};

} // namespace

bool SpatialPredicateProvesEmpty(const SpatialPredicate &predicate) noexcept {
	NumericInterval latitude;
	NumericInterval longitude;
	// Every supported grid emits normalized longitude in [-180, 180). This is
	// a safe domain bound even when the predicate itself is one-sided.
	longitude.AddLower(-180.0, true);
	longitude.AddUpper(180.0, false);
	for (const auto &constraint : predicate.necessary_conditions) {
		if (!std::isfinite(constraint.constant)) return false;
		AddConstraint(constraint.axis == SpatialAxis::Latitude ? latitude : longitude, constraint.comparison,
		              constraint.constant);
	}
	if (latitude.Empty() || longitude.Empty()) return true;
	if (predicate.longitude_union.empty()) return false;
	for (const auto &branch : predicate.longitude_union) {
		auto branch_interval = longitude;
		branch_interval.AddLower(branch.lower, branch.lower_inclusive);
		branch_interval.AddUpper(branch.upper, branch.upper_inclusive);
		if (!branch_interval.Empty()) return false;
	}
	return true;
}

bool SpatialPredicateCoversGeographicDomain(const SpatialPredicate &predicate) noexcept {
	NumericInterval latitude;
	latitude.AddLower(-90.0, true);
	latitude.AddUpper(90.0, true);
	NumericInterval longitude;
	longitude.AddLower(-180.0, true);
	longitude.AddUpper(180.0, false);
	for (const auto &constraint : predicate.necessary_conditions) {
		if (!std::isfinite(constraint.constant)) return false;
		AddConstraint(constraint.axis == SpatialAxis::Latitude ? latitude : longitude, constraint.comparison,
		              constraint.constant);
	}
	const auto covers = [](const NumericInterval &interval, double domain_lower, bool domain_lower_inclusive,
	                       double domain_upper, bool domain_upper_inclusive) {
		const bool covers_lower = interval.lower < domain_lower ||
		                          (interval.lower == domain_lower &&
		                           (!domain_lower_inclusive || interval.lower_inclusive));
		const bool covers_upper = interval.upper > domain_upper ||
		                          (interval.upper == domain_upper &&
		                           (!domain_upper_inclusive || interval.upper_inclusive));
		return covers_lower && covers_upper;
	};
	if (!covers(latitude, -90.0, true, 90.0, true) ||
	    !covers(longitude, -180.0, true, 180.0, false)) {
		return false;
	}
	if (predicate.longitude_union.empty()) return true;
	if (predicate.longitude_union.size() > 2) return false;
	std::array<SpatialInterval, 2> intervals{};
	for (std::size_t index = 0; index < predicate.longitude_union.size(); index++) {
		intervals[index] = predicate.longitude_union[index];
		if (!std::isfinite(intervals[index].lower) || !std::isfinite(intervals[index].upper)) return false;
	}
	if (predicate.longitude_union.size() == 2 && intervals[1].lower < intervals[0].lower) {
		std::swap(intervals[0], intervals[1]);
	}
	const auto &first = intervals[0];
	if (first.lower > -180.0 || (first.lower == -180.0 && !first.lower_inclusive) || first.upper < -180.0) {
		return false;
	}
	double covered_upper = std::min(180.0, first.upper);
	bool covered_upper_inclusive = first.upper < 180.0 && first.upper_inclusive;
	if (covered_upper >= 180.0) return true; // 180 is outside the longitude domain.
	for (std::size_t index = 1; index < predicate.longitude_union.size(); index++) {
		const auto &next = intervals[index];
		if (next.upper <= covered_upper || next.lower > 180.0) continue;
		if (next.lower > covered_upper ||
		    (next.lower == covered_upper && !covered_upper_inclusive && !next.lower_inclusive)) {
			return false;
		}
		if (next.upper >= 180.0) return true;
		if (next.upper > covered_upper) {
			covered_upper = next.upper;
			covered_upper_inclusive = next.upper_inclusive;
		} else if (next.upper == covered_upper && next.upper_inclusive) {
			covered_upper_inclusive = true;
		}
	}
	return false;
}

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
