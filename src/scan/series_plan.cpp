#include "duckomo/series_plan.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <utility>

#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/common/types.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/om_reader.hpp"
#include "duckomo/remote_file.hpp"
#include "duckomo/series_registry.hpp"

namespace duckdb {
namespace duckomo {
namespace {

constexpr std::int64_t MICROS_PER_SECOND = 1'000'000;
using Wide = __int128_t;

[[noreturn]] void InvalidSeries(const std::string &message) {
	throw BinderException("read_om_series " + message);
}

bool IsFiniteTimestamp(std::int64_t microseconds) {
	return Value::IsFinite(timestamp_t(microseconds));
}

std::int64_t ReadInt64(const Value &value, const std::string &where) {
	if (value.IsNull() || !value.type().IsIntegral()) {
		InvalidSeries(where + " must be a non-NULL integer");
	}
	try {
		return value.DefaultCastAs(LogicalType::BIGINT, true).GetValue<std::int64_t>();
	} catch (const std::exception &) {
		InvalidSeries(where + " must fit in a signed 64-bit integer");
	}
}

std::int64_t ReadFixedStep(const Value &value) {
	if (value.IsNull() || value.type().id() != LogicalTypeId::INTERVAL) {
		InvalidSeries("time_step must be a non-NULL fixed INTERVAL");
	}
	const auto interval = value.GetValue<interval_t>();
	if (interval.months != 0) {
		InvalidSeries("time_step cannot contain calendar months or years");
	}
	const Wide total = static_cast<Wide>(interval.days) * Interval::MICROS_PER_DAY + interval.micros;
	if (total <= 0 || total > std::numeric_limits<std::int64_t>::max()) {
		InvalidSeries("time_step must be positive and fit in signed 64-bit microseconds");
	}
	const auto step = static_cast<std::int64_t>(total);
	if (step % MICROS_PER_SECOND != 0) {
		InvalidSeries("time_step must have whole-second precision");
	}
	return step;
}

timestamp_t ReadTimestamp(const Value &value, const std::string &label) {
	if (value.IsNull()) InvalidSeries(label + " must be a finite, non-NULL timestamp");
	timestamp_t result;
	if (value.type().id() == LogicalTypeId::TIMESTAMP) {
		result = value.GetValue<timestamp_t>();
	} else if (value.type().id() == LogicalTypeId::TIMESTAMP_TZ) {
		result = timestamp_t(value.GetValue<timestamp_tz_t>().value);
	} else {
		InvalidSeries(label + " must be TIMESTAMP or TIMESTAMPTZ");
	}
	if (!Value::IsFinite(result)) InvalidSeries(label + " must be a finite timestamp");
	return result;
}

std::int64_t FloorDiv(std::int64_t numerator, std::int64_t positive_denominator) {
	const auto quotient = numerator / positive_denominator;
	const auto remainder = numerator % positive_denominator;
	return remainder < 0 ? quotient - 1 : quotient;
}

Wide FloorDiv(Wide numerator, Wide positive_denominator) {
	Wide quotient = numerator / positive_denominator;
	const Wide remainder = numerator % positive_denominator;
	if (remainder < 0) --quotient;
	return quotient;
}

Wide CeilDiv(Wide numerator, Wide positive_denominator) {
	return -FloorDiv(-numerator, positive_denominator);
}

void ValidateLayout(const SeriesTemporalLayout &layout) {
	const auto expected = MakeSeriesTemporalLayout(layout.chunk_time_length, layout.step_microseconds);
	if (layout.span_microseconds != expected.span_microseconds) {
		InvalidSeries("temporal layout has an inconsistent chunk span");
	}
}

std::int64_t CheckedFiniteTimestamp(Wide value, const std::string &what) {
	if (value < std::numeric_limits<std::int64_t>::min() ||
	    value > std::numeric_limits<std::int64_t>::max() ||
	    !IsFiniteTimestamp(static_cast<std::int64_t>(value))) {
		InvalidSeries(what + " is outside the finite TIMESTAMP range");
	}
	return static_cast<std::int64_t>(value);
}

std::int64_t CheckedOrigin(std::int64_t chunk_index, const SeriesTemporalLayout &layout) {
	return CheckedFiniteTimestamp(static_cast<Wide>(chunk_index) * layout.span_microseconds,
	                              "chunk origin");
}

bool HasPathComponent(const std::string &path, const std::string &component) {
	std::size_t begin = 0;
	while (begin <= path.size()) {
		const auto end = path.find('/', begin);
		const auto length = (end == std::string::npos ? path.size() : end) - begin;
		if (path.compare(begin, length, component) == 0) return true;
		if (end == std::string::npos) break;
		begin = end + 1;
	}
	return false;
}

std::string RemotePath(const std::string &prefix) {
	const auto authority_start = prefix.find("://") + 3;
	const auto path_start = prefix.find('/', authority_start);
	const auto authority_end = path_start == std::string::npos ? prefix.size() : path_start;
	const auto authority = prefix.substr(authority_start, authority_end - authority_start);
	if (authority.empty()) InvalidSeries("remote prefix must include an endpoint");
	if (authority.find('@') != std::string::npos) {
		InvalidSeries("remote prefix must not contain URI userinfo or credentials");
	}
	return path_start == std::string::npos ? std::string() : prefix.substr(path_start);
}

} // namespace

std::string ReadSeriesPrefix(const TableFunctionBindInput &input) {
	if (input.inputs.size() != 1 || input.inputs[0].IsNull() ||
	    input.inputs[0].type().id() != LogicalTypeId::VARCHAR) {
		InvalidSeries("expects one non-NULL constant VARCHAR directory prefix");
	}
	return NormalizeSeriesPrefix(StringValue::Get(input.inputs[0]));
}

SeriesWindow ReadSeriesWindow(const TableFunctionBindInput &input) {
	const auto *times = GetTableFunctionNamedArgument(input, "valid_times");
	const auto *start = GetTableFunctionNamedArgument(input, "start");
	const auto *end = GetTableFunctionNamedArgument(input, "end");
	const bool has_times = times != nullptr;
	const bool has_start = start != nullptr;
	const bool has_end = end != nullptr;
	if (has_times && (has_start || has_end)) {
		InvalidSeries("valid_times and start/end are mutually exclusive window forms");
	}

	timestamp_t start_timestamp;
	timestamp_t end_timestamp;
	if (has_times) {
		if (times->IsNull() || times->type().id() != LogicalTypeId::LIST) {
			InvalidSeries("valid_times must be a non-NULL two-element timestamp list");
		}
		const auto &children = ListValue::GetChildren(*times);
		if (children.size() != 2) {
			InvalidSeries("valid_times must contain exactly two endpoints");
		}
		start_timestamp = ReadTimestamp(children[0], "valid_times start");
		end_timestamp = ReadTimestamp(children[1], "valid_times end");
	} else {
		if (has_start != has_end) {
			InvalidSeries("both start and end must be supplied together");
		}
		if (!has_start) InvalidSeries("requires valid_times or both start and end window endpoints");
		start_timestamp = ReadTimestamp(*start, "start");
		end_timestamp = ReadTimestamp(*end, "end");
	}
	if (start_timestamp.value > end_timestamp.value) {
		InvalidSeries("window start must be less than or equal to end");
	}
	return {start_timestamp.value, end_timestamp.value};
}

SeriesTemporalLayout ReadSeriesTemporalLayout(const TableFunctionBindInput &input) {
	std::optional<std::string> model;
	if (const auto *model_value = GetTableFunctionNamedArgument(input, "model")) {
		if (model_value->IsNull() || model_value->type().id() != LogicalTypeId::VARCHAR) {
			InvalidSeries("model must be a non-NULL VARCHAR");
		}
		model = StringValue::Get(*model_value);
	}

	SeriesTemporalOverrides overrides;
	if (const auto *length_value = GetTableFunctionNamedArgument(input, "chunk_time_length")) {
		overrides.chunk_time_length = ReadInt64(*length_value, "chunk_time_length");
	}
	if (const auto *step_value = GetTableFunctionNamedArgument(input, "time_step")) {
		overrides.time_step_microseconds = ReadFixedStep(*step_value);
	}

	std::optional<std::string_view> model_key;
	if (model.has_value()) model_key = *model;
	const auto resolved = ResolveSeriesTemporalParameters(model_key, overrides);
	return MakeSeriesTemporalLayout(resolved.chunk_time_length, resolved.time_step_microseconds);
}

std::string NormalizeSeriesPrefix(const std::string &prefix) {
	if (prefix.empty()) InvalidSeries("directory prefix must not be empty");
	if (prefix.find_first_of("*?[]{}#") != std::string::npos) {
		InvalidSeries("directory prefix cannot contain glob characters or a URI fragment");
	}
	if (prefix.find('?') != std::string::npos) InvalidSeries("directory prefix cannot contain a query string");

	std::string normalized = prefix;
	const auto delimiter = prefix.find("://");
	if (delimiter != std::string::npos) {
		std::string scheme = prefix.substr(0, delimiter);
		std::transform(scheme.begin(), scheme.end(), scheme.begin(),
		               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		if (scheme != "http" && scheme != "https" && scheme != "s3") {
			InvalidSeries("remote directory prefix must use HTTP, HTTPS, or S3");
		}
		const auto path = RemotePath(prefix);
		if (!IsSupportedRemoteOmUri(prefix)) {
			// IsSupportedRemoteOmUri validates the endpoint policy without opening it.
			// A suffix is unnecessary; the parser accepts a directory URI as well.
			InvalidSeries("remote directory prefix is not a supported HTTP, HTTPS, or S3 URI");
		}
		if (HasPathComponent(path, "data_run") || HasPathComponent(path, "data_spatial")) {
			InvalidSeries("data_run/ and data_spatial/ prefixes are outside this layout");
		}
	} else {
		try {
			ValidateLocalFilePath(prefix);
		} catch (const ReaderError &error) {
			InvalidSeries(std::string("invalid local directory prefix: ") + error.what());
		}
		if (HasPathComponent(prefix, "data_run") || HasPathComponent(prefix, "data_spatial")) {
			InvalidSeries("data_run/ and data_spatial/ prefixes are outside this layout");
		}
	}

	if (normalized.back() == '/' && normalized != "/") {
		normalized.pop_back();
		if (!normalized.empty() && normalized.back() == '/') {
			InvalidSeries("directory prefix may have at most one terminal slash");
		}
	}
	if (normalized.empty()) InvalidSeries("directory prefix must identify one directory");
	const auto last_slash = normalized.rfind('/');
	const auto final_component = normalized.substr(last_slash == std::string::npos ? 0 : last_slash + 1);
	if (final_component.size() >= 3 && final_component.compare(final_component.size() - 3, 3, ".om") == 0) {
		InvalidSeries("directory prefix must not name an .om object");
	}
	return normalized;
}

SeriesTemporalLayout MakeSeriesTemporalLayout(std::int64_t chunk_time_length, std::int64_t step_microseconds) {
	if (chunk_time_length <= 0) InvalidSeries("chunk_time_length must be a positive signed 64-bit slot count");
	if (step_microseconds <= 0) InvalidSeries("time_step must be positive");
	if (step_microseconds % MICROS_PER_SECOND != 0) {
		InvalidSeries("time_step must have whole-second precision");
	}
	const Wide span = static_cast<Wide>(chunk_time_length) * step_microseconds;
	if (span <= 0 || span > std::numeric_limits<std::int64_t>::max()) {
		InvalidSeries("chunk_time_length multiplied by time_step exceeds signed 64-bit microseconds");
	}
	return {chunk_time_length, step_microseconds, static_cast<std::int64_t>(span)};
}

SeriesCandidateRange MakeSeriesCandidateRange(std::int64_t window_start_microseconds,
                                              std::int64_t window_end_microseconds,
                                              const SeriesTemporalLayout &layout,
                                              std::uint64_t candidate_limit) {
	ValidateLayout(layout);
	if (!IsFiniteTimestamp(window_start_microseconds) || !IsFiniteTimestamp(window_end_microseconds)) {
		InvalidSeries("window endpoints must be finite timestamps");
	}
	if (window_start_microseconds > window_end_microseconds) {
		InvalidSeries("window start must be less than or equal to end");
	}
	if (candidate_limit == 0) InvalidSeries("candidate limit must be positive");
	const auto first = FloorDiv(window_start_microseconds, layout.span_microseconds);
	const auto last = FloorDiv(window_end_microseconds, layout.span_microseconds);
	const Wide count = static_cast<Wide>(last) - first + 1;
	if (count <= 0 || count > candidate_limit) {
		InvalidSeries("window expands to more than " + std::to_string(candidate_limit) +
		              " candidate chunks; split the window into smaller ranges");
	}
	(void)CheckedOrigin(first, layout);
	(void)CheckedOrigin(last, layout);
	const Wide final_slot = static_cast<Wide>(last) * layout.span_microseconds +
	                        static_cast<Wide>(layout.chunk_time_length - 1) * layout.step_microseconds;
	(void)CheckedFiniteTimestamp(final_slot, "final generated slot");
	return {first, last, static_cast<std::uint64_t>(count)};
}

SeriesSlotRange IntersectSeriesSlots(std::int64_t chunk_index, std::int64_t window_start_microseconds,
                                     std::int64_t window_end_microseconds,
                                     const SeriesTemporalLayout &layout) {
	ValidateLayout(layout);
	if (!IsFiniteTimestamp(window_start_microseconds) || !IsFiniteTimestamp(window_end_microseconds) ||
	    window_start_microseconds > window_end_microseconds) {
		InvalidSeries("slot intersection requires an ordered finite timestamp window");
	}
	const auto origin = CheckedOrigin(chunk_index, layout);
	const Wide first = CeilDiv(static_cast<Wide>(window_start_microseconds) - origin, layout.step_microseconds);
	const Wide last = FloorDiv(static_cast<Wide>(window_end_microseconds) - origin,
	                           static_cast<Wide>(layout.step_microseconds));
	const Wide maximum = layout.chunk_time_length - 1;
	if (last < 0 || first > maximum || first > last) return {};
	const auto clamped_first = static_cast<std::uint64_t>(std::max<Wide>(first, 0));
	const auto clamped_last = static_cast<std::uint64_t>(std::min<Wide>(last, maximum));
	if (clamped_first > clamped_last) return {};
	return {clamped_first, clamped_last, false};
}

std::string FormatSeriesChunkIndex(std::int64_t chunk_index) {
	std::string digits;
	std::string sign;
	if (chunk_index < 0) {
		sign = "-";
		const auto magnitude = static_cast<std::uint64_t>(-(chunk_index + 1)) + 1;
		digits = std::to_string(magnitude);
		if (digits.size() < 3) digits.insert(0, 3 - digits.size(), '0');
	} else {
		digits = std::to_string(static_cast<std::uint64_t>(chunk_index));
		if (digits.size() < 4) digits.insert(0, 4 - digits.size(), '0');
	}
	return sign + digits;
}

std::string BuildSeriesChunkUri(const std::string &prefix, std::int64_t chunk_index) {
	const auto normalized = NormalizeSeriesPrefix(prefix);
	return normalized + (normalized.back() == '/' ? "" : "/") + "chunk_" +
	       FormatSeriesChunkIndex(chunk_index) + ".om";
}

SeriesPlan BuildSeriesPlan(const std::string &prefix, const SeriesWindow &window,
                           const SeriesTemporalLayout &layout) {
	SeriesPlan result;
	result.normalized_prefix = NormalizeSeriesPrefix(prefix);
	result.window = window;
	result.temporal = layout;
	const auto range = MakeSeriesCandidateRange(window.start_microseconds, window.end_microseconds, layout);
	result.candidates.reserve(static_cast<std::size_t>(range.count));
	for (std::uint64_t offset = 0; offset < range.count; offset++) {
		const auto index = static_cast<std::int64_t>(static_cast<Wide>(range.first_index) + offset);
		SeriesChunkCandidate candidate;
		candidate.index = index;
		candidate.origin_microseconds = CheckedOrigin(index, layout);
		candidate.uri = BuildSeriesChunkUri(result.normalized_prefix, index);
		candidate.slots = IntersectSeriesSlots(index, window.start_microseconds, window.end_microseconds, layout);
		result.candidates.emplace_back(std::move(candidate));
	}
	return result;
}

SeriesPlan BindSeriesPlan(const TableFunctionBindInput &input, const SeriesTemporalLayout &layout) {
	if (GetTableFunctionNamedArgument(input, "axes") != nullptr) {
		InvalidSeries("does not accept axes; the time axis is generated automatically");
	}
	return BuildSeriesPlan(ReadSeriesPrefix(input), ReadSeriesWindow(input), layout);
}

Value MakeSeriesTimeAxes(std::int64_t origin_microseconds, std::int64_t step_microseconds) {
	(void)CheckedFiniteTimestamp(origin_microseconds, "generated time origin");
	(void)MakeSeriesTemporalLayout(1, step_microseconds);
	const auto time_axis = Value::STRUCT({
	    {"axis", Value("time")},
	    {"start", Value::TIMESTAMP(timestamp_t(origin_microseconds))},
	    {"step", Value::INTERVAL(interval_t{0, 0, step_microseconds})},
	});
	return Value::STRUCT({{"time", time_axis}});
}

} // namespace duckomo
} // namespace duckdb
