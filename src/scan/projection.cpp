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
	                           const SemanticAxes &semantic_axes, bool has_source_column,
	                           bool has_region_id_column, bool has_sampling_column) {
	std::vector<OutputColumn> schema_slots;
	schema_slots.reserve(schema.variables.size() + (has_spatial_columns ? 2 : 0) + (has_time_column ? 1 : 0) +
	                     semantic_axes.size() + (has_source_column ? 1 : 0) + (has_region_id_column ? 1 : 0) +
	                     (has_sampling_column ? 1 : 0));
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
	if (has_region_id_column) {
		OutputColumn region_id;
		region_id.kind = OutputColumnKind::RegionId;
		schema_slots.emplace_back(region_id);
	}
	if (has_sampling_column) {
		OutputColumn sampling;
		sampling.kind = OutputColumnKind::Sampling;
		schema_slots.emplace_back(sampling);
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
	bool sampling_requested = false;
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
		if (slot.kind == OutputColumnKind::Sampling) sampling_requested = true;
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
	if (sampling_requested) {
		requires_sampling_variables = true;
		for (idx_t variable_index = 0; variable_index < schema.variables.size(); ++variable_index) {
			if (required_variable_indexes.find(variable_index) != required_variable_indexes.end()) continue;
			required_variable_indexes.emplace(variable_index, required_variable_ids.size());
			required_variable_ids.emplace_back(variable_index);
		}
	}
}

namespace {
std::vector<column_t> PrimaryColumnIds(const std::vector<ColumnIndex> &column_indexes) {
	std::vector<column_t> result;
	result.reserve(column_indexes.size());
	for (const auto &index : column_indexes) {
		if (!index.HasPrimaryIndex()) throw InternalException("read_om received a named output column index");
		result.push_back(index.GetPrimaryIndex());
	}
	return result;
}

void AddVariableDependency(idx_t variable_index, std::unordered_map<idx_t, idx_t> &indexes,
                           std::vector<idx_t> &required) {
	if (indexes.find(variable_index) != indexes.end()) return;
	indexes.emplace(variable_index, required.size());
	required.push_back(variable_index);
}
} // namespace

ProjectionPlan::ProjectionPlan(const BoundSchema &schema, const std::vector<ColumnIndex> &column_indexes,
                               bool has_spatial_columns, bool has_time_column, const SemanticAxes &semantic_axes,
                               bool has_source_column, bool has_region_id_column, bool has_sampling_column,
                               bool supports_sampling_extract)
    : ProjectionPlan(schema, PrimaryColumnIds(column_indexes), has_spatial_columns, has_time_column, semantic_axes,
                     has_source_column, has_region_id_column, has_sampling_column) {
	std::unordered_map<idx_t, idx_t> variable_indexes;
	variable_indexes.reserve(column_indexes.size());
	required_variable_ids.clear();
	requires_sampling_variables = false;
	for (idx_t output = 0; output < output_slots.size(); ++output) {
		auto &slot = output_slots[output];
		const auto &column_index = column_indexes[output];
		if (slot.kind == OutputColumnKind::Value) {
			AddVariableDependency(slot.source_index, variable_indexes, required_variable_ids);
			slot.required_variable_index = variable_indexes.at(slot.source_index);
			continue;
		}
		if (slot.kind != OutputColumnKind::Sampling) continue;
		if (column_index.IsPushdownExtract() && column_index.HasChildren()) {
			const auto &children = column_index.GetChildIndexes();
			if (!supports_sampling_extract || children.size() != 1 || !children[0].HasPrimaryIndex() ||
			    children[0].HasChildren()) {
				throw InternalException("read_om received an unsupported nearest sampling extract path");
			}
			if (children[0].HasPrimaryIndex() == false || !children[0].HasType() ||
			    children[0].GetType() != column_index.GetScanType()) {
				throw InternalException("read_om nested sampling leaf type disagrees with DuckDB's projection");
			}
			const auto field_index = children[0].GetPrimaryIndex();
			if (field_index > 14) throw InternalException("read_om received an unknown nearest sampling field");
			slot.kind = OutputColumnKind::SamplingField;
			slot.sampling_field_index = field_index;
			if (field_index == 14) {
				requires_sampling_variables = true;
				for (idx_t variable = 0; variable < schema.variables.size(); ++variable)
					AddVariableDependency(variable, variable_indexes, required_variable_ids);
			}
		} else {
			if (column_index.HasChildren())
				throw InternalException("read_om received partial sampling projection without pushdown-extract support");
			requires_sampling_variables = true;
			for (idx_t variable = 0; variable < schema.variables.size(); ++variable)
				AddVariableDependency(variable, variable_indexes, required_variable_ids);
		}
	}
}

} // namespace duckomo
} // namespace duckdb
