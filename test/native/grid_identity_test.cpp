#include "duckomo/grid_identity.hpp"

#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "duckomo/dimensions.hpp"
#include "duckomo/domain_registry.hpp"
#include "duckomo/om_reader.hpp"
#include "duckomo/spatial_layout.hpp"

namespace {
using namespace duckdb::duckomo;
using duckdb::LogicalType;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::string ReadGolden(const std::string &vector_id, const std::string &field) {
	std::ifstream input("test/data/grids/canonical-vectors.json", std::ios::binary);
	Require(static_cast<bool>(input), "canonical-vectors.json is available from the repository root");
	const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	const auto id_position = contents.find("\"id\": \"" + vector_id + "\"");
	Require(id_position != std::string::npos, "canonical vector exists: " + vector_id);
	const auto next_vector = contents.find("\"id\":", id_position + 1);
	const auto field_position = contents.find("\"" + field + "\": \"", id_position);
	Require(field_position != std::string::npos && (next_vector == std::string::npos || field_position < next_vector),
	        "canonical vector " + vector_id + " contains " + field);
	const auto value_begin = field_position + field.size() + 5;
	const auto value_end = contents.find('"', value_begin);
	Require(value_end != std::string::npos && (next_vector == std::string::npos || value_end < next_vector),
	        "canonical vector field is a string: " + vector_id + "." + field);
	return contents.substr(value_begin, value_end - value_begin);
}

BoundSchema MakeSchema(std::vector<std::uint64_t> shape, std::vector<std::string> axes) {
	BoundSchema schema;
	schema.shape = shape;
	schema.row_count = 1;
	for (const auto length : shape) schema.row_count *= length;
	BoundVariable variable;
	variable.canonical_path = "/value";
	variable.column_name = "value";
	variable.type = LogicalType::FLOAT;
	variable.shape = shape;
	variable.row_count = schema.row_count;
	variable.inferred_axes = std::move(axes);
	schema.variables.push_back(std::move(variable));
	return schema;
}

void TestCanonicalGridDefinitionAndFloatNormalization() {
	GridDefinition positive_zero("regular_grid_v1", GridEarth{}, GridNumericPolicy::Float64V1,
	                             RegularGrid(3, 2, 10, 0.0, 1, 2, GridStorageOrder::Separate));
	GridDefinition negative_zero("regular_grid_v1", GridEarth{}, GridNumericPolicy::Float64V1,
	                             RegularGrid(3, 2, 10, -0.0, 1, 2, GridStorageOrder::Separate));
	GridDefinition different_step("regular_grid_v1", GridEarth{}, GridNumericPolicy::Float64V1,
	                              RegularGrid(3, 2, 10, 0.0, 1, 2.5, GridStorageOrder::Separate));
	Require(CanonicalGridDefinition(positive_zero) == CanonicalGridDefinition(negative_zero),
	        "canonical serialization normalizes negative zero");
	Require(GridId(positive_zero) == GridId(negative_zero), "negative zero has the same canonical grid identity");
	Require(GridId(positive_zero) != GridId(different_step), "coordinate-affecting parameters change grid identity");
	const auto regular_id = GridId(positive_zero);
	Require(regular_id == ReadGolden("regular_grid_negative_zero", "grid_id"),
	        "regular-grid runtime identity matches the shared canonical vector");
	GridDefinition openmeteo_f32("regular_grid_v1", GridEarth{}, GridNumericPolicy::OpenMeteoF32V1,
	                              RegularGrid(2, 1, 10.1234567, -0.0, 0.1, 0.2,
	                                          GridStorageOrder::Separate));
	Require(GridId(openmeteo_f32) == ReadGolden("regular_grid_openmeteo_f32_rounding", "grid_id"),
	        "Float32 identity rounds and widens values according to the shared canonical vector");
	GridDefinition float64_same_parameters("regular_grid_v1", GridEarth{}, GridNumericPolicy::Float64V1,
	                                       RegularGrid(2, 1, 10.1234567, -0.0, 0.1, 0.2,
	                                                   GridStorageOrder::Separate));
	Require(GridId(openmeteo_f32) != GridId(float64_same_parameters),
	        "numeric policy is part of canonical grid identity");
}

void TestParentAndLayoutIdentities() {
	const std::vector<GaussianRow> rows{{60, 4, 0, 90}, {20, 6, 0, 60}, {-20, 6, 0, 60}, {-60, 4, 0, 90}};
	bool rejected_non_wgs84 = false;
	try {
		GridEarth earth{GridEarthKind::Wgs84Source};
		earth.semi_major_axis_m = 6378136.0;
		(void)GridDefinition("explicit_gaussian_v1", earth, GridNumericPolicy::Float64V1,
		                     GaussianGrid(2, "explicit_v1", rows));
	} catch (const ReaderError &) {
		rejected_non_wgs84 = true;
	}
	Require(rejected_non_wgs84, "WGS84 source profile rejects altered ellipsoid parameters");
	GridDefinition full("explicit_gaussian_v1", GridEarth{GridEarthKind::Wgs84Source},
	                    GridNumericPolicy::Float64V1, GaussianGrid(2, "explicit_v1", rows));
	GridDefinition subset("explicit_gaussian_v1", GridEarth{GridEarthKind::Wgs84Source},
	                      GridNumericPolicy::Float64V1, GaussianGrid(2, "explicit_v1", rows, {{1, 1, 3}}));
	GridDefinition gaussian_roots("gaussian_n2_legendre_roots_v1", GridEarth{GridEarthKind::Wgs84Source},
	                              GridNumericPolicy::Float64V1,
	                              GaussianGrid(2, "legendre_roots_v1", rows));
	GridDefinition gaussian_producer_rule("gaussian_n2_openmeteo_approx_v1", GridEarth{GridEarthKind::Wgs84Source},
	                                      GridNumericPolicy::Float64V1,
	                                      GaussianGrid(2, "openmeteo_approx_v1", rows));
	Require(!ParentGridId(full).has_value(), "full grid has no parent grid identity");
	Require(ParentGridId(subset) == GridId(full), "Gaussian subset points to its full canonical parent");
	Require(GridId(subset) != GridId(full), "Gaussian subset mapping changes its own grid identity");

	RegularGrid grid(3, 2, 10, 100, 1, 2, GridStorageOrder::Separate);
	GridDefinition regular("regular_grid_v1", GridEarth{}, GridNumericPolicy::Float64V1, grid);
	GridDefinition same_definition_from_other_source("regular_grid_v1", GridEarth{}, GridNumericPolicy::Float64V1,
	                                                RegularGrid(3, 2, 10, 100, 1, 2,
	                                                            GridStorageOrder::Separate));
	auto first_schema = MakeSchema({2, 3}, {"lat", "lon"});
	const auto first_layout = BindGridSpatialLayout(first_schema, {{"lat", "lon"}}, regular, {"lat", "lon"});
	auto second_schema = MakeSchema({4, 2, 3}, {"member", "lat", "lon"});
	const auto second_layout = BindGridSpatialLayout(second_schema, {{"member", "lat", "lon"}}, regular,
	                                                 {"lat", "lon"});
	Require(GridId(full) == ReadGolden("gaussian_full_parent", "grid_id"),
	        "full Gaussian runtime identity matches the shared canonical vector");
	Require(GridId(subset) == ReadGolden("gaussian_region_subset", "grid_id"),
	        "Gaussian subset runtime identity matches the shared canonical vector");
	Require(GridId(gaussian_roots) != GridId(gaussian_producer_rule),
	        "Gaussian Legendre-root and producer-approximation rules have different identities");
	Require(ParentGridId(subset) == ReadGolden("gaussian_region_subset", "parent_grid_id"),
	        "Gaussian subset parent identity matches the shared canonical vector");
	Require(GridId(regular) == GridId(regular), "space definition identity is stable across non-spatial axes");
	Require(GridId(regular) == ReadGolden("regular_separate_two_axis_layout", "grid_id"),
	        "regular layout runtime grid identity matches its shared canonical vector");
	Require(GridId(regular) == GridId(same_definition_from_other_source),
	        "source annotations and domain names are outside the canonical definition identity");
	Require(LayoutId(regular, first_layout) != LayoutId(regular, second_layout),
	        "layout identity includes ordered source axes and shape");
	Require(LayoutId(regular, first_layout) == ReadGolden("regular_separate_two_axis_layout", "layout_id"),
	        "regular layout runtime identity matches the shared canonical vector");
}

void TestGeneratedDefinitionRegistry() {
	const auto *rotated = FindRegisteredGridDefinition("gem_rdps_10km");
	const auto *stereographic = FindRegisteredGridDefinition("gem_regional");
	const auto *formula_vector = FindRegisteredGridDefinition("lambert_formula_vector");
	const auto *n160 = FindRegisteredGridDefinition("n160");
	const auto *n320 = FindRegisteredGridDefinition("n320");
	const auto *region = FindRegisteredGridDefinition("n320_ecmwf_aifs_europe_ensemble");
	Require(rotated && stereographic && formula_vector && n160 && n320 && region,
	        "all frozen generated definitions are consumed by the runtime registry");
	Require(rotated->expected_axis_order == std::vector<std::string>({"y", "x"}) &&
	            rotated->expected_layout == "separate",
	        "rotated source profile is retained as ordered separate axes");
	Require(formula_vector->domain_bindable == false,
	        "a Lambert formula identity vector is not exposed as a producer domain");
	Require(std::get<GaussianGrid>(n160->definition.geometry).PointCount() == 138346 &&
	            std::get<GaussianGrid>(n320->definition.geometry).PointCount() == 542080 &&
	            std::get<GaussianGrid>(region->definition.geometry).PointCount() == 14747,
	        "generated Gaussian full and regional point counts are loaded from the frozen definitions");
	Require(ParentGridId(region->definition) == GridId(n320->definition),
	        "generated Gaussian region is linked to the n320 parent identity");
	Require(!stereographic->source_path.empty() && !stereographic->object_profile_status.empty(),
	        "generated domain source and unverified object-profile provenance remain available");
}

} // namespace

int main() {
	try {
		TestCanonicalGridDefinitionAndFloatNormalization();
		TestParentAndLayoutIdentities();
		TestGeneratedDefinitionRegistry();
		std::cout << "grid identity checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "grid identity checks failed: " << error.what() << '\n';
		return 1;
	}
}
