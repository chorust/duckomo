#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "duckomo/reader.hpp"

namespace duckdb {
namespace duckomo {

// Open-Meteo time coordinates store UTC Unix seconds, either a scalar
// valid_time for a spatial snapshot or an Int64 time array for a run.
struct OmTimeCoordinate final {
	std::vector<std::int64_t> epoch_seconds;
	bool scalar = false;
	bool operator==(const OmTimeCoordinate &other) const {
		return scalar == other.scalar && epoch_seconds == other.epoch_seconds;
	}
	bool operator!=(const OmTimeCoordinate &other) const {
		return !(*this == other);
	}
};

struct MetadataVariable final {
	std::string canonical_path;
	std::vector<std::string> inferred_axes;
	std::optional<OmTimeCoordinate> time;
	std::vector<std::uint64_t> shape;
	std::vector<std::uint64_t> chunk_shape;
	std::uint64_t row_count = 0;
	std::uint64_t metadata_offset = 0;
	std::uint64_t metadata_size = 0;
	std::shared_ptr<const OwnedMetadataBuffer> metadata_owner;
};

struct OmMetadataTree final {
	std::vector<MetadataVariable> arrays;
	std::string crs_wkt;
};

// Reads and validates all metadata nodes through the official OM v3 reader.
// Float32 arrays are value columns; their descendants and scalar siblings are metadata.
// Int64 time coordinate arrays are decoded as UTC Unix seconds, never value columns.
OmMetadataTree ReadMetadataTree(const OmV3Reader &reader);

// Encodes a single metadata name segment as a canonical OM path segment.
std::string EncodeMetadataName(const std::string &name);

} // namespace duckomo
} // namespace duckdb
