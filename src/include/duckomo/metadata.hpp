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
	std::shared_ptr<ScanMetrics> memory_metrics;
	std::shared_ptr<ScanMemoryAccount> memory_account;

	OmTimeCoordinate() = default;
	OmTimeCoordinate(const OmTimeCoordinate &other)
	    : epoch_seconds(other.epoch_seconds), scalar(other.scalar), memory_metrics(other.memory_metrics) {
		RefreshMemoryAccount();
	}
	OmTimeCoordinate &operator=(const OmTimeCoordinate &other) {
		if (this != &other) {
			epoch_seconds = other.epoch_seconds;
			scalar = other.scalar;
			memory_metrics = other.memory_metrics;
			RefreshMemoryAccount();
		}
		return *this;
	}
	OmTimeCoordinate(OmTimeCoordinate &&) noexcept = default;
	OmTimeCoordinate &operator=(OmTimeCoordinate &&) noexcept = default;

	void EnableMemoryAccounting(const std::shared_ptr<ScanMetrics> &metrics) {
		memory_metrics = metrics;
		RefreshMemoryAccount();
	}
	void RefreshMemoryAccount() {
		if (!memory_metrics) return;
		if (!memory_account) {
			memory_account = std::make_shared<ScanMemoryAccount>(memory_metrics,
			                                                    ScanMemoryComponent::CoordinateBuffers);
		}
		const auto capacity = static_cast<std::uint64_t>(epoch_seconds.capacity());
		const auto payload = capacity > UINT64_MAX / sizeof(std::int64_t)
		                        ? UINT64_MAX
		                        : capacity * sizeof(std::int64_t);
		memory_account->Set(payload);
	}
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

// A source CRS profile is only promoted after its complete WKT structure,
// earth model, axis order, and angular unit match a closed profile. Unknown
// WKT remains preserved separately and is never interpreted heuristically.
enum class SourceCrsProfileKind : std::uint8_t { NotSpecified, Unrecognized, ReducedGaussianWgs84V1 };

struct SourceCrsProfile final {
	SourceCrsProfileKind kind = SourceCrsProfileKind::NotSpecified;
	// ECMWF's recognized "Reduced Gaussian Grid O<n> (ECMWF)" remark fixes
	// the Gaussian order. The CRS geometry is checked against this when present.
	std::optional<std::uint64_t> gaussian_order;

	bool operator==(const SourceCrsProfile &other) const noexcept {
		return kind == other.kind && gaussian_order == other.gaussian_order;
	}
	bool operator!=(const SourceCrsProfile &other) const noexcept { return !(*this == other); }
};

struct OmMetadataTree final {
	std::vector<MetadataVariable> arrays;
	std::string crs_wkt;
	SourceCrsProfile crs_profile;
};

// Reads and validates all metadata nodes through the official OM v3 reader.
// Float32 arrays are value columns; their descendants and scalar siblings are metadata.
// Int64 time coordinate arrays are decoded as UTC Unix seconds, never value columns.
OmMetadataTree ReadMetadataTree(const OmV3Reader &reader);

// Encodes a single metadata name segment as a canonical OM path segment.
std::string EncodeMetadataName(const std::string &name);

} // namespace duckomo
} // namespace duckdb
