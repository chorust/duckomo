#include "duckomo/region_geometry.hpp"

#include <cmath>

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/scalar_function_catalog_entry.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/function/function_binder.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension_manager.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

namespace duckdb::duckomo {
namespace {

[[noreturn]] void DependencyError(const std::string &message) {
	throw BinderException("[spatial_dependency] %s", message);
}

bool ExactSignature(const ScalarFunction &function, const std::vector<LogicalType> &arguments,
                    const LogicalType &return_type) {
	if (function.arguments.size() != arguments.size() || function.varargs.id() != LogicalTypeId::INVALID ||
	    function.return_type != return_type) {
		return false;
	}
	for (idx_t i = 0; i < arguments.size(); ++i) {
		if (function.arguments[i] != arguments[i]) return false;
	}
	return true;
}

} // namespace

RegionGeometryAdapter::RegionGeometryAdapter(ClientContext &context) : context(context) {
	const auto blob = LogicalType(LogicalTypeId::BLOB);
	const auto geometry = LogicalType::GEOMETRY();
	const auto double_type = LogicalType(LogicalTypeId::DOUBLE);
	const auto boolean = LogicalType(LogicalTypeId::BOOLEAN);
	RequireSignature("st_geomfromwkb", {blob}, geometry);
	RequireSignature("st_isvalid", {geometry}, boolean);
	RequireSignature("st_point", {double_type, double_type}, geometry);
	RequireSignature("st_covers", {geometry, geometry}, boolean);
}

std::string RegionGeometryAdapter::SpatialVersion() const {
	auto &manager = ExtensionManager::Get(context);
	auto info = manager.GetExtensionInfo("spatial");
	if (!info) DependencyError("loaded spatial extension version is unavailable");
	std::lock_guard<std::mutex> guard(info->lock);
	if (!info->is_loaded || !info->install_info || info->install_info->version.empty())
		DependencyError("loaded spatial extension version is unavailable");
	return info->install_info->version;
}

void RegionGeometryAdapter::RequireNearestOperations() const {
	const auto geometry = LogicalType::GEOMETRY();
	RequireSignature("st_pointonsurface", {geometry}, geometry);
	RequireSignature("st_x", {geometry}, LogicalType(LogicalTypeId::DOUBLE));
	RequireSignature("st_y", {geometry}, LogicalType(LogicalTypeId::DOUBLE));
}

void RegionGeometryAdapter::RequireSignature(const std::string &name, const std::vector<LogicalType> &arguments,
                                             const LogicalType &return_type) const {
	auto entry = Catalog::GetSystemCatalog(context).GetEntry<ScalarFunctionCatalogEntry>(
	    context, DEFAULT_SCHEMA, name, OnEntryNotFound::RETURN_NULL);
	if (!entry) {
		DependencyError("DuckDB spatial is required; explicitly load a matching spatial extension before using region reads");
	}
	for (const auto &function : entry->functions.functions) {
		if (ExactSignature(function, arguments, return_type)) return;
	}
	DependencyError("loaded spatial function " + name + " does not have the required exact signature");
}

unique_ptr<Expression> RegionGeometryAdapter::BindScalar(const std::string &name,
	                                                       const std::vector<LogicalType> &arguments,
	                                                       const LogicalType &return_type,
	                                                       std::vector<unique_ptr<Expression>> children) const {
	RequireSignature(name, arguments, return_type);
	auto &entry = Catalog::GetSystemCatalog(context).GetEntry<ScalarFunctionCatalogEntry>(context, DEFAULT_SCHEMA, name);
	ErrorData error;
	FunctionBinder binder(context);
	auto result = binder.BindScalarFunction(entry, std::move(children), error);
	if (!result) error.Throw("[spatial_dependency] failed to bind required spatial expression");
	if (result->return_type != return_type) {
		DependencyError("loaded spatial function " + name + " returned an unexpected type");
	}
	return result;
}

Value RegionGeometryAdapter::PointOnSurface(const Value &geometry) const {
	if (geometry.IsNull() || geometry.type() != LogicalType::GEOMETRY()) {
		throw BinderException("[region_sampling] representative point requires a non-NULL GEOMETRY value");
	}
	std::vector<unique_ptr<Expression>> arguments;
	arguments.push_back(make_uniq<BoundConstantExpression>(geometry));
	auto expression = BindScalar("st_pointonsurface", {LogicalType::GEOMETRY()}, LogicalType::GEOMETRY(),
	                            std::move(arguments));
	auto result = ExpressionExecutor::EvaluateScalar(context, *expression);
	if (result.IsNull() || result.type() != LogicalType::GEOMETRY()) {
		throw BinderException("[region_sampling] ST_PointOnSurface returned NULL or an unexpected type");
	}
	return result;
}

GridCoordinate RegionGeometryAdapter::PointOnSurfaceCoordinate(const Value &geometry) const {
	const auto point = PointOnSurface(geometry);
	auto bind_coordinate = [&](const std::string &name) {
		std::vector<unique_ptr<Expression>> arguments;
		arguments.push_back(make_uniq<BoundConstantExpression>(point));
		return BindScalar(name, {LogicalType::GEOMETRY()}, LogicalType(LogicalTypeId::DOUBLE), std::move(arguments));
	};
	const auto longitude = ExpressionExecutor::EvaluateScalar(context, *bind_coordinate("st_x"));
	const auto latitude = ExpressionExecutor::EvaluateScalar(context, *bind_coordinate("st_y"));
	if (longitude.IsNull() || latitude.IsNull())
		throw BinderException("[region_sampling] spatial point coordinate functions returned NULL");
	const auto x = longitude.GetValue<double>();
	const auto y = latitude.GetValue<double>();
	if (!std::isfinite(x) || !std::isfinite(y) || y < -90.0 || y > 90.0)
		throw BinderException("[region_sampling] representative point coordinates are not finite longitude/latitude");
	const auto covers = [&]() {
		std::vector<unique_ptr<Expression>> point_arguments;
		point_arguments.push_back(make_uniq<BoundConstantExpression>(Value::DOUBLE(x)));
		point_arguments.push_back(make_uniq<BoundConstantExpression>(Value::DOUBLE(y)));
		auto point_expression = BindScalar("st_point", {LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
		                                   LogicalType::GEOMETRY(), std::move(point_arguments));
		std::vector<unique_ptr<Expression>> cover_arguments;
		cover_arguments.push_back(make_uniq<BoundConstantExpression>(geometry));
		cover_arguments.push_back(std::move(point_expression));
		auto cover_expression = BindScalar("st_covers", {LogicalType::GEOMETRY(), LogicalType::GEOMETRY()},
		                                   LogicalType(LogicalTypeId::BOOLEAN), std::move(cover_arguments));
		return ExpressionExecutor::EvaluateScalar(context, *cover_expression);
	}();
	if (covers.IsNull() || !covers.GetValue<bool>())
		throw BinderException("[region_sampling] representative point is not covered by its source geometry");
	return {y, x};
}

Value RegionGeometryAdapter::CreateGeometry(const std::string &wkb) const {
	const auto blob = Value::BLOB(reinterpret_cast<const_data_ptr_t>(wkb.data()), wkb.size());
	std::vector<unique_ptr<Expression>> arguments;
	arguments.push_back(make_uniq<BoundConstantExpression>(blob));
	auto expression = BindScalar("st_geomfromwkb", {LogicalType(LogicalTypeId::BLOB)}, LogicalType::GEOMETRY(),
	                             std::move(arguments));
	auto result = ExpressionExecutor::EvaluateScalar(context, *expression);
	if (result.IsNull() || result.type() != LogicalType::GEOMETRY()) {
		DependencyError("ST_GeomFromWKB did not produce a non-NULL GEOMETRY");
	}
	return result;
}

bool RegionGeometryAdapter::IsValid(const Value &geometry) const {
	if (geometry.IsNull() || geometry.type() != LogicalType::GEOMETRY()) {
		throw BinderException("[region_input] topology validation requires a non-NULL GEOMETRY value");
	}
	std::vector<unique_ptr<Expression>> arguments;
	arguments.push_back(make_uniq<BoundConstantExpression>(geometry));
	auto expression = BindScalar("st_isvalid", {LogicalType::GEOMETRY()}, LogicalType(LogicalTypeId::BOOLEAN),
	                             std::move(arguments));
	const auto result = ExpressionExecutor::EvaluateScalar(context, *expression);
	return !result.IsNull() && result.GetValue<bool>();
}

unique_ptr<Expression> RegionGeometryAdapter::BindCovers(const Value &geometry, idx_t longitude_index,
	                                                      idx_t latitude_index) const {
	if (geometry.IsNull() || geometry.type() != LogicalType::GEOMETRY()) {
		throw BinderException("[region_input] point predicate requires a non-NULL GEOMETRY value");
	}
	std::vector<unique_ptr<Expression>> point_arguments;
	point_arguments.push_back(make_uniq<BoundReferenceExpression>(LogicalType(LogicalTypeId::DOUBLE),
	                                                              static_cast<storage_t>(longitude_index)));
	point_arguments.push_back(make_uniq<BoundReferenceExpression>(LogicalType(LogicalTypeId::DOUBLE),
	                                                              static_cast<storage_t>(latitude_index)));
	auto point = BindScalar("st_point", {LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
	                        LogicalType::GEOMETRY(), std::move(point_arguments));
	std::vector<unique_ptr<Expression>> covers_arguments;
	covers_arguments.push_back(make_uniq<BoundConstantExpression>(geometry));
	covers_arguments.push_back(std::move(point));
	return BindScalar("st_covers", {LogicalType::GEOMETRY(), LogicalType::GEOMETRY()},
	                  LogicalType(LogicalTypeId::BOOLEAN), std::move(covers_arguments));
}


RegionPredicateEvaluator::RegionPredicateEvaluator(ClientContext &context, const Expression &bound_expression)
	: expression(bound_expression.Copy()),
	  executor(make_uniq<ExpressionExecutor>(context, *expression)) {
}

idx_t RegionPredicateEvaluator::Select(DataChunk &coordinates, SelectionVector &selected) {
	if (coordinates.size() > REGION_GEOMETRY_BATCH_SIZE) {
		throw InvalidInputException("[region_input_limit] geometry evaluation batches cannot exceed 256 points");
	}
	return executor->SelectExpression(coordinates, selected);
}

} // namespace duckdb::duckomo
