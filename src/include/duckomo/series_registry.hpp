#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace duckdb {
namespace duckomo {

enum class SeriesTemporalParameterSource : std::uint8_t {
	RegisteredDefault,
	ExplicitOverride,
};

struct SeriesModelTemporalDefaults final {
	std::int64_t chunk_time_length;
	std::int64_t temporal_resolution_seconds;
};

struct SeriesTemporalOverrides final {
	std::optional<std::int64_t> chunk_time_length;
	std::optional<std::int64_t> time_step_microseconds;
};

struct ResolvedSeriesTemporalParameters final {
	std::int64_t chunk_time_length;
	std::int64_t time_step_microseconds;
	SeriesTemporalParameterSource chunk_time_length_source;
	SeriesTemporalParameterSource time_step_source;
};

// Returns defaults only for an exact, case-sensitive producer model key.
std::optional<SeriesModelTemporalDefaults> FindSeriesModel(std::string_view model_key);

// Convert frozen registry seconds to DuckDB interval microseconds with overflow checks.
std::int64_t SeriesRegistrySecondsToMicroseconds(std::int64_t seconds);

// Resolve each field independently: explicit override, then exact registry default.
// An omitted or unknown model is valid only when both overrides are supplied.
ResolvedSeriesTemporalParameters ResolveSeriesTemporalParameters(
    std::optional<std::string_view> model_key, const SeriesTemporalOverrides &overrides);

} // namespace duckomo
} // namespace duckdb
