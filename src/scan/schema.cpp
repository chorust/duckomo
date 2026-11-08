#include "duckomo/schema.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "duckdb/common/string_util.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckomo/read_om_binding.hpp"

namespace duckdb {
namespace duckomo {

namespace {

bool Utf8ByteLess(const std::string &left, const std::string &right) {
	return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
	                                    [](char left_byte, char right_byte) {
		                                    return static_cast<unsigned char>(left_byte) <
		                                           static_cast<unsigned char>(right_byte);
	                                    });
}

[[noreturn]] void InvalidSeriesDeclaration(const std::string &detail) {
	throw BinderException("read_om_series dimensions " + detail);
}

std::string ValidateDeclaredArrayPath(const std::string &path, bool allow_root_alias) {
	if (allow_root_alias && path == "value") return "/";
	if (path == "/") return path;
	if (path.empty() || path.front() != '/') {
		InvalidSeriesDeclaration("map keys must be '/' for the root array or absolute canonical array paths");
	}
	std::size_t segment_start = 1;
	for (std::size_t index = 1; index <= path.size(); index++) {
		if (index != path.size() && path[index] != '/') {
			if (path[index] == '%') {
				if (index + 2 >= path.size() ||
				    !((path[index + 1] == '2' && path[index + 2] == 'F') ||
				      (path[index + 1] == '2' && path[index + 2] == '5'))) {
					InvalidSeriesDeclaration("map keys must use canonical percent escapes in array paths");
				}
				index += 2;
			}
			continue;
		}
		if (index == segment_start) InvalidSeriesDeclaration("map array paths cannot contain empty segments");
		segment_start = index + 1;
	}
	return path;
}

std::vector<std::string> ReadDeclaredAxes(const Value &value) {
	if (value.IsNull() || value.type().id() != LogicalTypeId::LIST ||
	    ListType::GetChildType(value.type()).id() != LogicalTypeId::VARCHAR) {
		InvalidSeriesDeclaration("values must be non-NULL VARCHAR[] axis declarations");
	}
	const auto &children = ListValue::GetChildren(value);
	if (children.empty() || children.size() > OM_MAX_RANK) {
		InvalidSeriesDeclaration("axis declarations must contain between one and OM_MAX_RANK names");
	}
	std::vector<std::string> result;
	std::unordered_set<std::string> seen;
	std::size_t time_count = 0;
	result.reserve(children.size());
	for (const auto &child : children) {
		if (child.IsNull() || child.type().id() != LogicalTypeId::VARCHAR) {
			InvalidSeriesDeclaration("axis names must be non-NULL VARCHAR values");
		}
		auto name = StringValue::Get(child);
		if (name.empty() || !seen.emplace(name).second) {
			InvalidSeriesDeclaration("axis names must be nonempty and unique");
		}
		if (name == "time") ++time_count;
		result.emplace_back(std::move(name));
	}
	if (time_count != 1) InvalidSeriesDeclaration("must declare exactly one axis named 'time' per array");
	return result;
}

DeclaredSeriesArray MakeDeclaredArray(const std::string &path, const Value &axes_value) {
	DeclaredSeriesArray result;
	result.canonical_path = path;
	result.column_name = ArrayOutputColumnName(path);
	result.dimension_names = ReadDeclaredAxes(axes_value);
	const auto time_axis = std::find(result.dimension_names.begin(), result.dimension_names.end(), "time");
	result.time_axis_index = static_cast<std::size_t>(time_axis - result.dimension_names.begin());
	return result;
}

bool SameNonTimeShape(const ReadOmArrayBindingSignature &left, const ReadOmArrayBindingSignature &right) {
	if (left.canonical_path != right.canonical_path || left.column_name != right.column_name ||
	    left.type != right.type || left.dimension_names != right.dimension_names ||
	    left.shape.size() != right.shape.size()) {
		return false;
	}
	for (std::size_t axis = 0; axis < left.shape.size(); axis++) {
		if (left.dimension_names[axis] != "time" && left.shape[axis] != right.shape[axis]) return false;
	}
	return true;
}

bool SameLogicalBinding(const ReadOmBindingSignature &left, const ReadOmBindingSignature &right) {
	if (left.column_types != right.column_types || left.column_names != right.column_names ||
	    left.grid_id != right.grid_id || left.layout_id != right.layout_id ||
	    left.include_source != right.include_source || left.arrays.size() != right.arrays.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.arrays.size(); index++) {
		if (left.arrays[index].shape != right.arrays[index].shape ||
		    !SameNonTimeShape(left.arrays[index], right.arrays[index])) return false;
	}
	return true;
}

} // namespace

std::string ArrayOutputColumnName(const std::string &canonical_path) {
	return canonical_path == "/" ? "value" : canonical_path.substr(1);
}

void AppendArrayOutputColumn(const std::string &canonical_path, const LogicalType &type,
                             std::vector<LogicalType> &return_types, TableFunctionColumnNames &names) {
	return_types.emplace_back(type);
	names.emplace_back(ArrayOutputColumnName(canonical_path));
}

void AppendValueOutputColumns(const BoundSchema &schema, std::vector<LogicalType> &return_types,
                              TableFunctionColumnNames &names) {
	for (const auto &variable : schema.variables) {
		AppendArrayOutputColumn(variable.canonical_path, variable.type, return_types, names);
	}
}

DeclaredSeriesSchema BuildDeclaredSeriesSchema(const Value *dimensions, bool include_source,
                                               const std::optional<std::string> &variable) {
	if (dimensions == nullptr || dimensions->IsNull()) {
		InvalidSeriesDeclaration("must explicitly declare array dimensions for every chunk");
	}

	DeclaredSeriesSchema result;
	result.include_source = include_source;
	if (dimensions->type().id() == LogicalTypeId::LIST) {
		result.arrays.emplace_back(MakeDeclaredArray("/", *dimensions));
	} else if (dimensions->type().id() == LogicalTypeId::MAP &&
	           MapType::KeyType(dimensions->type()).id() == LogicalTypeId::VARCHAR &&
	           MapType::ValueType(dimensions->type()).id() == LogicalTypeId::LIST &&
	           ListType::GetChildType(MapType::ValueType(dimensions->type())).id() == LogicalTypeId::VARCHAR) {
		const auto &entries = MapValue::GetChildren(*dimensions);
		if (entries.empty()) InvalidSeriesDeclaration("map must declare at least one selected array");
		std::unordered_set<std::string> paths;
		result.arrays.reserve(entries.size());
		for (const auto &entry : entries) {
			const auto &parts = StructValue::GetChildren(entry);
			if (parts.size() != 2 || parts[0].IsNull()) {
				InvalidSeriesDeclaration("map contains a NULL or malformed array path");
			}
			const auto path = ValidateDeclaredArrayPath(StringValue::Get(parts[0]), true);
			if (!paths.emplace(path).second) InvalidSeriesDeclaration("map contains duplicate canonical array paths");
			result.arrays.emplace_back(MakeDeclaredArray(path, parts[1]));
		}
	} else {
		InvalidSeriesDeclaration("must be VARCHAR[] for the root array or MAP(VARCHAR, VARCHAR[]) for selected arrays");
	}

	if (variable) {
		if (variable->empty()) InvalidSeriesDeclaration("variable selector must not be empty");
		const auto selected_path = ValidateDeclaredArrayPath(
		    *variable == "value" ? std::string("value")
		                         : (variable->front() == '/' ? *variable : "/" + *variable),
		    true);
		if (result.arrays.size() != 1 || result.arrays.front().canonical_path != selected_path) {
			InvalidSeriesDeclaration("must describe exactly the array selected by variable");
		}
	}

	std::sort(result.arrays.begin(), result.arrays.end(), [](const DeclaredSeriesArray &left,
	                                                         const DeclaredSeriesArray &right) {
		return Utf8ByteLess(left.canonical_path, right.canonical_path);
	});
	if (result.arrays.size() > 1 && result.arrays.front().canonical_path == "/") {
		InvalidSeriesDeclaration("cannot mix the root array with named array paths");
	}
	for (std::size_t index = 1; index < result.arrays.size(); index++) {
		if (result.arrays[index].dimension_names != result.arrays.front().dimension_names) {
			InvalidSeriesDeclaration("must preserve one shared ordered dimension profile across selected arrays");
		}
	}

	BoundSchema output_schema;
	output_schema.variables.reserve(result.arrays.size());
	for (const auto &array : result.arrays) {
		BoundVariable variable_schema;
		variable_schema.canonical_path = array.canonical_path;
		variable_schema.column_name = array.column_name;
		variable_schema.type = array.type;
		output_schema.variables.emplace_back(std::move(variable_schema));
	}
	TableFunctionColumnNames names;
	AppendValueOutputColumns(output_schema, result.column_types, names);
	AppendSpatialOutputColumns(output_schema, result.column_types, names);
	AppendValidTimeOutputColumn("read_om_series", result.column_types, names);
	if (include_source) AppendOmSourceOutputColumn(output_schema, "read_om_series", result.column_types, names);
	result.column_names.reserve(names.size());
	for (const auto &name : names) result.column_names.emplace_back(TableFunctionColumnNameString(name));
	return result;
}

BoundSchema BuildBoundSchema(const OmMetadataTree &tree) {
	if (tree.arrays.empty()) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM file contains no supported Float32 arrays");
	}

