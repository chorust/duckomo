#pragma once

#include <cstddef>
#include <cstdint>

namespace duckdb {
namespace duckomo {

// Query selection state is copied out of DuckDB's filter callback and is
// deliberately bounded independently of the source array size. Two predicate
// families may coexist (geographic and semantic axes), so each gets 160 KiB;
// interval payload gets 512 KiB, native selector storage is also capped at
// 4096 ranges, and each deduplicated diagnostic list gets 64 KiB.
constexpr std::uint64_t MAX_SELECTION_TOTAL_PAYLOAD_BYTES = 1024 * 1024;
constexpr std::uint64_t MAX_SELECTION_PREDICATE_BYTES = 160 * 1024;
constexpr std::uint64_t MAX_SELECTION_INTERVAL_BYTES = 512 * 1024;
constexpr std::uint64_t MAX_SELECTION_DIAGNOSTIC_BYTES = 64 * 1024;
constexpr std::uint64_t MAX_NATIVE_WINDOW_POINTS = 65536;
constexpr std::uint64_t MAX_SELECTOR_PAYLOAD_BYTES = 256 * 1024;
constexpr std::uint64_t MAX_SPATIAL_SELECTOR_RANGES = 4096;
static_assert(2 * MAX_SELECTION_PREDICATE_BYTES + MAX_SELECTION_INTERVAL_BYTES +
	              2 * MAX_SELECTION_DIAGNOSTIC_BYTES <= MAX_SELECTION_TOTAL_PAYLOAD_BYTES,
	          "selection payload components must fit the shared one MiB budget");

template <class T>
constexpr std::size_t MaxSelectionItems() noexcept {
	return static_cast<std::size_t>(MAX_SELECTION_PREDICATE_BYTES / sizeof(T));
}

} // namespace duckomo
} // namespace duckdb
