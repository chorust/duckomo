#include "duckomo/region_input.hpp"
#include "duckomo/compat/duckdb_api.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/exception/binder_exception.hpp"

namespace duckdb {
namespace duckomo {
namespace {

struct ParsedWkb final {
	RegionEnvelope envelope;
	std::uint64_t vertices = 0;
	std::uint64_t geometry_bytes = 0;
};

std::string Ordinal(std::uint64_t ordinal) {
	return "region ordinal " + std::to_string(ordinal + 1) + ": ";
}

[[noreturn]] void InputError(std::uint64_t ordinal, const std::string &detail) {
	throw BinderException("[region_input] " + Ordinal(ordinal) + detail);
}

[[noreturn]] void LimitError(std::uint64_t ordinal, const std::string &detail) {
	throw BinderException("[region_input_limit] " + Ordinal(ordinal) + detail);
}

[[noreturn]] void CollectionInputError(const std::string &detail) {
	throw BinderException("[region_input] " + detail);
}

[[noreturn]] void CollectionLimitError(const std::string &detail) {
	throw BinderException("[region_input_limit] " + detail);
}

bool ValidUtf8(const std::string &text) noexcept {
	const auto *data = reinterpret_cast<const unsigned char *>(text.data());
	std::size_t index = 0;
	while (index < text.size()) {
		const auto first = data[index++];
		if (first <= 0x7f) continue;
		std::uint32_t codepoint = 0;
		std::size_t continuation = 0;
		if (first >= 0xc2 && first <= 0xdf) {
			codepoint = first & 0x1f;
			continuation = 1;
		} else if (first >= 0xe0 && first <= 0xef) {
			codepoint = first & 0x0f;
			continuation = 2;
		} else if (first >= 0xf0 && first <= 0xf4) {
			codepoint = first & 0x07;
			continuation = 3;
		} else {
			return false;
		}
		if (continuation > text.size() - index) return false;
		for (std::size_t i = 0; i < continuation; ++i) {
			const auto next = data[index++];
			if ((next & 0xc0) != 0x80) return false;
			codepoint = (codepoint << 6) | (next & 0x3f);
		}
		if ((continuation == 1 && codepoint < 0x80) || (continuation == 2 && codepoint < 0x800) ||
		    (continuation == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
		    (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
			return false;
		}
	}
	return true;
}

class WkbReader final {
public:
	WkbReader(const std::string &bytes, std::uint64_t ordinal, const std::function<void()> &check_interrupt)
	    : bytes(bytes), ordinal(ordinal), check_interrupt(check_interrupt) {
	}

	ParsedWkb Parse() {
		if (check_interrupt) check_interrupt();
		const auto [little_endian, type] = ReadHeader();
		if (type == 3) {
			ParsePolygonBody(little_endian);
		} else if (type == 6) {
			const auto polygon_count = ReadU32(little_endian);
			if (polygon_count > Remaining() / 9) InputError(ordinal, "MultiPolygon count exceeds the remaining WKB bytes");
			for (std::uint32_t i = 0; i < polygon_count; ++i) {
				const auto [polygon_little_endian, polygon_type] = ReadHeader();
				if (polygon_type != 3) InputError(ordinal, "MultiPolygon children must be standard 2D Polygon values");
				ParsePolygonBody(polygon_little_endian);
			}
		} else {
			InputError(ordinal, "only standard 2D Polygon and MultiPolygon WKB are accepted");
		}
		if (position != bytes.size()) InputError(ordinal, "WKB has trailing bytes");
		if (!result.envelope.empty && result.envelope.max_longitude - result.envelope.min_longitude >= 180.0) {
			InputError(ordinal, "non-empty geometry longitude span must be less than 180 degrees");
		}
		std::uint64_t coordinate_bytes = 0;
		if (result.vertices > (std::numeric_limits<std::uint64_t>::max() - bytes.size() - 64) / 16) {
			LimitError(ordinal, "opaque geometry size overflows the supported range");
		}
		coordinate_bytes = result.vertices * 16;
		result.geometry_bytes = static_cast<std::uint64_t>(bytes.size()) + coordinate_bytes + 64;
		if (result.geometry_bytes > REGION_MAX_GEOMETRY_BYTES) {
			LimitError(ordinal, "opaque GEOMETRY allocation exceeds the 2 MiB per-region limit");
		}
		return result;
	}

private:
	std::pair<bool, std::uint32_t> ReadHeader() {
		const auto order = ReadByte();
		if (order > 1) InputError(ordinal, "WKB byte order must be 0 or 1");
		const bool little_endian = order == 1;
		return {little_endian, ReadU32(little_endian)};
	}

	std::uint8_t ReadByte() {
		if (position >= bytes.size()) InputError(ordinal, "WKB is truncated");
		return static_cast<std::uint8_t>(static_cast<unsigned char>(bytes[position++]));
	}

	std::uint32_t ReadU32(bool little_endian) {
		if (Remaining() < sizeof(std::uint32_t)) InputError(ordinal, "WKB is truncated");
		std::uint32_t value = 0;
		for (unsigned i = 0; i < 4; ++i) {
			const auto shift = little_endian ? i * 8 : (3 - i) * 8;
			value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[position++])) << shift;
		}
		return value;
	}

	double ReadDouble(bool little_endian) {
		if (Remaining() < sizeof(double)) InputError(ordinal, "WKB coordinate is truncated");
		std::uint64_t bits = 0;
		for (unsigned i = 0; i < 8; ++i) {
			const auto shift = little_endian ? i * 8 : (7 - i) * 8;
			bits |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[position++])) << shift;
		}
		double value;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

