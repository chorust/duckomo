#pragma once

#include <vector>

#include "duckdb/common/constants.hpp"
#include "duckdb/common/typedefs.hpp"
#include "duckomo/schema.hpp"

namespace duckdb {
namespace duckomo {

// An output slot either refers to one metadata variable or to DuckDB's
// internal empty column, which carries cardinality without reading a value
// array. Physical-variable indexes address ProjectionPlan::RequiredVariableIds.
struct ProjectionOutputSlot final {
	bool is_cardinality = false;
	idx_t variable_index = DConstants::INVALID_INDEX;
	idx_t required_variable_index = DConstants::INVALID_INDEX;
};

// Immutable mapping from DuckDB's requested scan columns to the bound schema.
// Required variables are kept in first-use order and occur only once, while
// output slots retain their original order and any duplicate requests.
class ProjectionPlan final {
public:
	ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids);

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
