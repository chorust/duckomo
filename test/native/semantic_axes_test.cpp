#include "duckomo/semantic_axes.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
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

BoundSchema MakeSchema(std::vector<std::uint64_t> shape, std::optional<OmTimeCoordinate> time = std::nullopt) {
	BoundSchema schema;
	schema.shape = shape;
	schema.row_count = 1;
	for (const auto length : shape) schema.row_count *= length;
	BoundVariable variable;
	variable.shape = std::move(shape);
	variable.row_count = schema.row_count;
	variable.time = std::move(time);
	schema.variables.emplace_back(std::move(variable));
	return schema;
}

Value RegularTimestamp(const std::string &axis, timestamp_t start, std::int64_t step_micros) {
	return Value::STRUCT({{"axis", Value(axis)}, {"start", Value::TIMESTAMP(start)},
	                       {"step", Value::INTERVAL(interval_t{0, 0, step_micros})}});
}

void VerifyCompactRegularCoordinatesAndTypedValues() {
	auto schema = MakeSchema({2, 3});
	const AxisDeclarations declared_axes = {{"time", "member_axis"}};
	auto axes_value = Value::STRUCT({
	    {"time", RegularTimestamp("time", timestamp_t(2'000'000), -1'000'000)},
	    {"member", Value::STRUCT({{"axis", Value("member_axis")}, {"start", Value::BIGINT(42)},
	                              {"step", Value::BIGINT(0)}})}});
	auto axes = BindSemanticAxes(&axes_value, schema, declared_axes);
	Require(axes.size() == 2, "both semantic declarations should bind");
	Require(axes[0].regular && axes[0].timestamps.empty(), "regular timestamps should not be expanded at bind time");
	Require(axes[1].regular && axes[1].integer_members.empty(), "regular members should not be expanded at bind time");
	Require(axes[0].CoordinateValue(0).GetValue<timestamp_t>() == timestamp_t(2'000'000),
	        "regular timestamp start should be preserved");
	Require(axes[0].CoordinateValue(3).GetValue<timestamp_t>() == timestamp_t(1'000'000),
	        "negative timestamp step should be applied by logical axis position");
	Require(axes[1].CoordinateValue(1).GetValue<std::int64_t>() == 42 &&
	            axes[1].CoordinateValue(2).GetValue<std::int64_t>() == 42,
	        "zero member step should preserve duplicate coordinates");

	auto explicit_values = Value::STRUCT({
	    {"member", Value::STRUCT({{"axis", Value("member_axis")},
	                              {"values", Value::LIST(LogicalType::VARCHAR,
	                                                     {Value("ctl"), Value("pert"), Value("p3")})}})}});
	auto explicit_axes = BindSemanticAxes(&explicit_values, schema, declared_axes);
	Require(!explicit_axes[0].regular && explicit_axes[0].text_members == std::vector<std::string>({"ctl", "pert", "p3"}),
	        "explicit member strings should remain exact and ordered");
}

void VerifyFileTimeEvidenceAndLosslessValidation() {
	OmTimeCoordinate evidence;
	evidence.epoch_seconds = {0, 1};
	auto schema = MakeSchema({2}, evidence);
	const AxisDeclarations declared_axes = {{"time"}};
	auto consistent = Value::STRUCT({{"time", RegularTimestamp("time", timestamp_t(0), 1'000'000)}});
	auto axes = BindSemanticAxes(&consistent, schema, declared_axes);
	Require(axes.size() == 1 && axes[0].CoordinateValue(1).GetValue<timestamp_t>() == timestamp_t(1'000'000),
	        "regular explicit time must agree with file UTC-second evidence");
	auto conflict = Value::STRUCT({{"time", RegularTimestamp("time", timestamp_t(1), 1'000'000)}});
	bool rejected = false;
	try {
		(void)BindSemanticAxes(&conflict, schema, declared_axes);
	} catch (const BinderException &) {
		rejected = true;
	}
	Require(rejected, "time metadata disagreement must fail during binding");

	auto overflow_schema = MakeSchema({3});
	auto overflow = Value::STRUCT({{"member", Value::STRUCT({{"axis", Value("member")},
	    {"start", Value::BIGINT(std::numeric_limits<std::int64_t>::max())}, {"step", Value::BIGINT(1)}})}});
	rejected = false;
	try {
		(void)BindSemanticAxes(&overflow, overflow_schema, AxisDeclarations{{"member"}});
	} catch (const BinderException &) {
		rejected = true;
	}
	Require(rejected, "regular member overflow must be rejected before scanning");

	auto precision = Value::STRUCT({{"level", Value::STRUCT({{"axis", Value("level")},
	    {"values", Value::LIST(LogicalType::BIGINT, {Value::BIGINT(9'007'199'254'740'993LL)})},
	    {"kind", Value("model")}, {"unit", Value("1")}})}});
	rejected = false;
	try {
		(void)BindSemanticAxes(&precision, MakeSchema({1}), AxisDeclarations{{"level"}});
	} catch (const BinderException &) {
		rejected = true;
	}
	Require(rejected, "integer levels that lose precision as DOUBLE must fail binding");

	auto fractional_model = Value::STRUCT({{"level", Value::STRUCT({{"axis", Value("level")},
	    {"start", Value::DOUBLE(1)}, {"step", Value::DOUBLE(0.5)},
	    {"kind", Value("model")}, {"unit", Value("1")}})}});
	rejected = false;
	try {
		(void)BindSemanticAxes(&fractional_model, MakeSchema({3}), AxisDeclarations{{"level"}});
	} catch (const BinderException &) {
		rejected = true;
	}
	Require(rejected, "fractional intermediate model levels must fail binding");
}

} // namespace

int main() {
	try {
		VerifyCompactRegularCoordinatesAndTypedValues();
		VerifyFileTimeEvidenceAndLosslessValidation();
		std::cout << "semantic axis checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