	std::size_t Remaining() const noexcept {
		return bytes.size() - position;
	}

	void AddPoint(bool little_endian, std::uint64_t point_index, double &first_x, double &first_y,
	              double &previous_x, double &previous_y) {
		const auto longitude = ReadDouble(little_endian);
		const auto latitude = ReadDouble(little_endian);
		if (!std::isfinite(longitude) || !std::isfinite(latitude)) {
			InputError(ordinal, "coordinates must be finite");
		}
		if (longitude < -180.0 || longitude > 180.0 || latitude <= -90.0 || latitude >= 90.0) {
			InputError(ordinal, "coordinates must use longitude [-180,180] and latitude (-90,90)");
		}
		if (point_index != 0 && std::abs(longitude - previous_x) >= 180.0) {
			InputError(ordinal, "adjacent ring longitudes must differ by less than 180 degrees");
		}
		if (point_index == 0) {
			first_x = longitude;
			first_y = latitude;
		}
		previous_x = longitude;
		previous_y = latitude;
		if (result.envelope.empty) {
			result.envelope = {false, longitude, latitude, longitude, latitude};
		} else {
			result.envelope.min_longitude = std::min(result.envelope.min_longitude, longitude);
			result.envelope.min_latitude = std::min(result.envelope.min_latitude, latitude);
			result.envelope.max_longitude = std::max(result.envelope.max_longitude, longitude);
			result.envelope.max_latitude = std::max(result.envelope.max_latitude, latitude);
		}
		++result.vertices;
		if ((result.vertices & 255U) == 0 && check_interrupt) check_interrupt();
	}

	void ParsePolygonBody(bool little_endian) {
		const auto ring_count = ReadU32(little_endian);
		if (ring_count > Remaining() / sizeof(std::uint32_t)) {
			InputError(ordinal, "Polygon ring count exceeds the remaining WKB bytes");
		}
		for (std::uint32_t ring = 0; ring < ring_count; ++ring) {
			const auto point_count = ReadU32(little_endian);
			if (point_count == 0) continue;
			if (point_count < 4) InputError(ordinal, "every non-empty ring must contain at least four points");
			if (point_count > REGION_MAX_VERTICES - result.vertices) {
				LimitError(ordinal, "vertex count exceeds the 65536 per-region limit");
			}
			if (point_count > Remaining() / (2 * sizeof(double))) {
				InputError(ordinal, "ring point count exceeds the remaining WKB bytes");
			}
			double first_x = 0, first_y = 0, previous_x = 0, previous_y = 0;
			for (std::uint32_t point = 0; point < point_count; ++point) {
				AddPoint(little_endian, point, first_x, first_y, previous_x, previous_y);
			}
			if (previous_x != first_x || previous_y != first_y) {
				InputError(ordinal, "rings must be closed with an exact copy of the first coordinate");
			}
		}
	}

