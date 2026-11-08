#include "duckomo/series_plan.hpp"
#include "duckomo/series_registry.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckdb/common/types.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace duckdb;
using namespace duckdb::duckomo;

constexpr std::int64_t MICROS_PER_SECOND = 1'000'000;
constexpr std::int64_t MICROS_PER_HOUR = 3'600 * MICROS_PER_SECOND;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

template <typename ACTION>
void MustReject(ACTION action, const std::string &label) {
	try {
		action();
	} catch (const std::exception &) {
		return;
	}
	throw std::runtime_error(label + " should be rejected");
}

void TestTemporalLayoutAndCandidateBounds() {
	const auto hourly = MakeSeriesTemporalLayout(4, MICROS_PER_HOUR);
	Require(hourly.span_microseconds == 4 * MICROS_PER_HOUR, "span is the checked slot-count times step product");
	const auto crossing = MakeSeriesCandidateRange(3 * MICROS_PER_HOUR, 5 * MICROS_PER_HOUR, hourly);
	Require(crossing.first_index == 0 && crossing.last_index == 1 && crossing.count == 2,
	        "closed windows crossing an exact chunk boundary include both candidate chunks");

	const auto negative = MakeSeriesCandidateRange(-1, 0, hourly);
	Require(negative.first_index == -1 && negative.last_index == 0 && negative.count == 2,
	        "negative subsecond endpoints use floor division rather than truncation toward zero");
	const auto exact_negative = MakeSeriesCandidateRange(-4 * MICROS_PER_HOUR, -1, hourly);
	Require(exact_negative.first_index == -1 && exact_negative.last_index == -1 && exact_negative.count == 1,
	        "negative exact-boundary windows remain in the preceding signed chunk");

	const auto one_second = MakeSeriesTemporalLayout(1, MICROS_PER_SECOND);
	const auto at_limit = MakeSeriesCandidateRange(0, 4095 * MICROS_PER_SECOND, one_second);
	Require(at_limit.count == 4096, "the inclusive candidate limit accepts exactly 4096 chunks");
	MustReject([&] { (void)MakeSeriesCandidateRange(0, 4096 * MICROS_PER_SECOND, one_second); },
	           "4097 inclusive candidates");
	MustReject([] { (void)MakeSeriesTemporalLayout(0, 1); }, "zero chunk_time_length");
	MustReject([] { (void)MakeSeriesTemporalLayout(1, 0); }, "zero time_step");
	MustReject([] { (void)MakeSeriesTemporalLayout(-1, 1); }, "negative chunk_time_length");
	MustReject([] { (void)MakeSeriesTemporalLayout(1, -1); }, "negative time_step");
	MustReject([] {
		(void)MakeSeriesTemporalLayout(std::numeric_limits<std::int64_t>::max(), MICROS_PER_SECOND);
	}, "span multiplication overflow");
	MustReject([] { (void)MakeSeriesCandidateRange(2, 1, MakeSeriesTemporalLayout(1, MICROS_PER_SECOND)); },
	           "reversed closed window");
}

void TestSlotIntersectionUsesCeilAndFloor() {
	const auto hourly = MakeSeriesTemporalLayout(4, 60 * MICROS_PER_SECOND);
	const auto partial = IntersectSeriesSlots(0, 1 * MICROS_PER_SECOND, 121 * MICROS_PER_SECOND, hourly);
	Require(!partial.empty && partial.first_slot == 1 && partial.last_slot == 2,
	        "slot intersection rounds the first endpoint up and the last endpoint down");
	const auto singleton = IntersectSeriesSlots(0, 120 * MICROS_PER_SECOND, 120 * MICROS_PER_SECOND, hourly);
	Require(!singleton.empty && singleton.first_slot == 2 && singleton.last_slot == 2,
	        "a closed singleton at a slot time selects exactly that slot");
	const auto slotless = IntersectSeriesSlots(0, 1, 59 * MICROS_PER_SECOND, hourly);
	Require(slotless.empty, "an unaligned interval between slots has an empty intersection");
	const auto following_chunk = IntersectSeriesSlots(1, 4 * 60 * MICROS_PER_SECOND,
	                                                   4 * 60 * MICROS_PER_SECOND, hourly);
	Require(!following_chunk.empty && following_chunk.first_slot == 0 && following_chunk.last_slot == 0,
	        "the next chunk owns the exact epoch boundary slot");

	const auto axes = MakeSeriesTimeAxes(4 * MICROS_PER_HOUR, MICROS_PER_HOUR);
	const auto &outer_fields = StructType::GetChildTypes(axes.type());
	const auto &outer_values = StructValue::GetChildren(axes);
	Require(outer_fields.size() == 1 && IdentifierNameString(outer_fields[0].first) == "time" &&
	            outer_values.size() == 1,
	        "series synthesizes the existing axes STRUCT with a time entry");
	const auto &time_fields = StructType::GetChildTypes(outer_values[0].type());
	const auto &time_values = StructValue::GetChildren(outer_values[0]);
	Require(time_fields.size() == 3 && IdentifierNameString(time_fields[0].first) == "axis" &&
	            IdentifierNameString(time_fields[1].first) == "start" &&
	            IdentifierNameString(time_fields[2].first) == "step" &&
	            StringValue::Get(time_values[0]) == "time" &&
	            time_values[1].GetValue<timestamp_t>().value == 4 * MICROS_PER_HOUR &&
	            time_values[2].GetValue<interval_t>().micros == MICROS_PER_HOUR,
	        "generated axes.time uses the ordinary read_om axis/start/step declaration");
}

