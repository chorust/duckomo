#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "duckdb/common/types/interval.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {
namespace duckomo {

constexpr std::uint64_t MAX_SERIES_CANDIDATES = 4096;

struct SeriesWindow final {
	std::int64_t start_microseconds = 0;
	std::int64_t end_microseconds = 0;
};

struct SeriesTemporalLayout final {
	std::int64_t chunk_time_length = 0;
	std::int64_t step_microseconds = 0;
	std::int64_t span_microseconds = 0;
};

struct SeriesCandidateRange final {
	std::int64_t first_index = 0;
	std::int64_t last_index = -1;
	std::uint64_t count = 0;
};

struct SeriesSlotRange final {
	std::uint64_t first_slot = 0;
	std::uint64_t last_slot = 0;
	bool empty = true;
};

struct SeriesChunkCandidate final {
	std::int64_t index = 0;
	std::int64_t origin_microseconds = 0;
	std::string uri;
	SeriesSlotRange slots;
};

struct SeriesPlan final {
	std::string normalized_prefix;
	SeriesWindow window;
	SeriesTemporalLayout temporal;
	std::vector<SeriesChunkCandidate> candidates;
};

// Parse one constant directory/window request. These routines are pure planning
// operations: they never inspect the filesystem or open a transport.
std::string ReadSeriesPrefix(const TableFunctionBindInput &input);
SeriesWindow ReadSeriesWindow(const TableFunctionBindInput &input);
SeriesTemporalLayout ReadSeriesTemporalLayout(const TableFunctionBindInput &input);
std::string NormalizeSeriesPrefix(const std::string &prefix);

SeriesTemporalLayout MakeSeriesTemporalLayout(std::int64_t chunk_time_length, std::int64_t step_microseconds);
SeriesCandidateRange MakeSeriesCandidateRange(std::int64_t window_start_microseconds,
                                              std::int64_t window_end_microseconds,
                                              const SeriesTemporalLayout &layout,
                                              std::uint64_t candidate_limit = MAX_SERIES_CANDIDATES);
SeriesSlotRange IntersectSeriesSlots(std::int64_t chunk_index, std::int64_t window_start_microseconds,
                                     std::int64_t window_end_microseconds,
                                     const SeriesTemporalLayout &layout);
std::string FormatSeriesChunkIndex(std::int64_t chunk_index);
std::string BuildSeriesChunkUri(const std::string &prefix, std::int64_t chunk_index);
SeriesPlan BuildSeriesPlan(const std::string &prefix, const SeriesWindow &window,
                           const SeriesTemporalLayout &layout);
SeriesPlan BindSeriesPlan(const TableFunctionBindInput &input, const SeriesTemporalLayout &layout);

// Value representation accepted by the existing read_om axes binder:
// {'time': {'axis':'time', 'start': <UTC TIMESTAMP>, 'step': <fixed INTERVAL>}}.
Value MakeSeriesTimeAxes(std::int64_t origin_microseconds, std::int64_t step_microseconds);

} // namespace duckomo
} // namespace duckdb
