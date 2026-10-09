#include "duckomo/grid_info.hpp"

#include <iomanip>
#include <sstream>
#include <type_traits>

#include "duckomo/grid_identity.hpp"
#include "duckomo/projected_grid.hpp"

namespace duckdb {
namespace duckomo {
namespace {

std::string JsonQuote(const std::string &value) {
	std::string result = "\"";
	for (const auto byte : value) {
		switch (byte) {
		case '"': result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\b': result += "\\b"; break;
		case '\f': result += "\\f"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default:
			if (static_cast<unsigned char>(byte) < 0x20) {
				static constexpr char HEX[] = "0123456789abcdef";
				result += "\\u00";
				result.push_back(HEX[(static_cast<unsigned char>(byte) >> 4) & 0x0f]);
				result.push_back(HEX[static_cast<unsigned char>(byte) & 0x0f]);
			} else {
				result.push_back(byte);
			}
		}
	}
	result.push_back('"');
	return result;
}

std::string OptionalJsonString(const std::string &value) {
	return value.empty() ? "null" : JsonQuote(value);
}

std::string Number(double value) {
	std::ostringstream output;
	output << std::setprecision(17) << value;
	return output.str();
}

std::string U64Array(const std::vector<std::uint64_t> &values) {
	std::ostringstream output;
	output << '[';
	for (std::size_t index = 0; index < values.size(); index++) {
		if (index) output << ',';
		output << values[index];
	}
	output << ']';
	return output.str();
}

std::string StringArray(const std::vector<std::string> &values) {
	std::ostringstream output;
	output << '[';
	for (std::size_t index = 0; index < values.size(); index++) {
		if (index) output << ',';
		output << JsonQuote(values[index]);
	}
	output << ']';
	return output.str();
}

const char *PolicyName(GridNumericPolicy policy) {
	switch (policy) {
	case GridNumericPolicy::Float64V1: return "float64_v1";
	case GridNumericPolicy::OpenMeteoF32V1: return "openmeteo_f32_v1";
	}
	return "unknown";
}

const char *OrderName(GridStorageOrder order) {
	switch (order) {
	case GridStorageOrder::Separate: return "separate";
	case GridStorageOrder::LongitudeFastest: return "x_fastest";
	case GridStorageOrder::LatitudeFastest: return "y_fastest";
	}
	return "unknown";
}

std::string GridType(const GridDefinition &definition) {
	if (std::holds_alternative<RegularGrid>(definition.geometry)) return "regular_latlon";
	if (const auto *projected = std::get_if<ProjectedGrid>(&definition.geometry)) {
		return std::visit([](const auto &parameters) {
			using T = std::decay_t<decltype(parameters)>;
			if constexpr (std::is_same_v<T, RotatedLatLonParameters>) return std::string("rotated_latlon");
			if constexpr (std::is_same_v<T, LambertParameters>) return std::string("lambert_conformal_conic");
			return std::string("stereographic");
		}, projected->Parameters());
	}
	return "reduced_gaussian";
}

std::string EarthJson(const GridEarth &earth) {
	std::ostringstream output;
	if (earth.kind == GridEarthKind::Sphere) {
		output << "{\"model\":\"sphere\",\"radius_m\":" << Number(earth.radius_m) << '}';
	} else {
		output << "{\"model\":\"wgs84\",\"semi_major_m\":" << Number(earth.semi_major_axis_m)
		       << ",\"inverse_flattening\":" << Number(earth.inverse_flattening) << '}';
	}
	return output.str();
}

std::string DefinitionJson(const GridDefinition &definition) {
	std::ostringstream output;
	output << "{\"version\":" << definition.version << ",\"type\":" << JsonQuote(GridType(definition))
	       << ",\"coordinate_rule\":" << JsonQuote(definition.coordinate_rule_id)
	       << ",\"numeric_policy\":" << JsonQuote(PolicyName(definition.numeric_policy))
	       << ",\"earth\":" << EarthJson(definition.earth) << ",\"parameters\":";
	if (const auto *regular = std::get_if<RegularGrid>(&definition.geometry)) {
		output << "{\"nx\":" << regular->Nx() << ",\"ny\":" << regular->Ny()
		       << ",\"lat0\":" << Number(regular->LatitudeOrigin())
		       << ",\"lon0\":" << Number(regular->LongitudeOrigin())
		       << ",\"dlat\":" << Number(regular->LatitudeStep())
		       << ",\"dlon\":" << Number(regular->LongitudeStep())
		       << ",\"order\":" << JsonQuote(OrderName(regular->Order()))
		       << ",\"allow_out_of_range_latitude\":"
		       << (regular->AllowsOutOfRangeLatitude() ? "true" : "false") << '}';
	} else if (const auto *projected = std::get_if<ProjectedGrid>(&definition.geometry)) {
		std::visit([&](const auto &parameters) {
			using T = std::decay_t<decltype(parameters)>;
			output << "{\"nx\":" << parameters.nx << ",\"ny\":" << parameters.ny
			       << ",\"x0\":" << Number(parameters.x0) << ",\"y0\":" << Number(parameters.y0)
			       << ",\"dx\":" << Number(parameters.dx) << ",\"dy\":" << Number(parameters.dy)
			       << ",\"order\":" << JsonQuote(OrderName(parameters.order));
			if constexpr (std::is_same_v<T, RotatedLatLonParameters>) {
				output << ",\"north_pole_latitude\":" << Number(parameters.north_pole_latitude)
				       << ",\"north_pole_longitude\":" << Number(parameters.north_pole_longitude)
				       << ",\"rotation\":" << Number(parameters.rotation);
			} else if constexpr (std::is_same_v<T, LambertParameters>) {
				output << ",\"longitude_of_false_origin\":" << Number(parameters.longitude_of_false_origin)
				       << ",\"latitude_of_false_origin\":" << Number(parameters.latitude_of_false_origin)
				       << ",\"standard_parallel_1\":" << Number(parameters.standard_parallel_1)
				       << ",\"standard_parallel_2\":" << Number(parameters.standard_parallel_2)
				       << ",\"radius_m\":" << Number(parameters.radius_m)
				       << ",\"false_easting_m\":" << Number(parameters.false_easting_m)
				       << ",\"false_northing_m\":" << Number(parameters.false_northing_m);
			} else {
				output << ",\"latitude_of_origin\":" << Number(parameters.latitude_of_origin)
				       << ",\"longitude_of_origin\":" << Number(parameters.longitude_of_origin)
				       << ",\"radius_m\":" << Number(parameters.radius_m)
				       << ",\"scale_factor\":" << Number(parameters.scale_factor)
				       << ",\"false_easting_m\":" << Number(parameters.false_easting_m)
				       << ",\"false_northing_m\":" << Number(parameters.false_northing_m);
			}
			output << '}';
	}, projected->Parameters());
	} else {
		const auto &gaussian = std::get<GaussianGrid>(definition.geometry);
		output << "{\"n\":" << gaussian.N() << ",\"latitude_rule\":" << JsonQuote(gaussian.LatitudeRule())
		       << ",\"rows\":[";
		for (std::size_t index = 0; index < gaussian.Rows().size(); index++) {
			if (index) output << ',';
			const auto &row = gaussian.Rows()[index];
			output << "{\"latitude\":" << Number(row.latitude) << ",\"point_count\":" << row.point_count
			       << ",\"longitude_origin\":" << Number(row.longitude_origin)
			       << ",\"longitude_step\":" << Number(row.longitude_step) << '}';
		}
		output << "],\"subset_segments\":";
		if (gaussian.IsSubset()) {
			output << '[';
			for (std::size_t index = 0; index < gaussian.SubsetSegments().size(); index++) {
				if (index) output << ',';
				const auto &segment = gaussian.SubsetSegments()[index];
				output << "{\"parent_row\":" << segment.parent_row << ",\"parent_begin\":" << segment.parent_begin
				       << ",\"count\":" << segment.count << '}';
			}
			output << ']';
		} else {
			output << "null";
		}
		output << '}';
	}
	output << '}';
	return output.str();
}

std::string LayoutJson(const GridDefinition &definition, const SpatialLayout &layout,
	                   const std::string &grid_id, const std::string &layout_id) {
	std::string geometry = "regular";
	if (layout.geometry == SpatialLayoutGeometry::Projected) geometry = "projected";
	else if (layout.geometry == SpatialLayoutGeometry::Gaussian) geometry = "gaussian";
	std::ostringstream output;
	output << "{\"grid_id\":" << JsonQuote(grid_id) << ",\"layout_id\":" << JsonQuote(layout_id)
	       << ",\"geometry\":" << JsonQuote(geometry) << ",\"shape\":" << U64Array(layout.shape)
	       << ",\"axes\":" << StringArray(layout.axes) << ",\"strides\":" << U64Array(layout.strides)
	       << ",\"non_spatial_axes\":" << U64Array(layout.non_spatial_axes)
	       << ",\"flattened\":" << (layout.flattened ? "true" : "false")
	       << ",\"order\":" << JsonQuote(OrderName(layout.order))
	       << ",\"latitude_axis\":";
	if (layout.flattened) output << "null";
	else output << layout.latitude_axis;
	output << ",\"longitude_axis\":";
	if (layout.flattened) output << "null";
	else output << layout.longitude_axis;
	output << ",\"point_axis\":";
	if (!layout.flattened) output << "null";
	else output << layout.point_axis;
	if (const auto *gaussian = std::get_if<GaussianGrid>(&definition.geometry)) {
		output << ",\"subset_segments\":";
		if (!gaussian->IsSubset()) {
			output << "null";
		} else {
			output << '[';
			for (std::size_t index = 0; index < gaussian->SubsetSegments().size(); index++) {
				if (index) output << ',';
				const auto &segment = gaussian->SubsetSegments()[index];
				output << "{\"parent_row\":" << segment.parent_row << ",\"parent_begin\":" << segment.parent_begin
				       << ",\"count\":" << segment.count << '}';
			}
			output << ']';
		}
	}
	output << '}';
	return output.str();
}

std::string CapabilitiesJson() {
	return "{\"point_semantics\":{\"status\":\"defined\",\"name\":\"native_point_sample\"},"
	       "\"adjacency\":{\"status\":\"unsupported\",\"reason\":\"no verified adjacency model\"},"
	       "\"cell_boundary\":{\"status\":\"unsupported\",\"reason\":\"no verified cell boundary\"},"
	       "\"area\":{\"status\":\"unsupported\",\"reason\":\"no verified cell area\"},"
	       "\"distance\":{\"status\":\"unsupported\",\"reason\":\"no distance operator is defined\"},"
	       "\"vector_orientation\":{\"status\":\"unsupported\",\"reason\":\"no verified vector orientation\"}}";
}

std::string CrsJson(const GridDefinition &definition, const BoundSchema &schema) {
	std::string native_unit = "degrees";
	if (const auto *projected = std::get_if<ProjectedGrid>(&definition.geometry)) {
		if (projected->CoordinateUnit() == NativeCoordinateUnit::Metres) native_unit = "metres";
	}
	std::ostringstream output;
	const auto output_crs = definition.earth.kind == GridEarthKind::Wgs84Source
	                            ? std::string("OGC:CRS84")
	                            : "+proj=longlat +R=" + Number(definition.earth.radius_m) + " +type=crs";
	output << "{\"earth\":" << EarthJson(definition.earth)
	       << ",\"native_coordinate_unit\":" << JsonQuote(native_unit)
	       << ",\"output_crs\":" << JsonQuote(output_crs) << ",\"axis_order\":[\"longitude\",\"latitude\"]"
	       << ",\"source_crs_wkt\":";
	if (schema.crs_wkt.empty()) output << "null";
	else output << JsonQuote(schema.crs_wkt);
	output << '}';
	return output.str();
}

} // namespace

GridInfoDocument DescribeGrid(const GridDefinition &definition, const SpatialLayout &layout,
	                          const BoundSchema &schema, const std::string &source,
	                          const std::string &object_id,
	                          const std::optional<std::string> &object_version,
	                          const std::string &version_strength, bool content_verified,
	                          const std::string &evidence_level, const std::string &evidence_sample_id,
	                          const std::string &evidence_source, const std::string &evidence_build_pair,
	                          const std::string &evidence_claims) {
	GridInfoDocument document;
	document.grid_id = GridId(definition);
	document.parent_grid_id = ParentGridId(definition);
	document.grid_type = GridType(definition);
	document.definition_json = DefinitionJson(definition);
	if (document.definition_json.size() > 1) {
		document.definition_json.pop_back();
		document.definition_json += ",\"grid_id\":" + JsonQuote(document.grid_id) + '}';
	}
	document.layout_json = LayoutJson(definition, layout, document.grid_id, LayoutId(definition, layout));
	document.crs_json = CrsJson(definition, schema);
	document.capabilities_json = CapabilitiesJson();
	document.provenance_json = "{\"source\":" + JsonQuote(source) +
	                           ",\"evidence_level\":" + JsonQuote(evidence_level) +
	                           ",\"evidence_sample_id\":" + OptionalJsonString(evidence_sample_id) +
	                           ",\"evidence_source\":" + OptionalJsonString(evidence_source) +
	                           ",\"evidence_build_pair\":" + OptionalJsonString(evidence_build_pair) +
	                           ",\"evidence_claims\":" + JsonQuote(evidence_claims) +
	                           ",\"identity_hash_excludes_provenance\":true}";
	document.object_id = object_id;
	document.object_version = object_version;
	document.version_strength = version_strength;
	document.content_verified = content_verified;
	return document;
}

} // namespace duckomo
} // namespace duckdb