	auto arrays = tree.arrays;
	std::sort(arrays.begin(), arrays.end(), [](const MetadataVariable &left, const MetadataVariable &right) {
		return Utf8ByteLess(left.canonical_path, right.canonical_path);
	});

	BoundSchema result;
	result.crs_wkt = tree.crs_wkt;
	result.crs_profile = tree.crs_profile;
	result.variables.reserve(arrays.size());
	std::vector<std::string> seen_names;
	seen_names.reserve(arrays.size());
	for (const auto &array : arrays) {
		if (array.canonical_path.empty() || array.shape.empty() || array.shape.size() > OM_MAX_RANK ||
		    array.shape.size() != array.chunk_shape.size()) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM metadata contains an incomplete array descriptor at '" + array.canonical_path + "'");
		}
		const auto row_count = CheckedShapeProduct(array.shape);
		if (row_count != array.row_count) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM array row count changed while building schema at '" + array.canonical_path + "'");
		}

		BoundVariable variable;
		variable.canonical_path = array.canonical_path;
		variable.column_name = ArrayOutputColumnName(array.canonical_path);
		variable.inferred_axes = array.inferred_axes;
		variable.time = array.time;
		variable.shape = array.shape;
		variable.chunk_shape = array.chunk_shape;
		variable.row_count = row_count;
		variable.metadata_offset = array.metadata_offset;
		variable.metadata_size = array.metadata_size;
		variable.metadata_owner = array.metadata_owner;

		for (const auto &existing_name : seen_names) {
			if (StringUtil::CIEquals(existing_name, variable.column_name)) {
				throw ReaderError(ReaderErrorCode::InvalidMetadata,
				                  "OM metadata paths produce duplicate DuckDB column name '" + variable.column_name +
				                      "' (conflicts with '" + existing_name + "')");
			}
		}
		seen_names.emplace_back(variable.column_name);
		result.variables.emplace_back(std::move(variable));
	}
	result.shape = result.variables.front().shape;
	result.row_count = result.variables.front().row_count;
	return result;
}