void TestSignedChunkNamingAndPrefixValidation() {
	Require(FormatSeriesChunkIndex(0) == "0000", "zero uses four minimum digits");
	Require(FormatSeriesChunkIndex(921) == "0921", "three-digit positive indices are padded");
	Require(FormatSeriesChunkIndex(10000) == "10000", "width four does not truncate longer indices");
	Require(FormatSeriesChunkIndex(-1) == "-001", "negative indices pad the magnitude after the sign");
	Require(BuildSeriesChunkUri("data/model/variable/", -1) == "data/model/variable/chunk_-001.om",
	        "one optional terminal slash is normalized exactly once");
	MustReject([] { (void)BuildSeriesChunkUri("", 0); }, "empty series prefix");
	MustReject([] { (void)BuildSeriesChunkUri("data/model/chunk_0000.om", 0); }, "object filename as prefix");
	MustReject([] { (void)BuildSeriesChunkUri("data/model/*", 0); }, "glob prefix");
	MustReject([] { (void)BuildSeriesChunkUri("https://host/path?token=secret", 0); }, "query-bearing prefix");
	MustReject([] { (void)BuildSeriesChunkUri("https://user@host/path", 0); }, "userinfo prefix");
	MustReject([] { (void)BuildSeriesChunkUri("data/model/data_run/run/variable", 0); }, "data_run prefix");
}

void TestCheckedTimestampBoundaries() {
	const auto min = std::numeric_limits<std::int64_t>::min();
	const auto max = std::numeric_limits<std::int64_t>::max();
	// The range is representable, but mathematical floor puts its first origin on DuckDB's
	// negative-infinity sentinel. The planner must reject this before any child bind can run.
	MustReject([&] {
		(void)MakeSeriesCandidateRange(min + 1, min + 1, MakeSeriesTemporalLayout(2, MICROS_PER_SECOND));
	}, "first chunk origin outside the finite timestamp range");
	// The selected endpoint is finite, while the final generated slot of its candidate is not.
	MustReject([&] {
		(void)MakeSeriesCandidateRange(max - 1, max - 1, MakeSeriesTemporalLayout(2, 3 * MICROS_PER_SECOND));
	}, "last generated slot outside the finite timestamp range");
	MustReject([&] {
		(void)MakeSeriesCandidateRange(min + 1, min + 1, MakeSeriesTemporalLayout(1, MICROS_PER_SECOND));
	}, "timestamp endpoint at an unrepresentable chunk origin");
}

