#pragma once

#include <vector>

#include "duckdb/common/constants.hpp"
#include "duckdb/common/typedefs.hpp"
#include "duckomo/schema.hpp"

namespace duckdb {
namespace duckomo {

// An output slot refers to a value variable, a synthetic coordinate/time, or
// DuckDB's internal empty column carrying cardinality. Only physical value
// variables address ProjectionPlan::RequiredVariableIds.
struct ProjectionOutputSlot final {
	bool is_cardinality = false;
	bool is_latitude = false;
	bool is_longitude = false;
	bool is_valid_time = false;
	idx_t variable_index = DConstants::INVALID_INDEX;
	idx_t required_variable_index = DConstants::INVALID_INDEX;
};

// Immutable mapping from DuckDB's requested scan columns to the bound schema.
// Required variables are kept in first-use order and occur only once, while
// output slots retain their original order and any duplicate requests.
class ProjectionPlan final {
public:
	ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids);
	ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids, bool has_spatial_columns);
	ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids, bool has_spatial_columns,
	               bool has_time_column);

	const std::vector<ProjectionOutputSlot> &GetOutputSlots() const {
		return output_slots;
	}

	const std::vector<idx_t> &GetRequiredVariableIds() const {
		return required_variable_ids;
	}

	bool IsCardinalityOnly() const {
		return required_variable_ids.empty();
	}

	bool HasCardinalitySlot() const {
		return has_cardinality_slot;
	}

private:
	std::vector<ProjectionOutputSlot> output_slots;
	std::vector<idx_t> required_variable_ids;
	bool has_cardinality_slot = false;
};

} // namespace duckomo
} // namespace duckdb
