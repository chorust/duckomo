#pragma once

#include <string>
#include <vector>

#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/types/selection_vector.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckomo/regular_grid.hpp"

namespace duckdb {
class ClientContext;
class ExpressionExecutor;
}

namespace duckdb::duckomo {

constexpr idx_t REGION_GEOMETRY_BATCH_SIZE = 256;

class RegionGeometryAdapter final {
public:
	explicit RegionGeometryAdapter(ClientContext &context);

	std::string SpatialVersion() const;
	void RequireNearestOperations() const;
	Value CreateGeometry(const std::string &wkb) const;
	Value PointOnSurface(const Value &geometry) const;
	GridCoordinate PointOnSurfaceCoordinate(const Value &geometry) const;
	bool IsValid(const Value &geometry) const;
	unique_ptr<Expression> BindCovers(const Value &geometry, idx_t longitude_index, idx_t latitude_index) const;

private:
	unique_ptr<Expression> BindScalar(const std::string &name, const std::vector<LogicalType> &arguments,
	                                  const LogicalType &return_type,
	                                  std::vector<unique_ptr<Expression>> children) const;
	void RequireSignature(const std::string &name, const std::vector<LogicalType> &arguments,
	                      const LogicalType &return_type) const;

	ClientContext &context;
};

class RegionPredicateEvaluator final {
public:
	RegionPredicateEvaluator(ClientContext &context, const Expression &bound_expression);
	~RegionPredicateEvaluator();

	idx_t Select(DataChunk &coordinates, SelectionVector &selected);

private:
	unique_ptr<Expression> expression;
	unique_ptr<ExpressionExecutor> executor;
};

} // namespace duckdb::duckomo
