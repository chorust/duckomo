#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "duckdb/common/types/interval.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckomo/dimensions.hpp"

namespace duckdb {
namespace duckomo {

enum class SemanticAxisKind : std::uint8_t { Time, Level, LeadTime, Member, Run };

// Explicit coordinates are stored by value. Regular coordinates keep their
// checked start/step pair so large axes do not allocate one value per row.
struct SemanticAxis final {
	SemanticAxisKind kind = SemanticAxisKind::Time;
	std::string axis_name;
	idx_t axis_index = 0;
	std::uint64_t axis_length = 0;
	std::uint64_t stride = 0;
	LogicalType output_type = LogicalType::INVALID;
	bool regular = false;
	std::string level_kind;
	std::string unit;
	timestamp_t timestamp_start = timestamp_t(0);
	std::int64_t timestamp_step_micros = 0;
	double number_start = 0;
	double number_step = 0;
	std::int64_t duration_start_micros = 0;
	std::int64_t duration_step_micros = 0;
	std::int64_t integer_member_start = 0;
	std::int64_t integer_member_step = 0;
	std::vector<timestamp_t> timestamps;
	std::vector<double> numbers;
	std::vector<interval_t> durations;
	std::vector<std::int64_t> integer_members;
	std::vector<std::string> text_members;

	idx_t CoordinateIndex(std::uint64_t logical_position) const;
	Value CoordinateValue(std::uint64_t logical_position) const;
	bool operator==(const SemanticAxis &other) const;
};

using SemanticAxes = std::vector<SemanticAxis>;

// Parse and validate the axes STRUCT against the declared logical axes and
// optional spatial-axis names. NULL/omitted declarations return an empty list.
SemanticAxes BindSemanticAxes(const Value *value, const BoundSchema &schema, const AxisDeclarations &declared_axes,
	                          const std::vector<std::string> &spatial_axis_names = {});

const char *SemanticAxisName(SemanticAxisKind kind) noexcept;
std::string SemanticOutputName(SemanticAxisKind kind);

} // namespace duckomo
} // namespace duckdb