	const std::string &bytes;
	std::uint64_t ordinal;
	const std::function<void()> &check_interrupt;
	std::size_t position = 0;
	ParsedWkb result;
};

void ValidateStructType(const LogicalType &type, idx_t &id_index, idx_t &wkb_index) {
	if (type.id() != LogicalTypeId::STRUCT || StructType::GetChildCount(type) != 2) {
		CollectionInputError("regions list elements must be STRUCT(id VARCHAR, wkb BLOB)");
	}
	for (idx_t i = 0; i < 2; ++i) {
		const auto name = IdentifierNameString(StructType::GetChildName(type, i));
		if (name == "id" && id_index == DConstants::INVALID_INDEX) id_index = i;
		else if (name == "wkb" && wkb_index == DConstants::INVALID_INDEX) wkb_index = i;
		else CollectionInputError("regions STRUCT field names must be exactly id and wkb with exact spelling");
	}
	if (id_index == DConstants::INVALID_INDEX || wkb_index == DConstants::INVALID_INDEX ||
	    StructType::GetChildType(type, id_index).id() != LogicalTypeId::VARCHAR ||
	    StructType::GetChildType(type, wkb_index).id() != LogicalTypeId::BLOB) {
		CollectionInputError("regions fields must have exact types id VARCHAR and wkb BLOB");
	}
}

std::uint64_t CheckedAdd(std::uint64_t left, std::uint64_t right, const std::string &message) {
	if (right > std::numeric_limits<std::uint64_t>::max() - left) CollectionLimitError(message);
	return left + right;
}

} // namespace

RegionCollection RegionCollection::Build(const Value &input, const std::function<void()> &check_interrupt) {
	if (input.IsNull() || input.type().id() != LogicalTypeId::LIST) {
		CollectionInputError("regions must be a non-NULL LIST of STRUCT(id VARCHAR, wkb BLOB)");
	}
	idx_t id_index = DConstants::INVALID_INDEX;
	idx_t wkb_index = DConstants::INVALID_INDEX;
	ValidateStructType(ListType::GetChildType(input.type()), id_index, wkb_index);
	const auto &input_regions = ListValue::GetChildren(input);
	if (input_regions.size() > REGION_MAX_COUNT) {
		CollectionLimitError("regions count exceeds the 4096 item limit");
	}

	std::uint64_t total_wkb_bytes = 0;
	std::uint64_t estimated_owned_bytes = sizeof(RegionCollection) + input_regions.size() * sizeof(Region);
	for (idx_t i = 0; i < input_regions.size(); ++i) {
		const auto &value = input_regions[i];
		if (value.IsNull()) InputError(i, "region element must not be NULL");
		const auto &children = StructValue::GetChildren(value);
		if (children.size() != 2) InputError(i, "region element must contain exactly id and wkb fields");
		const auto &id = children[id_index];
		const auto &wkb = children[wkb_index];
		if (id.IsNull() || wkb.IsNull()) InputError(i, "id and wkb fields must not be NULL");
		const auto &id_bytes = StringValue::Get(id);
		const auto &wkb_bytes = StringValue::Get(wkb);
		if (id_bytes.empty() || id_bytes.size() > 256 || !ValidUtf8(id_bytes)) {
			InputError(i, "id must be 1–256 bytes of valid UTF-8");
		}
		if (wkb_bytes.size() > REGION_MAX_WKB_BYTES) {
			LimitError(i, "WKB exceeds the 1 MiB per-region byte limit");
		}
		total_wkb_bytes = CheckedAdd(total_wkb_bytes, wkb_bytes.size(), "total WKB size overflows");
		if (total_wkb_bytes > REGION_MAX_TOTAL_WKB_BYTES) {
			LimitError(i, "total WKB exceeds the 16 MiB collection byte limit");
		}
		estimated_owned_bytes = CheckedAdd(estimated_owned_bytes, id_bytes.size(), "owned input size overflows");
		estimated_owned_bytes = CheckedAdd(estimated_owned_bytes, wkb_bytes.size(), "owned input size overflows");
	}
	if (estimated_owned_bytes > REGION_MAX_OWNED_BYTES) {
		CollectionLimitError("owned input exceeds the 64 MiB collection limit");
	}

	std::unordered_set<std::string> ids;
	ids.reserve(input_regions.size());
	std::vector<ParsedWkb> parsed;
	parsed.reserve(input_regions.size());
	std::uint64_t total_vertices = 0;
	for (idx_t i = 0; i < input_regions.size(); ++i) {
		const auto &children = StructValue::GetChildren(input_regions[i]);
		const auto &id = StringValue::Get(children[id_index]);
		if (!ids.emplace(id).second) InputError(i, "id must be unique by exact UTF-8 bytes");
		const auto &wkb = StringValue::Get(children[wkb_index]);
		auto item = WkbReader(wkb, i, check_interrupt).Parse();
		if (item.vertices > REGION_MAX_TOTAL_VERTICES - total_vertices) {
			LimitError(i, "vertex count exceeds the 1048576 collection limit");
		}
		total_vertices += item.vertices;
		estimated_owned_bytes = CheckedAdd(estimated_owned_bytes, item.geometry_bytes, "owned region storage size overflows");
		if (estimated_owned_bytes > REGION_MAX_OWNED_BYTES) {
			LimitError(i, "owned region storage exceeds the 64 MiB collection limit");
		}
		parsed.push_back(item);
	}

	RegionCollection result;
	result.regions.reserve(input_regions.size());
	result.total_vertices = total_vertices;
	for (idx_t i = 0; i < input_regions.size(); ++i) {
		const auto &children = StructValue::GetChildren(input_regions[i]);
		const auto &id = StringValue::Get(children[id_index]);
		const auto &wkb = StringValue::Get(children[wkb_index]);
		Region region;
		region.id = id;
		region.wkb.assign(wkb.begin(), wkb.end());
		region.envelope = parsed[i].envelope;
		region.vertex_count = parsed[i].vertices;
		region.geometry_bytes = parsed[i].geometry_bytes;
		result.regions.push_back(std::move(region));
	}
	std::uint64_t owned_bytes = sizeof(result) + result.regions.capacity() * sizeof(Region);
	for (const auto &region : result.regions) {
		owned_bytes = CheckedAdd(owned_bytes, region.id.capacity(), "owned input size overflows");
		owned_bytes = CheckedAdd(owned_bytes, region.wkb.capacity(), "owned input size overflows");
		owned_bytes = CheckedAdd(owned_bytes, region.geometry_bytes, "owned region storage size overflows");
	}
	result.owned_bytes = owned_bytes;
	result.ValidateOwnedBytes();
	return result;
}

std::uint64_t RegionCollection::ValidateOwnedBytes(std::uint64_t binding_bytes) const {
	const auto total = CheckedAdd(owned_bytes, binding_bytes, "owned region storage size overflows");
	if (total > REGION_MAX_OWNED_BYTES) {
		CollectionLimitError("owned region storage exceeds the 64 MiB collection limit");
	}
	return total;
}

bool RegionCollection::Equals(const RegionCollection &other) const noexcept {
	if (regions.size() != other.regions.size()) return false;
	for (idx_t i = 0; i < regions.size(); ++i) {
		if (regions[i].id != other.regions[i].id || regions[i].wkb != other.regions[i].wkb) return false;
	}
	return true;
}

void ValidateRegionCrs(const Value &input) {
	if (input.IsNull() || input.type().id() != LogicalTypeId::VARCHAR || StringValue::Get(input) != "grid_lonlat") {
		throw BinderException("[region_reference] region_crs must be the exact string 'grid_lonlat'");
	}
}

} // namespace duckomo
} // namespace duckdb
