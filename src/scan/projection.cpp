#include "duckomo/projection.hpp"

#include <unordered_map>

#include "duckdb/common/exception.hpp"

namespace duckdb {
namespace duckomo {

ProjectionPlan::ProjectionPlan(const BoundSchema &schema, const std::vector<column_t> &column_ids) {
	output_slots.reserve(column_ids.size());
	required_variable_ids.reserve(column_ids.size());
	std::unordered_map<idx_t, idx_t> required_variable_indexes;
	required_variable_indexes.reserve(column_ids.size());

	for (const auto column_id : column_ids) {
		ProjectionOutputSlot slot;
		if (column_id == COLUMN_IDENTIFIER_EMPTY) {
			slot.is_cardinality = true;
			has_cardinality_slot = true;
			output_slots.emplace_back(slot);
			continue;
		}

		if (column_id >= schema.variables.size()) {
			throw InternalException("read_om received projected column id %llu for schema with %llu variables",
			                        static_cast<unsigned long long>(column_id),
			                        static_cast<unsigned long long>(schema.variables.size()));
		}

		slot.variable_index = column_id;
		auto required_entry = required_variable_indexes.find(column_id);
		if (required_entry == required_variable_indexes.end()) {
			slot.required_variable_index = required_variable_ids.size();
			required_variable_indexes.emplace(column_id, slot.required_variable_index);
			required_variable_ids.emplace_back(column_id);
		} else {
			slot.required_variable_index = required_entry->second;
		}
		output_slots.emplace_back(slot);
	}
}

} // namespace duckomo
} // namespace duckdb
