#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "duckdb/common/types/value.hpp"

namespace duckdb {
namespace duckomo {

inline constexpr std::uint64_t REGION_MAX_COUNT = 4096;
inline constexpr std::uint64_t REGION_MAX_WKB_BYTES = 1024 * 1024;
inline constexpr std::uint64_t REGION_MAX_TOTAL_WKB_BYTES = 16 * 1024 * 1024;
inline constexpr std::uint64_t REGION_MAX_VERTICES = 65536;
inline constexpr std::uint64_t REGION_MAX_TOTAL_VERTICES = 1048576;
inline constexpr std::uint64_t REGION_MAX_OWNED_BYTES = 64 * 1024 * 1024;
inline constexpr std::uint64_t REGION_MAX_GEOMETRY_BYTES = 2 * 1024 * 1024;

struct RegionEnvelope final {
	bool empty = true;
	double min_longitude = 0;
	double min_latitude = 0;
	double max_longitude = 0;
	double max_latitude = 0;
};

struct Region final {
	std::string id;
	std::vector<std::uint8_t> wkb;
	RegionEnvelope envelope;
	std::uint64_t vertex_count = 0;
	std::uint64_t geometry_bytes = 0;
};

// Owns and validates a complete bound regions value before any spatial call.
class RegionCollection final {
public:
	static RegionCollection Build(const Value &input, const std::function<void()> &check_interrupt = {});

	const std::vector<Region> &GetRegions() const noexcept {
		return regions;
	}

	std::uint64_t GetTotalVertices() const noexcept {
		return total_vertices;
	}

	std::uint64_t GetOwnedBytes() const noexcept {
		return owned_bytes;
	}

	bool Equals(const RegionCollection &other) const noexcept;

private:
	std::vector<Region> regions;
	std::uint64_t total_vertices = 0;
	std::uint64_t owned_bytes = 0;
};

void ValidateRegionCrs(const Value &input);

} // namespace duckomo
} // namespace duckdb
