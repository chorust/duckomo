#include "duckomo/projection.hpp"

#include <unordered_map>

#include "duckdb/common/exception.hpp"

namespace duckdb {
namespace duckomo {

ProjectionPlan::ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids)
    : ProjectionPlan(schema, column_ids, false) {
}

ProjectionPlan::ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids,
                               bool has_spatial_columns)
    : ProjectionPlan(schema, column_ids, has_spatial_columns, false) {
}

ProjectionPlan::ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids,
                               bool has_spatial_columns, bool has_time_column)
	: ProjectionPlan(schema, column_ids, has_spatial_columns, has_time_column, {}) {
}

ProjectionPlan::ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids,
	                           bool has_spatial_columns, bool has_time_column,
	                           const SemanticAxes &semantic_axes, bool has_source_column) {
	std::vector<OutputColumn> schema_slots;
	schema_slots.reserve(schema.variables.size() + (has_spatial_columns ? 2 : 0) + (has_time_column ? 1 : 0) +
	                     semantic_axes.size() + (has_source_column ? 1 : 0));
	for (idx_t variable_index = 0; variable_index < schema.variables.size(); variable_index++) {
		OutputColumn slot;
		slot.source_index = variable_index;
		schema_slots.emplace_back(slot);
	}
	if (has_spatial_columns) {
		OutputColumn latitude;
		latitude.kind = OutputColumnKind::Latitude;
		schema_slots.emplace_back(latitude);
		OutputColumn longitude;
		longitude.kind = OutputColumnKind::Longitude;
		schema_slots.emplace_back(longitude);
	}
	if (has_time_column) {
		OutputColumn time;
		time.kind = OutputColumnKind::ValidTime;
		schema_slots.emplace_back(time);
	}
	for (idx_t semantic_index = 0; semantic_index < semantic_axes.size(); semantic_index++) {
		if (semantic_axes[semantic_index].kind == SemanticAxisKind::Time) continue;
		OutputColumn coordinate;
		coordinate.kind = OutputColumnKind::SemanticCoordinate;
		coordinate.semantic_axis_index = semantic_index;
		schema_slots.emplace_back(coordinate);
	}
	if (has_source_column) {
		OutputColumn source;
		source.kind = OutputColumnKind::Source;
		schema_slots.emplace_back(source);
	}

	output_slots.reserve(column_ids.size());
	required_variable_ids.reserve(column_ids.size());
	std::unordered_map<idx_t, idx_t> required_variable_indexes;
	required_variable_indexes.reserve(column_ids.size());
	for (const auto column_id : column_ids) {
		if (column_id == COLUMN_IDENTIFIER_EMPTY) {
			OutputColumn slot;
			slot.kind = OutputColumnKind::Cardinality;
			has_cardinality_slot = true;
			output_slots.emplace_back(slot);
			continue;
		}
		if (column_id >= schema_slots.size()) {
			throw InternalException("read_om received projected column id %llu for schema with %llu output columns",
			                        static_cast<unsigned long long>(column_id),
			                        static_cast<unsigned long long>(schema_slots.size()));
		}
		auto slot = schema_slots[column_id];
		if (slot.kind != OutputColumnKind::Value) {
			output_slots.emplace_back(slot);
			continue;
		}
		const auto variable_index = slot.source_index;
		auto required_entry = required_variable_indexes.find(variable_index);
		if (required_entry == required_variable_indexes.end()) {
			slot.required_variable_index = required_variable_ids.size();
			required_variable_indexes.emplace(variable_index, slot.required_variable_index);
			required_variable_ids.emplace_back(variable_index);
		} else {
			slot.required_variable_index = required_entry->second;
		}
		output_slots.emplace_back(slot);
	}
}

} // namespace duckomo
} // namespace duckdb
