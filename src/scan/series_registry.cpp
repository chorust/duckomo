#include "duckomo/series_registry.hpp"

#include <limits>
#include <string>

#include "duckdb/common/exception/binder_exception.hpp"
#include "duckomo/generated_series_registry.hpp"

namespace duckdb {
namespace duckomo {
namespace {

constexpr std::int64_t MICROS_PER_SECOND = 1'000'000;

[[noreturn]] void InvalidTemporalParameters(const std::string &message) {
	throw BinderException("read_om_series " + message);
}

} // namespace

std::optional<SeriesModelTemporalDefaults> FindSeriesModel(std::string_view model_key) {
	for (const auto &model : generated::SERIES_MODELS) {
		if (model.key == model_key) {
			return SeriesModelTemporalDefaults{model.chunk_time_length, model.temporal_resolution_seconds};
		}
	}
	return std::nullopt;
}

std::int64_t SeriesRegistrySecondsToMicroseconds(std::int64_t seconds) {
	if (seconds <= 0 || seconds > std::numeric_limits<std::int64_t>::max() / MICROS_PER_SECOND) {
		InvalidTemporalParameters("registered time_step seconds must be positive and fit in signed 64-bit microseconds");
	}
	return seconds * MICROS_PER_SECOND;
}

ResolvedSeriesTemporalParameters ResolveSeriesTemporalParameters(
    std::optional<std::string_view> model_key, const SeriesTemporalOverrides &overrides) {
	const auto defaults = model_key.has_value() ? FindSeriesModel(*model_key) : std::nullopt;
	if (!defaults.has_value() && (!overrides.chunk_time_length.has_value() ||
	                              !overrides.time_step_microseconds.has_value())) {
		if (model_key.has_value()) {
			InvalidTemporalParameters("model '" + std::string(*model_key) +
			                          "' is not registered; provide chunk_time_length and time_step overrides");
		}
		InvalidTemporalParameters("model is omitted; provide both chunk_time_length and time_step overrides");
	}

	ResolvedSeriesTemporalParameters result{};
	if (overrides.chunk_time_length.has_value()) {
		result.chunk_time_length = *overrides.chunk_time_length;
		result.chunk_time_length_source = SeriesTemporalParameterSource::ExplicitOverride;
	} else {
		result.chunk_time_length = defaults->chunk_time_length;
		result.chunk_time_length_source = SeriesTemporalParameterSource::RegisteredDefault;
	}
	if (overrides.time_step_microseconds.has_value()) {
		result.time_step_microseconds = *overrides.time_step_microseconds;
		result.time_step_source = SeriesTemporalParameterSource::ExplicitOverride;
	} else {
		result.time_step_microseconds =
		    SeriesRegistrySecondsToMicroseconds(defaults->temporal_resolution_seconds);
		result.time_step_source = SeriesTemporalParameterSource::RegisteredDefault;
	}

	if (result.chunk_time_length <= 0) {
		InvalidTemporalParameters("chunk_time_length must be a positive signed 64-bit slot count");
	}
	if (result.time_step_microseconds <= 0 || result.time_step_microseconds % MICROS_PER_SECOND != 0) {
		InvalidTemporalParameters("time_step must be positive and have whole-second precision");
	}
	return result;
}

} // namespace duckomo
} // namespace duckdb
