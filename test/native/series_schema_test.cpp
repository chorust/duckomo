#include "duckomo/read_om_binding.hpp"
#include "duckomo/schema.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace duckdb;
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

template <typename ACTION>
void MustReject(ACTION action, const std::string &label) {
	try {
		action();
	} catch (const std::exception &) {
		return;
	}
	throw std::runtime_error(label + " should be rejected");
}

Value Axes(std::initializer_list<const char *> names) {
	std::vector<Value> values;
	for (const auto *name : names) values.emplace_back(name);
	return Value::LIST(LogicalType::VARCHAR, std::move(values));
}

Value AxisMap(std::initializer_list<const char *> paths) {
	std::vector<Value> keys;
	std::vector<Value> values;
	for (const auto *path : paths) {
		keys.emplace_back(path);
		values.emplace_back(Axes({"latitude", "time", "longitude"}));
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR),
	                  std::move(keys), std::move(values));
}

ReadOmBindingSignature SignatureFor(const DeclaredSeriesSchema &declared) {
	ReadOmBindingSignature signature;
	signature.column_types = declared.column_types;
	signature.column_names = declared.column_names;
	signature.grid_id = "grid-identity";
	signature.layout_id = "layout-identity";
	signature.object_id = "object-specific-identity";
	signature.include_source = declared.include_source;
	for (const auto &array : declared.arrays) {
		ReadOmArrayBindingSignature bound;
		bound.canonical_path = array.canonical_path;
		bound.column_name = array.column_name;
		bound.type = array.type;
		bound.dimension_names = array.dimension_names;
		bound.shape = {2, 3, 4};
		signature.arrays.emplace_back(std::move(bound));
	}
	return signature;
}

void TestRootAndCompleteMapProfiles() {
	auto root_axes = Axes({"latitude", "time", "longitude"});
	auto root = BuildDeclaredSeriesSchema(&root_axes, true);
	Require(root.arrays.size() == 1 && root.arrays[0].canonical_path == "/" &&
	            root.arrays[0].column_name == "value" && root.arrays[0].type == LogicalType::FLOAT,
	        "a shared list declares the root Float32 array named value");
	Require(root.column_names == std::vector<std::string>({"value", "lat", "lon", "valid_time", "om_source"}) &&
	            root.column_types.back() == OmSourceType(),
	        "declaration output uses the ordinary spatial, time and source signature");

	auto map = AxisMap({"/zeta", "/alpha"});
	auto named = BuildDeclaredSeriesSchema(&map, false);
	Require(named.arrays.size() == 2 && named.arrays[0].canonical_path == "/alpha" &&
	            named.arrays[1].canonical_path == "/zeta" &&
	            named.column_names == std::vector<std::string>({"alpha", "zeta", "lat", "lon", "valid_time"}),
	        "complete maps are sorted by canonical UTF-8 path before shared columns");

	auto root_map = AxisMap({"value"});
	auto root_alias = BuildDeclaredSeriesSchema(&root_map, false);
	Require(root_alias.arrays.size() == 1 && root_alias.arrays[0].canonical_path == "/",
	        "value is accepted as a root-only map alias");
	MustReject([&] { (void)BuildDeclaredSeriesSchema(&root_axes, false, std::string("/alpha")); },
	           "shared root declaration with non-root variable");
	MustReject([&] { (void)BuildDeclaredSeriesSchema(&map, false, std::string("alpha")); },
	           "variable selection with declarations for other arrays");
}

void TestDeclarationValidation() {
	auto missing_time = Axes({"latitude", "longitude"});
	MustReject([&] { (void)BuildDeclaredSeriesSchema(&missing_time, false); }, "declaration without time axis");
	auto duplicate_axis = Axes({"time", "time"});
	MustReject([&] { (void)BuildDeclaredSeriesSchema(&duplicate_axis, false); }, "duplicate dimension names");
	auto relative_path = AxisMap({"temperature"});
	MustReject([&] { (void)BuildDeclaredSeriesSchema(&relative_path, false); }, "relative non-root map key");
	auto duplicate_root_alias = AxisMap({"/", "value"});
	MustReject([&] { (void)BuildDeclaredSeriesSchema(&duplicate_root_alias, false); }, "duplicate root aliases");
	auto mixed_root_and_named = AxisMap({"/", "/temperature"});
	MustReject([&] { (void)BuildDeclaredSeriesSchema(&mixed_root_and_named, false); }, "mixed root and named map paths");
	auto different_axes = Value::MAP(LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR),
	                                {Value("/a"), Value("/b")},
	                                {Axes({"latitude", "time", "longitude"}), Axes({"time", "latitude"})});
	MustReject([&] { (void)BuildDeclaredSeriesSchema(&different_axes, false); }, "inconsistent ordered array dimensions");
}

void TestChildSignatureReconciliation() {
	auto dimensions = AxisMap({"/temperature", "/wind"});
	auto declared = BuildDeclaredSeriesSchema(&dimensions, true);
	auto first = SignatureFor(declared);
	ValidateSeriesChildSignature(declared, first, 3);

	auto next = first;
	next.object_id = "a-different-object";
	ValidateSeriesChildSignature(declared, next, 3, &first);

	auto wrong_time_extent = first;
	wrong_time_extent.arrays[0].shape[1] = 4;
	MustReject([&] { ValidateSeriesChildSignature(declared, wrong_time_extent, 3); }, "wrong time extent");
	auto wrong_non_time_shape = first;
	wrong_non_time_shape.arrays[1].shape[0] = 5;
	MustReject([&] { ValidateSeriesChildSignature(declared, wrong_non_time_shape, 3, &first); },
	           "non-time shape mismatch");
	auto wrong_path = first;
	wrong_path.arrays[0].canonical_path = "/other";
	MustReject([&] { ValidateSeriesChildSignature(declared, wrong_path, 3); }, "selected array mismatch");
	auto wrong_type = first;
	wrong_type.arrays[0].type = LogicalType::BIGINT;
	MustReject([&] { ValidateSeriesChildSignature(declared, wrong_type, 3); }, "implicit value cast");
	auto wrong_grid = first;
	wrong_grid.grid_id = "another-grid";
	MustReject([&] { ValidateSeriesChildSignature(declared, wrong_grid, 3, &first); }, "grid identity mismatch");
	auto wrong_layout = first;
	wrong_layout.layout_id = "another-layout";
	MustReject([&] { ValidateSeriesChildSignature(declared, wrong_layout, 3, &first); }, "layout identity mismatch");
	auto missing_source = first;
	missing_source.include_source = false;
	MustReject([&] { ValidateSeriesChildSignature(declared, missing_source, 3); }, "source signature mismatch");
}

} // namespace

int main() {
	try {
		TestRootAndCompleteMapProfiles();
		TestDeclarationValidation();
		TestChildSignatureReconciliation();
		std::cout << "series schema reconciliation checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "series schema reconciliation checks failed: " << error.what() << '\n';
		return 1;
	}
}
