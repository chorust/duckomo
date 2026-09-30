#include "duckomo/axis_selection.hpp"
#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &) {
}
} // namespace duckdb

namespace {

using namespace duckdb;
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

SemanticAxis TimeAxis() {
	SemanticAxis axis;
	axis.kind = SemanticAxisKind::Time;
	axis.axis_name = "time";
	axis.axis_index = 0;
	axis.axis_length = 2;
	axis.stride = 3;
	axis.output_type = LogicalType::TIMESTAMP;
	axis.timestamps = {timestamp_t(0), timestamp_t(3600LL * 1000000)};
	return axis;
}

SemanticAxis MemberAxis() {
	SemanticAxis axis;
	axis.kind = SemanticAxisKind::Member;
	axis.axis_name = "member";
	axis.axis_index = 1;
	axis.axis_length = 3;
	axis.stride = 1;
	axis.output_type = LogicalType::BIGINT;
	axis.integer_members = {10, 20, 20};
	return axis;
}

void VerifyCombinedPredicateAndSegments() {
	const SemanticAxes axes = {TimeAxis(), MemberAxis()};
	AxisPredicate predicate;
	predicate.necessary_conditions.push_back({0, AxisComparison::GreaterEqual,
	                                         Value::TIMESTAMP(timestamp_t(3600LL * 1000000))});
	predicate.necessary_conditions.push_back({1, AxisComparison::Equal, Value::BIGINT(20)});
	AxisSelectionCursor cursor({2, 3}, axes, predicate);
	Require(cursor.HasConstraints() && !cursor.IsEmpty(), "combined predicate should be non-empty");
	Require(cursor.Contains(4) && cursor.Contains(5) && !cursor.Contains(3),
	        "membership checks should recognize selected positions independently of traversal");
	std::vector<std::uint64_t> positions;
	Require(cursor.Next(1, positions) == 1 && positions == std::vector<std::uint64_t>{4},
	        "first bounded cursor batch should preserve row-major ordinal 4");
	Require(cursor.Next(2, positions) == 1 && positions == std::vector<std::uint64_t>{5},
	        "duplicate member coordinates should retain their distinct source positions");
	Require(cursor.Next(2, positions) == 0 && cursor.IsExhausted(), "cursor should exhaust exactly once");
	Require(cursor.Contains(4), "membership checks should still work after cursor exhaustion");

	const auto segments = BuildSelectedBatchSegments({2, 3}, {1, 2, 4, 5});
	Require(segments.size() == 2, "sparse row positions should form two contiguous read segments");
	Require(segments[0].linear_index == 1 && segments[0].count == 2 && segments[0].batch_offset == 0,
	        "first selected read segment should preserve output offsets");
	Require(segments[1].linear_index == 4 && segments[1].count == 2 && segments[1].batch_offset == 2,
	        "second selected read segment should preserve output offsets");
}

void VerifyContradictionBecomesEmpty() {
	AxisPredicate predicate;
	predicate.necessary_conditions.push_back({1, AxisComparison::Equal, Value::BIGINT(99)});
	AxisSelectionCursor cursor({2, 3}, {TimeAxis(), MemberAxis()}, predicate);
	Require(cursor.HasConstraints() && cursor.IsEmpty(), "unmatched equality should prove an empty selection");
	std::vector<std::uint64_t> positions;
	Require(cursor.Next(8, positions) == 0, "empty selection must emit no positions");
}

void VerifyUnconstrainedAxesCoverEachPositionOnce() {
	AxisSelectionCursor cursor({2, 3}, {TimeAxis(), MemberAxis()}, {});
	std::vector<std::uint64_t> positions;
	std::vector<std::uint64_t> all;
	while (cursor.Next(2, positions) != 0) all.insert(all.end(), positions.begin(), positions.end());
	Require(all == std::vector<std::uint64_t>({0, 1, 2, 3, 4, 5}),
	        "unconstrained cursor should cover every row-major position once");
}

} // namespace

int main() {
	try {
		VerifyCombinedPredicateAndSegments();
		VerifyContradictionBecomesEmpty();
		VerifyUnconstrainedAxesCoverEachPositionOnce();
		std::cout << "semantic axis selection checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
