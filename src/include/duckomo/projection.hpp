#pragma once

#include <cstdint>
#include <vector>

#include "duckdb/common/constants.hpp"
#include "duckdb/common/typedefs.hpp"
#include "duckomo/schema.hpp"
#include "duckomo/semantic_axes.hpp"

namespace duckdb {
namespace duckomo {

enum class OutputColumnKind : std::uint8_t {
	Value,
	Latitude,
	Longitude,
	ValidTime,
	SemanticCoordinate,
	Source,
	Cardinality
};

// Typed output descriptor. Only Value columns address the required variable
// list; synthetic coordinates carry their source mapping explicitly.
struct OutputColumn final {
	OutputColumnKind kind = OutputColumnKind::Value;
	idx_t source_index = DConstants::INVALID_INDEX;
	idx_t required_variable_index = DConstants::INVALID_INDEX;
	idx_t semantic_axis_index = DConstants::INVALID_INDEX;
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
	ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids, bool has_spatial_columns,
	               bool has_time_column, const SemanticAxes &semantic_axes, bool has_source_column = false);

	const std::vector<OutputColumn> &GetOutputSlots() const {
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
	std::vector<OutputColumn> output_slots;
	std::vector<idx_t> required_variable_ids;
	bool has_cardinality_slot = false;
};

} // namespace duckomo
} // namespace duckdb
