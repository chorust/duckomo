#include "duckomo/dimensions.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {
namespace duckomo {

namespace {

std::string AxisInputError(const std::string &detail) {
	return "read_om dimensions parameter " + detail;
}

std::vector<std::string> ReadAxes(const Value &value, const BoundVariable &variable) {
	if (value.IsNull()) {
		throw BinderException(AxisInputError("contains a NULL axis list for '" + variable.canonical_path + "'"));
	}
	if (value.type().id() != LogicalTypeId::LIST || ListType::GetChildType(value.type()).id() != LogicalTypeId::VARCHAR) {
		throw BinderException(AxisInputError("values must be VARCHAR[] for '" + variable.canonical_path + "'"));
	}
	const auto &children = ListValue::GetChildren(value);
	if (children.size() != variable.shape.size()) {
		throw BinderException(AxisInputError("axis count must match rank " + std::to_string(variable.shape.size()) +
		                                     " for '" + variable.canonical_path + "'"));
	}
	std::vector<std::string> result;
	result.reserve(children.size());
	std::unordered_set<std::string> unique_axes;
	for (const auto &axis_value : children) {
		if (axis_value.IsNull()) {
			throw BinderException(AxisInputError("must not contain NULL axis names for '" + variable.canonical_path + "'"));
		}
		if (axis_value.type().id() != LogicalTypeId::VARCHAR) {
			throw BinderException(AxisInputError("axis names must be VARCHAR values for '" + variable.canonical_path + "'"));
		}
		auto axis = StringValue::Get(axis_value);
		if (axis.empty()) {
			throw BinderException(AxisInputError("axis names must not be empty for '" + variable.canonical_path + "'"));
		}
		if (!unique_axes.emplace(axis).second) {
			throw BinderException(AxisInputError("contains duplicate axis '" + axis + "' for '" +
			                                     variable.canonical_path + "'"));
		}
		result.emplace_back(std::move(axis));
	}
	return result;
}

} // namespace

AxisDeclarations ValidateAxisDeclarations(const Value *dimensions, const BoundSchema &schema) {
	if (schema.variables.empty()) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "read_om requires at least one supported array");
	}
	if (dimensions == nullptr || dimensions->IsNull()) {
		if (schema.variables.size() > 1) {
			AxisDeclarations inferred;
			inferred.reserve(schema.variables.size());
			for (const auto &variable : schema.variables) {
				if (variable.shape != schema.shape || variable.row_count != schema.row_count ||
				    variable.inferred_axes.empty() ||
				    (!inferred.empty() && inferred.front() != variable.inferred_axes)) {
					throw BinderException(AxisInputError(
					    "is required for multiple arrays without matching shapes and ordered coordinates metadata"));
				}
				inferred.emplace_back(variable.inferred_axes);
			}
			return inferred;
		}
		return AxisDeclarations(1, schema.variables.front().inferred_axes);
	}
	if (dimensions->type().id() != LogicalTypeId::MAP ||
	    MapType::KeyType(dimensions->type()).id() != LogicalTypeId::VARCHAR ||
	    MapType::ValueType(dimensions->type()).id() != LogicalTypeId::LIST ||
	    ListType::GetChildType(MapType::ValueType(dimensions->type())).id() != LogicalTypeId::VARCHAR) {
		throw BinderException(AxisInputError("must be MAP(VARCHAR, VARCHAR[])"));
	}

	const auto &entries = MapValue::GetChildren(*dimensions);
	std::unordered_map<std::string, const BoundVariable *> variables_by_path;
	variables_by_path.reserve(schema.variables.size());
	for (const auto &variable : schema.variables) {
		variables_by_path.emplace(variable.canonical_path, &variable);
		variables_by_path.emplace(variable.column_name, &variable);
	}
	if (entries.size() != schema.variables.size()) {
		throw BinderException(AxisInputError("keys must exactly cover every array path"));
	}

	std::unordered_map<std::string, std::vector<std::string>> axes_by_path;
	axes_by_path.reserve(entries.size());
	for (const auto &entry : entries) {
		const auto &parts = StructValue::GetChildren(entry);
		if (parts.size() != 2 || parts[0].IsNull()) {
			throw BinderException(AxisInputError("contains a NULL or malformed array path"));
		}
		const auto path = StringValue::Get(parts[0]);
		const auto found = variables_by_path.find(path);
		if (found == variables_by_path.end()) {
			throw BinderException(AxisInputError("contains unknown array path '" + path + "'"));
		}
		if (!axes_by_path.emplace(found->second->canonical_path, ReadAxes(parts[1], *found->second)).second) {
			throw BinderException(AxisInputError("contains duplicate array path '" + path + "'"));
		}
	}

	AxisDeclarations result;
	result.reserve(schema.variables.size());
	for (const auto &variable : schema.variables) {
		const auto found = axes_by_path.find(variable.canonical_path);
		if (found == axes_by_path.end()) {
			throw BinderException(AxisInputError("is missing array path '" + variable.column_name + "'"));
		}
		if (variable.shape != schema.shape || variable.row_count != schema.row_count) {
			throw BinderException("read_om arrays do not have identical shapes: '" + variable.canonical_path + "'");
		}
		if (!variable.inferred_axes.empty() && found->second != variable.inferred_axes) {
			throw BinderException("read_om dimensions axis names conflict with coordinates metadata for array '" +
			                      variable.column_name + "'");
		}
		if (!result.empty() && result.front() != found->second) {
			throw BinderException("read_om dimensions axis order differs for array '" + variable.canonical_path + "'");
		}
		result.emplace_back(found->second);
	}
	return result;
}

} // namespace duckomo
} // namespace duckdb
