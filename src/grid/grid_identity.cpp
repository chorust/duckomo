#include "duckomo/grid_identity.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <vector>

#include "duckomo/om_reader.hpp"
#include "duckomo/projected_grid.hpp"
#include "duckomo/spatial_layout.hpp"
#include "mbedtls_wrapper.hpp"

namespace duckdb {
namespace duckomo {
namespace {

void AppendU64(std::string &target, std::uint64_t value) {
	for (int shift = 56; shift >= 0; shift -= 8) {
		target.push_back(static_cast<char>((value >> shift) & 0xffU));
	}
}

void AppendSized(std::string &target, const std::string &value) {
	AppendU64(target, static_cast<std::uint64_t>(value.size()));
	target.append(value);
}

std::string IntegerText(std::uint64_t value) {
	return std::to_string(value);
}

double CanonicalReal(double value, GridNumericPolicy policy) {
	if (!std::isfinite(value)) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "grid identity cannot encode a non-finite number");
	}
	if (policy == GridNumericPolicy::OpenMeteoF32V1) {
		const float rounded = static_cast<float>(value);
		if (!std::isfinite(rounded)) {
			throw ReaderError(ReaderErrorCode::InvalidShape, "grid identity Float32 parameter overflows");
		}
		value = static_cast<double>(rounded);
	} else if (policy != GridNumericPolicy::Float64V1) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "grid identity numeric policy is unsupported");
	}
	return value == 0 ? 0 : value;
}

std::string RealBits(double value, GridNumericPolicy policy) {
	value = CanonicalReal(value, policy);
	std::uint64_t bits = 0;
	static_assert(sizeof(bits) == sizeof(value), "binary64 is required for canonical grid identity");
	std::memcpy(&bits, &value, sizeof(bits));
	std::ostringstream output;
	output << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << bits;
	return output.str();
}

class CanonicalEncoder final {
public:
	explicit CanonicalEncoder(std::string format) { String("format", format); }

	void String(const std::string &name, const std::string &value) { Field(name, 's', value); }
	void Integer(const std::string &name, std::uint64_t value) { Field(name, 'u', IntegerText(value)); }
	void Boolean(const std::string &name, bool value) { Field(name, 'b', value ? "1" : "0"); }
	void Real(const std::string &name, double value, GridNumericPolicy policy) {
		Field(name, 'f', RealBits(value, policy));
	}

	void StringArray(const std::string &name, const std::vector<std::string> &values) {
		std::string payload;
		AppendU64(payload, static_cast<std::uint64_t>(values.size()));
		for (const auto &value : values) AppendSized(payload, value);
		Field(name, 'a', payload);
	}

	void IntegerArray(const std::string &name, const std::vector<std::uint64_t> &values) {
		std::string payload;
		AppendU64(payload, static_cast<std::uint64_t>(values.size()));
		for (const auto value : values) AppendSized(payload, IntegerText(value));
		Field(name, 'A', payload);
	}

	void RecordArray(const std::string &name, const std::vector<std::string> &values) {
		std::string payload;
		AppendU64(payload, static_cast<std::uint64_t>(values.size()));
		for (const auto &value : values) AppendSized(payload, value);
		Field(name, 'r', payload);
	}

	const std::string &Bytes() const { return bytes_; }

private:
	void Field(const std::string &name, char type, const std::string &value) {
		AppendSized(bytes_, name);
		bytes_.push_back(type);
		AppendSized(bytes_, value);
	}

	std::string bytes_;
};

const char *NumericPolicyName(GridNumericPolicy policy) {
	switch (policy) {
	case GridNumericPolicy::Float64V1: return "float64_v1";
	case GridNumericPolicy::OpenMeteoF32V1: return "openmeteo_f32_v1";
	}
	throw ReaderError(ReaderErrorCode::InvalidShape, "grid identity numeric policy is unsupported");
}

const char *EarthKindName(GridEarthKind kind) {
	switch (kind) {
	case GridEarthKind::Sphere: return "sphere";
	case GridEarthKind::Wgs84Source: return "wgs84_source";
	}
	throw ReaderError(ReaderErrorCode::InvalidShape, "grid identity earth model is unsupported");
}

const char *OrderName(GridStorageOrder order) {
	switch (order) {
	case GridStorageOrder::Separate: return "separate";
	case GridStorageOrder::LongitudeFastest: return "longitude_fastest";
	case GridStorageOrder::LatitudeFastest: return "latitude_fastest";
	}
	throw ReaderError(ReaderErrorCode::InvalidShape, "grid identity storage order is unsupported");
}