void TestTemporalRegistryResolution() {
	const auto registered = FindSeriesModel("ecmwf_ifs");
	Require(registered.has_value() && registered->chunk_time_length == 504 &&
	            registered->temporal_resolution_seconds == 3600,
	        "exact producer model keys resolve to their frozen temporal defaults");
	Require(!FindSeriesModel("ECMWF_IFS").has_value() && !FindSeriesModel("ecmwf").has_value(),
	        "registry lookup does not normalize case or infer model aliases");

	const auto default_values = ResolveSeriesTemporalParameters(
	    std::optional<std::string_view>("ecmwf_ifs"), SeriesTemporalOverrides{});
	Require(default_values.chunk_time_length == 504 &&
	            default_values.time_step_microseconds == 3600 * MICROS_PER_SECOND &&
	            default_values.chunk_time_length_source == SeriesTemporalParameterSource::RegisteredDefault &&
	            default_values.time_step_source == SeriesTemporalParameterSource::RegisteredDefault,
	        "a registered model supplies both independent defaults");

	SeriesTemporalOverrides length_override;
	length_override.chunk_time_length = 4;
	const auto length_resolved = ResolveSeriesTemporalParameters(
	    std::optional<std::string_view>("ecmwf_ifs"), length_override);
	Require(length_resolved.chunk_time_length == 4 &&
	            length_resolved.time_step_microseconds == 3600 * MICROS_PER_SECOND &&
	            length_resolved.chunk_time_length_source == SeriesTemporalParameterSource::ExplicitOverride &&
	            length_resolved.time_step_source == SeriesTemporalParameterSource::RegisteredDefault,
	        "overriding chunk length inherits only the registered cadence");

	SeriesTemporalOverrides step_override;
	step_override.time_step_microseconds = 2 * 3600 * MICROS_PER_SECOND;
	const auto step_resolved = ResolveSeriesTemporalParameters(
	    std::optional<std::string_view>("ecmwf_ifs"), step_override);
	Require(step_resolved.chunk_time_length == 504 &&
	            step_resolved.time_step_microseconds == 2 * 3600 * MICROS_PER_SECOND &&
	            step_resolved.chunk_time_length_source == SeriesTemporalParameterSource::RegisteredDefault &&
	            step_resolved.time_step_source == SeriesTemporalParameterSource::ExplicitOverride,
	        "overriding cadence inherits only the registered chunk length");

	SeriesTemporalOverrides full_override;
	full_override.chunk_time_length = 7;
	full_override.time_step_microseconds = 5 * 60 * MICROS_PER_SECOND;
	const auto full_resolved = ResolveSeriesTemporalParameters(
	    std::optional<std::string_view>("ecmwf_ifs"), full_override);
	Require(full_resolved.chunk_time_length == 7 &&
	            full_resolved.time_step_microseconds == 5 * 60 * MICROS_PER_SECOND &&
	            full_resolved.chunk_time_length_source == SeriesTemporalParameterSource::ExplicitOverride &&
	            full_resolved.time_step_source == SeriesTemporalParameterSource::ExplicitOverride,
	        "two explicit fields override both registered defaults");

	const auto unknown_with_overrides = ResolveSeriesTemporalParameters(
	    std::optional<std::string_view>("future_model"), full_override);
	const auto omitted_with_overrides = ResolveSeriesTemporalParameters(std::nullopt, full_override);
	Require(unknown_with_overrides.chunk_time_length == 7 && omitted_with_overrides.chunk_time_length == 7 &&
	            unknown_with_overrides.chunk_time_length_source == SeriesTemporalParameterSource::ExplicitOverride,
	        "unknown and omitted model keys accept a complete custom temporal layout");
	MustReject([&] {
		(void)ResolveSeriesTemporalParameters(std::optional<std::string_view>("future_model"), length_override);
	}, "unknown model with an incomplete override set");
	MustReject([&] {
		(void)ResolveSeriesTemporalParameters(std::nullopt, length_override);
	}, "omitted model with an incomplete override set");
	MustReject([&] {
		(void)ResolveSeriesTemporalParameters(std::optional<std::string_view>("ecmwf_ifs"),
		                                     SeriesTemporalOverrides{std::nullopt, 1});
	}, "subsecond explicit cadence");
	MustReject([] { (void)SeriesRegistrySecondsToMicroseconds(0); }, "zero registered cadence");
	const auto maximum_seconds = std::numeric_limits<std::int64_t>::max() / MICROS_PER_SECOND;
	Require(SeriesRegistrySecondsToMicroseconds(maximum_seconds) == maximum_seconds * MICROS_PER_SECOND,
	        "registry cadence conversion accepts the largest representable value");
	MustReject([&] { (void)SeriesRegistrySecondsToMicroseconds(maximum_seconds + 1); },
	           "registered cadence conversion overflow");
}

void TestPureValidationPrecedesObjectAccess() {
	// Planning helpers deliberately have no FileSystem/FileOpener parameter. Invalid constant
	// arguments are rejected while producing a value-only plan, before the binder can resolve URIs.
	MustReject([] { (void)BuildSeriesChunkUri("", 0); }, "invalid prefix");
	MustReject([] { (void)MakeSeriesCandidateRange(0, 1, MakeSeriesTemporalLayout(0, 1)); },
	           "invalid temporal configuration");
	MustReject([] { (void)MakeSeriesCandidateRange(2, 1, MakeSeriesTemporalLayout(1, MICROS_PER_SECOND)); },
	           "invalid time window");
}

} // namespace

int main() {
	try {
		TestTemporalLayoutAndCandidateBounds();
		TestSlotIntersectionUsesCeilAndFloor();
		TestSignedChunkNamingAndPrefixValidation();
		TestCheckedTimestampBoundaries();
		TestTemporalRegistryResolution();
		TestPureValidationPrecedesObjectAccess();
		std::cout << "series temporal planner checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "series temporal planner checks failed: " << error.what() << '\n';
		return 1;
	}
}