void ValidateSeriesChildSignature(const DeclaredSeriesSchema &declared,
                                  const ReadOmBindingSignature &child,
                                  std::int64_t chunk_time_length,
                                  const ReadOmBindingSignature *reference) {
	if (chunk_time_length <= 0 || declared.arrays.empty()) {
		throw BinderException("read_om_series has an invalid declared schema or time extent");
	}
	if (child.arrays.size() != declared.arrays.size() ||
	    child.column_names != declared.column_names || child.column_types != declared.column_types ||
	    child.include_source != declared.include_source) {
		throw BinderException("read_om_series child arrays or output signature differ from the declaration");
	}
	if (child.grid_id.empty() || child.layout_id.empty()) {
		throw BinderException("read_om_series child has no resolved grid_id/layout_id identity");
	}
	for (std::size_t index = 0; index < declared.arrays.size(); index++) {
		const auto &expected = declared.arrays[index];
		const auto &actual = child.arrays[index];
		if (actual.canonical_path != expected.canonical_path || actual.column_name != expected.column_name ||
		    actual.type != expected.type || actual.dimension_names != expected.dimension_names ||
		    actual.shape.size() != expected.dimension_names.size() ||
		    expected.time_axis_index >= actual.shape.size() ||
		    actual.dimension_names[expected.time_axis_index] != "time" ||
		    actual.shape[expected.time_axis_index] != static_cast<std::uint64_t>(chunk_time_length)) {
			throw BinderException("read_om_series child array declaration or time extent differs at '" +
			                      expected.canonical_path + "'");
		}
		if (index != 0 && actual.shape != child.arrays.front().shape) {
			throw BinderException("read_om_series selected arrays do not have identical logical shapes");
		}
	}
	if (reference != nullptr && !SameLogicalBinding(*reference, child)) {
		throw BinderException("read_om_series child grid, layout, array shape, or output signature differs between chunks");
	}
}

