#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "core_functions_extension.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckomo/read_om.hpp"
#include "duckomo/read_om_binding.hpp"
#include "duckomo_extension.hpp"
#include "duckdb_test_compat.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace duckdb {
DUCKOMO_TEST_LOAD_ALL_EXTENSIONS(database) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	database.LoadStaticExtension<DuckomoExtension>();
#else
	(void)database;
#endif
}
} // namespace duckdb

namespace {

using namespace duckdb;
using namespace duckdb::duckomo;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

unique_ptr<LogicalOperator> BindReadOmChildForTest(ClientContext &context, TableFunctionBindInput &input,
                                                   idx_t bind_index, TableFunctionColumnNames &return_names) {
	auto child = BindReadOmChild(context, input);
	Require(child.bind_data != nullptr, "child binding owns ordinary read_om FunctionData");
	Require(child.signature.column_names == std::vector<std::string>({"value", "lat", "lon", "om_source"}),
	        "child signature preserves exact ordinary read_om names and order");
	Require(child.signature.column_types.size() == child.signature.column_names.size(),
	        "signature contains one type for every named output");
	Require(child.signature.column_types[0] == LogicalType::FLOAT &&
	            child.signature.column_types[1] == LogicalType::DOUBLE &&
	            child.signature.column_types[2] == LogicalType::DOUBLE &&
	            child.signature.column_types[3].id() == LogicalTypeId::STRUCT,
	        "signature preserves value, coordinate and existing source types");
	Require(child.signature.arrays.size() == 1 && child.signature.arrays[0].canonical_path == "/" &&
	            child.signature.arrays[0].column_name == "value" &&
	            child.signature.arrays[0].type == LogicalType::FLOAT &&
	            child.signature.arrays[0].shape == std::vector<std::uint64_t>({2, 3}) &&
	            child.signature.arrays[0].dimension_names == std::vector<std::string>({"row", "column"}),
	        "signature retains complete selected-array path, shape, and declared axis order");
	Require(!child.signature.grid_id.empty() && !child.signature.layout_id.empty() &&
	            !child.signature.object_id.empty(),
	        "owned child signature retains spatial and opaque object identity");
	Require(child.virtual_columns.find(COLUMN_IDENTIFIER_EMPTY) != child.virtual_columns.end() &&
	            child.virtual_columns.at(COLUMN_IDENTIFIER_EMPTY).type == LogicalType::BOOLEAN,
	        "ordinary cardinality virtual column remains attached to the child bind");
	const auto ordinary_function = GetReadOmFunction();
	Require(child.function.function == ordinary_function.function &&
	            child.function.init_global == ordinary_function.init_global &&
	            child.function.init_local == ordinary_function.init_local &&
	            child.function.pushdown_complex_filter == ordinary_function.pushdown_complex_filter,
	        "child retains the original scan and filter callbacks");

	const auto retained_object_id = child.signature.object_id;
	const auto expected_types = child.signature.column_types;
	const auto expected_names = child.signature.column_names;
	for (const auto &name : expected_names) return_names.emplace_back(name);
	auto logical_root = std::move(child).CreateLogicalGet(bind_index);
	Require(logical_root != nullptr && logical_root->type == LogicalOperatorType::LOGICAL_GET,
	        "ordinary child binding creates a LogicalGet leaf, not another executor");
	auto &get = logical_root->Cast<LogicalGet>();
	Require(get.bind_data != nullptr, "LogicalGet owns the retained ordinary bind data");
	std::vector<std::string> actual_names;
	for (const auto &name : get.names) actual_names.emplace_back(TableFunctionColumnNameString(name));
	std::vector<LogicalType> actual_types(get.returned_types.begin(), get.returned_types.end());
	Require(actual_names == expected_names && actual_types == expected_types,
	        "LogicalGet exposes the exact bind signature after input temporaries expire");
	Require(ReadOmLogicalGetTableIndex(get) == bind_index, "LogicalGet retains the supplied root bind index");
	Require(get.bind_data->Copy()->Equals(*get.bind_data), "owned child bind data supports normal copy/equality lifetime");
	Require(ReadOmBindingSignatureFrom(*get.bind_data).object_id == retained_object_id,
	        "opaque object identity remains owned after input path and named-option storage expires");
	const auto bindings = logical_root->GetColumnBindings();
	Require(bindings.size() == expected_names.size(), "logical leaf exposes every output binding");
	for (const auto &binding : bindings) {
		Require(ReadOmColumnBindingTableIndex(binding) == bind_index,
		        "logical leaf exposes the supplied bind index at its root");
	}
	return logical_root;
}

#if DUCKOMO_DUCKDB_API_GENERATION >= 2
unique_ptr<LogicalOperator> BindReadOmChildForTest(ClientContext &context, TableFunctionBindInput &input,
                                                   TableIndex bind_index, vector<Identifier> &return_names) {
	return BindReadOmChildForTest(context, input, bind_index.index, reinterpret_cast<TableFunctionColumnNames &>(return_names));
}
#endif

TableFunction MakeReadOmChildTestFunction() {
	TableFunction function("series_binding_test_reader", {LogicalType::VARCHAR}, nullptr);
	AddTableFunctionOption(function, "dimensions", LogicalType::ANY);
	AddTableFunctionOption(function, "grid", LogicalType::ANY);
	AddTableFunctionOption(function, "spatial_axes", LogicalType::LIST(LogicalType::VARCHAR));
	AddTableFunctionOption(function, "include_source", LogicalType::BOOLEAN);
	function.bind_operator = BindReadOmChildForTest;
	return function;
}

} // namespace

int main() {
	try {
		DuckDB database(nullptr);
		DUCKOMO_TEST_LOAD_EXTENSIONS(database);
		database.LoadStaticExtension<CoreFunctionsExtension>();
		ExtensionLoader loader(*database.instance, "duckomo_series_binding_test");
		loader.RegisterFunction(MakeReadOmChildTestFunction());
		Connection connection(database);
		auto result = duckomo_test::Rows(connection.Query(
		    "SELECT * FROM series_binding_test_reader('test/data/raw.om', "
		    "dimensions := map(['value'], [['row','column']]), "
		    "grid := {'nx':3, 'ny':2, 'lat0':10.0, 'lon0':100.0, 'dlat':1.0, 'dlon':2.0, 'order':'separate'}, "
		    "spatial_axes := ['row','column'], include_source := true)"));
		Require(result && !result->HasError(), result ? result->GetError() : "binding query returned no result");
		Require(result->RowCount() == 6, "ordinary read_om leaf executes after the wrapper bind inputs expire");
		std::cout << "series binding seam checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "series binding seam checks failed: " << error.what() << '\n';
		return 1;
	}
}
