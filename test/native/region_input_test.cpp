#include "duckomo/region_input.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/main/extension_helper.hpp"

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	(void)database;
}
} // namespace duckdb

namespace {
using namespace duckdb;
using namespace duckdb::duckomo;

struct Point final {
	double x;
	double y;
};

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

void U32(std::string &out, std::uint32_t value, bool little_endian) {
	for (unsigned i = 0; i < 4; ++i) {
		const auto shift = little_endian ? i * 8 : (3 - i) * 8;
		out.push_back(static_cast<char>((value >> shift) & 0xff));
	}
}

void F64(std::string &out, double value, bool little_endian) {
	std::uint64_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	for (unsigned i = 0; i < 8; ++i) {
		const auto shift = little_endian ? i * 8 : (7 - i) * 8;
		out.push_back(static_cast<char>((bits >> shift) & 0xff));
	}
}

std::string PolygonWkb(const std::vector<Point> &ring, bool little_endian = true) {
	std::string out(1, little_endian ? '\x01' : '\x00');
	U32(out, 3, little_endian);
	U32(out, 1, little_endian);
	U32(out, static_cast<std::uint32_t>(ring.size()), little_endian);
	for (const auto &point : ring) {
		F64(out, point.x, little_endian);
		F64(out, point.y, little_endian);
	}
	return out;
}

std::string EmptyWkb(std::uint32_t type = 3, bool little_endian = true) {
	std::string out(1, little_endian ? '\x01' : '\x00');
	U32(out, type, little_endian);
	U32(out, 0, little_endian);
	return out;
}

std::string RepeatedRingWkb(std::uint32_t point_count) {
	std::string out(1, '\x01');
	U32(out, 3, true);
	U32(out, 1, true);
	U32(out, point_count, true);
	for (std::uint32_t i = 0; i < point_count; ++i) {
		F64(out, 0.5, true);
		F64(out, 0.5, true);
	}
	return out;
}

std::string MultiPolygonWkb(const std::string &polygon_wkb) {
	std::string out(1, '\x01');
	U32(out, 6, true);
	U32(out, 1, true);
	return out + polygon_wkb;
}

LogicalType RegionStructType(bool reversed = false, bool wrong_id_type = false, bool extra = false) {
	child_list_t<LogicalType> fields;
	if (reversed) {
		fields.emplace_back("wkb", LogicalType(LogicalTypeId::BLOB));
		fields.emplace_back("id", LogicalType(wrong_id_type ? LogicalTypeId::INTEGER : LogicalTypeId::VARCHAR));
	} else {
		fields.emplace_back("id", LogicalType(wrong_id_type ? LogicalTypeId::INTEGER : LogicalTypeId::VARCHAR));
		fields.emplace_back("wkb", LogicalType(LogicalTypeId::BLOB));
	}
	if (extra) fields.emplace_back("unused", LogicalType(LogicalTypeId::VARCHAR));
	return LogicalType::STRUCT(fields);
}

Value RawBlob(const std::string &bytes) {
	return Value::BLOB(reinterpret_cast<const_data_ptr_t>(bytes.data()), bytes.size());
}

Value RegionValue(const LogicalType &type, std::string id, std::string wkb) {
	if (StructType::GetChildName(type, 0) == "wkb") {
		return Value::STRUCT(type, {RawBlob(wkb), Value(std::move(id))});
	}
	return Value::STRUCT(type, {Value(std::move(id)), RawBlob(wkb)});
}

Value RegionList(std::vector<std::pair<std::string, std::string>> values, bool reversed = false) {
	const auto type = RegionStructType(reversed);
	std::vector<Value> children;
	children.reserve(values.size());
	for (auto &item : values) children.push_back(RegionValue(type, std::move(item.first), std::move(item.second)));
	return Value::LIST(type, std::move(children));
}

void RequireRegionError(const Value &input, const std::string &tag, const std::string &message) {
	try {
		(void)RegionCollection::Build(input);
	} catch (const Exception &error) {
		const auto text = std::string(error.what());
		Require(text.find(tag) != std::string::npos, message + ": wrong error tag: " + text);
		return;
	}
	throw std::runtime_error(message + ": expected rejection");
}

void RequireCrsError(const Value &input, const std::string &message) {
	try {
		ValidateRegionCrs(input);
	} catch (const Exception &error) {
		Require(std::string(error.what()).find("[region_reference]") != std::string::npos,
		        message + ": wrong error category");
		return;
	}
	throw std::runtime_error(message + ": expected rejection");
}

void TestOwnedCopyEmptyAndEndian() {
	const std::vector<Point> ring{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
	const auto polygon_wkb = PolygonWkb(ring, false);
	auto input = RegionList({{"west", polygon_wkb}, {"multi", MultiPolygonWkb(PolygonWkb(ring))},
	                         {"empty", EmptyWkb(6)}} , true);
	const auto collection = RegionCollection::Build(input);
	Require(collection.GetRegions().size() == 3 && collection.GetTotalVertices() == 10,
	        "field order is irrelevant and big-endian Polygon, MultiPolygon, and EMPTY parse");
	const auto &polygon = collection.GetRegions()[0];
	Require(polygon.envelope.min_longitude == 0 && polygon.envelope.max_longitude == 1 &&
	            polygon.envelope.min_latitude == 0 && polygon.envelope.max_latitude == 1 && !polygon.envelope.empty,
	        "the structural parser records the exact coordinate envelope");
	Require(collection.GetRegions()[2].envelope.empty && collection.GetRegions()[2].vertex_count == 0,
	        "a typed empty geometry remains distinct from an empty input collection");
	Require(collection.GetOwnedBytes() >= collection.GetRegions()[0].wkb.size() + 9,
	        "the immutable result owns a byte-for-byte WKB copy");
	input = Value(LogicalType::LIST(RegionStructType(true)));
	Require(collection.GetRegions()[0].wkb.size() == PolygonWkb(ring, false).size(),
	        "mutating the source Value does not change the owned region bytes");
	const auto empty = RegionCollection::Build(Value::LIST(RegionStructType(), {}));
	Require(empty.GetRegions().empty() && empty.GetTotalVertices() == 0,
	        "a typed empty list is accepted after full type validation");
	for (const bool little_endian : {false, true}) {
		const auto empty_ring = PolygonWkb({}, little_endian);
		const auto ring_collection = RegionCollection::Build(
		    RegionList({{"empty-ring", empty_ring}, {"empty-component", MultiPolygonWkb(empty_ring)}}));
		Require(ring_collection.GetTotalVertices() == 0 &&
		            ring_collection.GetRegions()[0].envelope.empty && ring_collection.GetRegions()[1].envelope.empty,
		        "zero-point Polygon rings and empty MultiPolygon components have no coordinate envelope");
	}
	for (const std::vector<Point> short_ring : {std::vector<Point>{{0, 0}},
	                                          std::vector<Point>{{0, 0}, {0, 0}},
	                                          std::vector<Point>{{0, 0}, {1, 0}, {0, 0}}}) {
		RequireRegionError(RegionList({{"short-ring", PolygonWkb(short_ring)}}), "[region_input]",
		                   "non-empty rings with fewer than four points remain invalid");
	}
}

void TestStrictShapeIdentityAndUtf8() {
	const auto polygon = PolygonWkb({{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}});
	ValidateRegionCrs(Value("grid_lonlat"));
	RequireCrsError(Value("GRID_LONLAT"), "CRS declaration is exact and case-sensitive");
	RequireCrsError(Value(LogicalType(LogicalTypeId::VARCHAR)), "NULL CRS declaration is rejected");
	RequireCrsError(Value::INTEGER(1), "non-VARCHAR CRS declaration is rejected");
	RequireRegionError(Value::LIST(LogicalType(LogicalTypeId::VARCHAR), {Value("not regions")}), "[region_input]",
	                   "non-STRUCT list child type is rejected");
	RequireRegionError(Value(LogicalType::LIST(RegionStructType())), "[region_input]", "NULL list is rejected");
	RequireRegionError(RegionList({{"same", polygon}, {"same", EmptyWkb()}}), "[region_input]",
	                   "duplicate identifiers reject geometrically different values");
	Require(RegionCollection::Build(RegionList({{"copy-a", polygon}, {"copy-b", polygon}}))
	            .GetRegions().size() == 2,
	        "same geometry under distinct identifiers remains two ordered region inputs");
	RequireRegionError(RegionList({{"", polygon}}), "[region_input]", "empty identifier is rejected");
	RequireRegionError(RegionList({{std::string(257, 'x'), polygon}}), "[region_input]",
	                   "identifier over 256 UTF-8 bytes is rejected");
	const auto wrong_type = RegionStructType(false, true);
	RequireRegionError(Value::LIST(wrong_type, {Value::STRUCT(wrong_type, {Value::INTEGER(1), RawBlob(polygon)})}),
	                   "[region_input]", "field types are exact");
	const auto extra_type = RegionStructType(false, false, true);
	RequireRegionError(Value::LIST(extra_type, {Value::STRUCT(extra_type,
	                   {Value("id"), RawBlob(polygon), Value("extra")})}), "[region_input]",
	                   "extra fields are rejected");
	const auto wrong_case = LogicalType::STRUCT({{"ID", LogicalType(LogicalTypeId::VARCHAR)},
	                                              {"wkb", LogicalType(LogicalTypeId::BLOB)}});
	RequireRegionError(Value::LIST(wrong_case, {Value::STRUCT(wrong_case,
	                   {Value("id"), RawBlob(polygon)})}), "[region_input]", "field spelling is exact");
	const auto nullable = RegionStructType();
	RequireRegionError(Value::LIST(nullable, {Value(nullable)}), "[region_input]", "NULL region is rejected");
	RequireRegionError(Value::LIST(nullable, {Value::STRUCT(nullable,
	                   {Value(LogicalType(LogicalTypeId::VARCHAR)), RawBlob(polygon)})}),
	                   "[region_input]", "NULL id is rejected");
	RequireRegionError(Value::LIST(nullable, {Value::STRUCT(nullable,
	                   {Value("id"), Value(LogicalType(LogicalTypeId::BLOB))})}),
	                   "[region_input]", "NULL WKB is rejected");
	const auto valid_utf8 = RegionCollection::Build(RegionList({{"\xf0\x9f\x8c\x8d", polygon}}));
	Require(valid_utf8.GetRegions().size() == 1, "valid four-byte UTF-8 is accepted");
}

void TestWkbStructuralAndCoordinateRejections() {
	const auto valid = PolygonWkb({{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}});
	const auto near_range_edge = PolygonWkb({{-180, -89.0}, {-179, -89.0}, {-179, std::nextafter(90.0, 0.0)},
	                                         {-180, std::nextafter(90.0, 0.0)}, {-180, -89.0}});
	Require(RegionCollection::Build(RegionList({{"range-edge", near_range_edge}})).GetRegions().size() == 1,
	        "inclusive longitude endpoints and latitudes strictly below the poles are accepted");
	auto trailing = valid;
	trailing.push_back('\0');
	auto truncated = valid;
	truncated.pop_back();
	auto wrong_type = valid;
	wrong_type[1] = 2;
	auto z_type = valid;
	z_type.replace(1, 4, std::string("\xeb\x03\0\0", 4));
	auto m_type = valid;
	m_type.replace(1, 4, std::string("\xd3\x07\0\0", 4));
	auto ewkb = valid;
	ewkb.replace(1, 4, std::string("\x03\0\0\x20", 4));
	const auto nested_multi = MultiPolygonWkb(EmptyWkb(6));
	const auto unclosed = PolygonWkb({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
	const auto too_few = PolygonWkb({{0, 0}, {1, 0}, {0, 0}});
	const auto seam = PolygonWkb({{-180, 0}, {180, 0}, {180, 1}, {-180, 1}, {-180, 0}});
	const auto pole = PolygonWkb({{0, 90}, {1, 90}, {1, 89}, {0, 89}, {0, 90}});
	const auto nonfinite = PolygonWkb({{0, 0}, {1, 0}, {1, std::numeric_limits<double>::infinity()},
	                                   {0, 1}, {0, 0}});
	for (const auto &item : std::vector<std::pair<std::string, std::string>>{
	         {"trailing", trailing}, {"truncated", truncated}, {"non-area", wrong_type}, {"Z", z_type},
	         {"M", m_type}, {"EWKB", ewkb}, {"nested MultiPolygon", nested_multi},
	         {"unclosed", unclosed}, {"short ring", too_few}, {"seam", seam},
	         {"pole", pole}, {"non-finite", nonfinite}}) {
		RequireRegionError(RegionList({{"x", item.second}}), "[region_input]", item.first + " WKB is rejected");
	}
	RequireRegionError(RegionList({{"empty-first", EmptyWkb()}, {"invalid-later", truncated}}), "[region_input]",
	                   "a leading EMPTY does not short-circuit whole-set validation");
}

void TestLimitsAndCancellation() {
	std::vector<std::pair<std::string, std::string>> max_regions;
	max_regions.reserve(REGION_MAX_COUNT);
	for (std::uint64_t i = 0; i < REGION_MAX_COUNT; ++i) max_regions.emplace_back("r" + std::to_string(i), EmptyWkb());
	Require(RegionCollection::Build(RegionList(std::move(max_regions))).GetRegions().size() == REGION_MAX_COUNT,
	        "exactly 4096 EMPTY regions fit the count limit");
	std::vector<std::pair<std::string, std::string>> too_many;
	too_many.reserve(REGION_MAX_COUNT + 1);
	for (std::uint64_t i = 0; i <= REGION_MAX_COUNT; ++i) too_many.emplace_back("r" + std::to_string(i), EmptyWkb());
	RequireRegionError(RegionList(std::move(too_many)), "[region_input_limit]", "4097 regions exceed the count limit");
	RequireRegionError(RegionList({{"large", std::string(REGION_MAX_WKB_BYTES + 1, '\0')}}),
	                   "[region_input_limit]", "per-region WKB byte cap is enforced before parsing");
	Require(RegionCollection::Build(RegionList({{"near-geometry-cap", RepeatedRingWkb(65533)}}))
	            .GetRegions()[0].geometry_bytes <= REGION_MAX_GEOMETRY_BYTES,
	        "opaque geometry estimate remains within the 2 MiB cap at its valid edge");
	RequireRegionError(RegionList({{"over-geometry-cap", RepeatedRingWkb(65534)}}),
	                   "[region_input_limit]", "opaque geometry estimate cannot exceed 2 MiB");

	std::vector<std::pair<std::string, std::string>> total_wkb;
	total_wkb.reserve(17);
	for (std::uint32_t i = 0; i < 17; ++i) total_wkb.emplace_back("large-" + std::to_string(i), RepeatedRingWkb(61681));
	RequireRegionError(RegionList(std::move(total_wkb)), "[region_input_limit]",
	                   "the 16 MiB collection WKB cap is checked before parsing or copying");

	std::string long_ring(1, '\x01');
	U32(long_ring, 3, true);
	U32(long_ring, 1, true);
	U32(long_ring, 512, true);
	for (std::uint32_t i = 0; i < 512; ++i) {
		F64(long_ring, 0.5, true);
		F64(long_ring, 0.5, true);
	}
	std::uint32_t checks = 0;
	RegionCollection::Build(RegionList({{"long", long_ring}}), [&] { ++checks; });
	Require(checks >= 2, "the structural walk checks cancellation at most every 256 vertices");
	checks = 0;
	bool interrupted = false;
	try {
		(void)RegionCollection::Build(RegionList({{"long", long_ring}}), [&] {
			if (++checks == 2) throw InterruptException();
		});
	} catch (const InterruptException &) {
		interrupted = true;
	}
	Require(interrupted && checks == 2, "WKB traversal propagates cancellation at its 256-vertex check");
}

} // namespace

int main() {
	try {
		TestOwnedCopyEmptyAndEndian();
		TestStrictShapeIdentityAndUtf8();
		TestWkbStructuralAndCoordinateRejections();
		TestLimitsAndCancellation();
		std::cout << "region_input_test: ok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "region_input_test: " << error.what() << '\n';
		return 1;
	}
}