std::string EncodeGaussianRow(const GaussianRow &row, GridNumericPolicy policy) {
	CanonicalEncoder encoder("duckomo-grid-row-v1");
	encoder.Real("latitude", row.latitude, policy);
	encoder.Integer("point_count", row.point_count);
	encoder.Real("longitude_origin", row.longitude_origin, policy);
	encoder.Real("longitude_step", row.longitude_step, policy);
	return encoder.Bytes();
}

std::string EncodeGaussianSegment(const GaussianRegionSegment &segment) {
	CanonicalEncoder encoder("duckomo-grid-region-segment-v1");
	encoder.Integer("parent_row", segment.parent_row);
	encoder.Integer("parent_begin", segment.parent_begin);
	encoder.Integer("count", segment.count);
	return encoder.Bytes();
}

void EncodeProjectedBase(CanonicalEncoder &encoder, std::uint64_t nx, std::uint64_t ny, double x0, double y0,
	                       double dx, double dy, GridStorageOrder order, GridNumericPolicy policy) {
	encoder.Integer("nx", nx);
	encoder.Integer("ny", ny);
	encoder.Real("x0", x0, policy);
	encoder.Real("y0", y0, policy);
	encoder.Real("dx", dx, policy);
	encoder.Real("dy", dy, policy);
	encoder.String("order", OrderName(order));
}

std::string Sha256Hex(const std::string &bytes) {
	char digest[duckdb_mbedtls::MbedTlsWrapper::SHA256_HASH_LENGTH_BYTES];
	duckdb_mbedtls::MbedTlsWrapper::ComputeSha256Hash(bytes.data(), bytes.size(), digest);
	static constexpr char HEX[] = "0123456789abcdef";
	std::string result;
	result.resize(sizeof(digest) * 2);
	for (std::size_t index = 0; index < sizeof(digest); index++) {
		const auto byte = static_cast<unsigned char>(digest[index]);
		result[index * 2] = HEX[byte >> 4];
		result[index * 2 + 1] = HEX[byte & 0x0f];
	}
	return result;
}

std::string LayoutGeometryName(SpatialLayoutGeometry geometry) {
	switch (geometry) {
	case SpatialLayoutGeometry::Regular: return "regular";
	case SpatialLayoutGeometry::Projected: return "projected";
	case SpatialLayoutGeometry::Gaussian: return "gaussian";
	}
	throw ReaderError(ReaderErrorCode::InvalidShape, "grid layout geometry is unsupported");
}

} // namespace