void AppendSpatialOutputColumns(const BoundSchema &schema, std::vector<LogicalType> &return_types,
	                            TableFunctionColumnNames &names) {
	for (const auto &variable : schema.variables) {
		if (StringUtil::CIEquals(variable.column_name, "lat") ||
		    StringUtil::CIEquals(variable.column_name, "lon")) {
			throw BinderException("read_om coordinate column name conflicts with source array '" + variable.column_name + "'");
		}
	}
	return_types.emplace_back(LogicalType::DOUBLE);
	names.emplace_back("lat");
	return_types.emplace_back(LogicalType::DOUBLE);
	names.emplace_back("lon");
}

void AppendValidTimeOutputColumn(const std::string &function_name,
                                 std::vector<LogicalType> &return_types, TableFunctionColumnNames &names) {
	for (const auto &existing_name : names) {
		if (StringUtil::CIEquals(TableFunctionColumnNameString(existing_name), "valid_time")) {
			throw BinderException(function_name + " valid_time column name conflicts with a source array");
		}
	}
	return_types.emplace_back(LogicalType::TIMESTAMP);
	names.emplace_back("valid_time");
}

LogicalType OmSourceType() {
	return LogicalType::STRUCT({
	    {"object_id", LogicalType::VARCHAR},
	    {"object_version", LogicalType::VARCHAR},
	    {"version_strength", LogicalType::VARCHAR},
	    {"content_verified", LogicalType::BOOLEAN},
	    {"grid_id", LogicalType::VARCHAR},
	    {"layout_id", LogicalType::VARCHAR},
	    {"logical_index", LogicalType::UBIGINT},
	    {"point_index", LogicalType::UBIGINT},
	    {"parent_point_index", LogicalType::UBIGINT},
	    {"axis_indices", LogicalType::LIST(LogicalType::UBIGINT)},
	});
}

void AppendOmSourceOutputColumn(const BoundSchema &schema, const std::string &function_name,
                                std::vector<LogicalType> &return_types, TableFunctionColumnNames &names) {
	(void)schema;
	for (const auto &existing_name : names) {
		if (StringUtil::CIEquals(TableFunctionColumnNameString(existing_name), "om_source")) {
			throw BinderException(function_name + " om_source column name conflicts with source array '" +
			                      TableFunctionColumnNameString(existing_name) + "'");
		}
	}
	return_types.emplace_back(OmSourceType());
	names.emplace_back("om_source");
}

} // namespace duckomo
} // namespace duckdb
