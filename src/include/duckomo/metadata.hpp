#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "duckomo/reader.hpp"

namespace duckdb {
namespace duckomo {

struct MetadataVariable final {
	std::string canonical_path;
	std::vector<std::string> inferred_axes;
	std::vector<std::uint64_t> shape;
	std::vector<std::uint64_t> chunk_shape;
	std::uint64_t row_count = 0;
	std::uint64_t metadata_offset = 0;
	std::uint64_t metadata_size = 0;
	std::shared_ptr<const OwnedMetadataBuffer> metadata_owner;
};

struct OmMetadataTree final {
	std::vector<MetadataVariable> arrays;
};

// Reads and validates all metadata nodes through the official OM v3 reader.
// Float32 arrays are value columns; their descendants and scalar siblings are metadata.
OmMetadataTree ReadMetadataTree(const OmV3Reader &reader);

// Encodes a single metadata name segment as a canonical OM path segment.
std::string EncodeMetadataName(const std::string &name);

} // namespace duckomo
} // namespace duckdb