std::string CanonicalGridDefinition(const GridDefinition &definition) {
	if (definition.version != 1) {
		throw ReaderError(ReaderErrorCode::InvalidShape, "grid identity supports only definition version 1");
	}
	CanonicalEncoder encoder("duckomo-grid-v1");
	encoder.Integer("version", definition.version);
	encoder.String("coordinate_rule_id", definition.coordinate_rule_id);
	encoder.String("numeric_policy", NumericPolicyName(definition.numeric_policy));
	encoder.String("earth.kind", EarthKindName(definition.earth.kind));
	if (definition.earth.kind == GridEarthKind::Sphere) {
		encoder.Real("earth.radius_m", definition.earth.radius_m, definition.numeric_policy);
	} else {
		encoder.Real("earth.semi_major_axis_m", definition.earth.semi_major_axis_m, definition.numeric_policy);
		encoder.Real("earth.inverse_flattening", definition.earth.inverse_flattening, definition.numeric_policy);
	}

	if (const auto *regular = std::get_if<RegularGrid>(&definition.geometry)) {
		encoder.String("geometry", "regular");
		encoder.Integer("nx", regular->Nx());
		encoder.Integer("ny", regular->Ny());
		encoder.Real("latitude_origin", regular->LatitudeOrigin(), definition.numeric_policy);
		encoder.Real("longitude_origin", regular->LongitudeOrigin(), definition.numeric_policy);
		encoder.Real("latitude_step", regular->LatitudeStep(), definition.numeric_policy);
		encoder.Real("longitude_step", regular->LongitudeStep(), definition.numeric_policy);
		encoder.String("order", OrderName(regular->Order()));
		encoder.Boolean("allow_out_of_range_latitude", regular->AllowsOutOfRangeLatitude());
	} else if (const auto *projected = std::get_if<ProjectedGrid>(&definition.geometry)) {
		std::visit([&](const auto &parameters) {
			using T = std::decay_t<decltype(parameters)>;
			if constexpr (std::is_same_v<T, RotatedLatLonParameters>) {
				encoder.String("geometry", "rotated_latlon");
				EncodeProjectedBase(encoder, parameters.nx, parameters.ny, parameters.x0, parameters.y0,
				                   parameters.dx, parameters.dy, parameters.order, definition.numeric_policy);
				encoder.Real("north_pole_latitude", parameters.north_pole_latitude, definition.numeric_policy);
				encoder.Real("north_pole_longitude", parameters.north_pole_longitude, definition.numeric_policy);
				encoder.Real("rotation", parameters.rotation, definition.numeric_policy);
			} else if constexpr (std::is_same_v<T, LambertParameters>) {
				encoder.String("geometry", "lambert_conformal_conic");
				EncodeProjectedBase(encoder, parameters.nx, parameters.ny, parameters.x0, parameters.y0,
				                   parameters.dx, parameters.dy, parameters.order, definition.numeric_policy);
				encoder.Real("longitude_of_false_origin", parameters.longitude_of_false_origin, definition.numeric_policy);
				encoder.Real("latitude_of_false_origin", parameters.latitude_of_false_origin, definition.numeric_policy);
				encoder.Real("standard_parallel_1", parameters.standard_parallel_1, definition.numeric_policy);
				encoder.Real("standard_parallel_2", parameters.standard_parallel_2, definition.numeric_policy);
				encoder.Real("radius_m", parameters.radius_m, definition.numeric_policy);
				encoder.Real("false_easting_m", parameters.false_easting_m, definition.numeric_policy);
				encoder.Real("false_northing_m", parameters.false_northing_m, definition.numeric_policy);
			} else {
				encoder.String("geometry", "stereographic");
				EncodeProjectedBase(encoder, parameters.nx, parameters.ny, parameters.x0, parameters.y0,
				                   parameters.dx, parameters.dy, parameters.order, definition.numeric_policy);
				encoder.Real("latitude_of_origin", parameters.latitude_of_origin, definition.numeric_policy);
				encoder.Real("longitude_of_origin", parameters.longitude_of_origin, definition.numeric_policy);
				encoder.Real("radius_m", parameters.radius_m, definition.numeric_policy);
				encoder.Real("scale_factor", parameters.scale_factor, definition.numeric_policy);
				encoder.Real("false_easting_m", parameters.false_easting_m, definition.numeric_policy);
				encoder.Real("false_northing_m", parameters.false_northing_m, definition.numeric_policy);
			}
		}, projected->Parameters());
	} else {
		const auto &gaussian = std::get<GaussianGrid>(definition.geometry);
		encoder.String("geometry", "reduced_gaussian");
		encoder.Integer("N", gaussian.N());
		encoder.String("latitude_rule", gaussian.LatitudeRule());
		std::vector<std::string> rows;
		rows.reserve(gaussian.Rows().size());
		for (const auto &row : gaussian.Rows()) rows.push_back(EncodeGaussianRow(row, definition.numeric_policy));
		encoder.RecordArray("rows", rows);
		std::vector<std::string> segments;
		segments.reserve(gaussian.SubsetSegments().size());
		for (const auto &segment : gaussian.SubsetSegments()) segments.push_back(EncodeGaussianSegment(segment));
		encoder.RecordArray("subset_segments", segments);
	}
	return encoder.Bytes();
}

std::string GridId(const GridDefinition &definition) { return Sha256Hex(CanonicalGridDefinition(definition)); }

std::optional<std::string> ParentGridId(const GridDefinition &definition) {
	const auto *gaussian = std::get_if<GaussianGrid>(&definition.geometry);
	if (gaussian == nullptr || !gaussian->IsSubset()) return std::nullopt;
	GridDefinition parent(definition.coordinate_rule_id, definition.earth, definition.numeric_policy,
	                      GaussianGrid(gaussian->N(), gaussian->LatitudeRule(), gaussian->Rows()));
	parent.version = definition.version;
	return GridId(parent);
}

std::string LayoutId(const GridDefinition &definition, const SpatialLayout &layout) {
	(void)layout.NativePosition(definition, 0);
	CanonicalEncoder encoder("duckomo-layout-v1");
	encoder.String("grid_id", GridId(definition));
	encoder.String("geometry", LayoutGeometryName(layout.geometry));
	encoder.IntegerArray("shape", layout.shape);
	encoder.StringArray("axes", layout.axes);
	encoder.IntegerArray("strides", layout.strides);
	encoder.IntegerArray("non_spatial_axes", layout.non_spatial_axes);
	encoder.Boolean("flattened", layout.flattened);
	encoder.String("order", OrderName(layout.order));
	encoder.Integer("latitude_axis", layout.latitude_axis);
	encoder.Integer("longitude_axis", layout.longitude_axis);
	encoder.Integer("point_axis", layout.point_axis);
	return Sha256Hex(encoder.Bytes());
}

} // namespace duckomo
} // namespace duckdb
