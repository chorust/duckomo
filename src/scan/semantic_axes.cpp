#include "duckomo/semantic_axes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/interval.hpp"
#include "duckomo/compat/duckdb_api.hpp"

namespace duckdb {
namespace duckomo {

namespace {

using FieldMap = std::unordered_map<std::string, const Value *>;

FieldMap ReadFields(const Value &value, const std::string &where) {
	if (value.IsNull() || value.type().id() != LogicalTypeId::STRUCT) {
		throw BinderException(where + "must be a non-NULL STRUCT");
	}
	const auto &names = StructType::GetChildTypes(value.type());
	const auto &values = StructValue::GetChildren(value);
	if (names.size() != values.size()) {
		throw BinderException(where + "has malformed STRUCT fields");
	}
	FieldMap result;
	for (idx_t index = 0; index < names.size(); index++) {
		const auto name = IdentifierNameString(names[index].first);
		if (!result.emplace(name, &values[index]).second) {
			throw BinderException(where + "contains duplicate field '" + name + "'");
		}
	}
	return result;
}

const Value &RequiredField(const FieldMap &fields, const std::string &name, const std::string &where) {
	auto entry = fields.find(name);
	if (entry == fields.end() || entry->second->IsNull()) {
		throw BinderException(where + "is missing non-NULL field '" + name + "'");
	}
	return *entry->second;
}

void RejectUnknownFields(const FieldMap &fields, const std::unordered_set<std::string> &allowed,
	                     const std::string &where) {
	for (const auto &entry : fields) {
		if (allowed.find(entry.first) == allowed.end()) {
			throw BinderException(where + "contains unknown field '" + entry.first + "'");
		}
	}
}

std::string ReadString(const Value &value, const std::string &where) {
	if (value.IsNull() || value.type().id() != LogicalTypeId::VARCHAR) {
		throw BinderException(where + "must be a non-NULL VARCHAR");
	}
	return StringValue::Get(value);
}

std::int64_t IntervalMicros(const Value &value, const std::string &where) {
	if (value.IsNull() || value.type().id() != LogicalTypeId::INTERVAL) {
		throw BinderException(where + "must be a non-NULL INTERVAL");
	}
	const auto interval = value.GetValue<interval_t>();
	if (interval.months != 0) {
		throw BinderException(where + " cannot contain calendar months");
	}
	const __int128 total = static_cast<__int128>(interval.days) * Interval::MICROS_PER_DAY + interval.micros;
	if (total < std::numeric_limits<std::int64_t>::min() || total > std::numeric_limits<std::int64_t>::max()) {
		throw BinderException(where + " exceeds the signed 64-bit microsecond range");
	}
	return static_cast<std::int64_t>(total);
}

timestamp_t ReadTimestamp(const Value &value, const std::string &where) {
	if (value.IsNull()) {
		throw BinderException(where + " must be a finite timestamp");
	}
	timestamp_t result;
	if (value.type().id() == LogicalTypeId::TIMESTAMP) {
		result = value.GetValue<timestamp_t>();
	} else if (value.type().id() == LogicalTypeId::TIMESTAMP_TZ) {
		result = timestamp_t(value.GetValue<timestamp_tz_t>().value);
	} else {
		throw BinderException(where + " must be TIMESTAMP or TIMESTAMPTZ");
	}
	if (!Value::IsFinite(result)) {
		throw BinderException(where + " must be finite");
	}
	return result;
}

double ReadFiniteDouble(const Value &value, const std::string &where) {
	if (value.IsNull() || !value.type().IsNumeric()) {
		throw BinderException(where + " must be a non-NULL numeric value");
	}
	double result;
	try {
		result = value.DefaultCastAs(LogicalType::DOUBLE, true).GetValue<double>();
	} catch (const std::exception &) {
		throw BinderException(where + " cannot be represented as DOUBLE");
	}
	if (!std::isfinite(result)) {
		throw BinderException(where + " must be finite");
	}
	if (value.type().id() != LogicalTypeId::DOUBLE) {
		try {
			const auto round_trip = Value::DOUBLE(result).DefaultCastAs(value.type(), true);
			if (!Value::NotDistinctFrom(value, round_trip)) {
				throw BinderException(where + " cannot be represented losslessly as DOUBLE");
			}
		} catch (const BinderException &) {
			throw;
		} catch (const std::exception &) {
			throw BinderException(where + " cannot be represented losslessly as DOUBLE");
		}
	}
	return result;
}

std::int64_t ReadInt64(const Value &value, const std::string &where) {
	if (value.IsNull() || !value.type().IsIntegral()) {
		throw BinderException(where + " must be an integer value");
	}
	try {
		return value.DefaultCastAs(LogicalType::BIGINT, true).GetValue<std::int64_t>();
	} catch (const std::exception &) {
		throw BinderException(where + " must fit in a signed 64-bit integer");
	}
}

bool HasField(const FieldMap &fields, const std::string &name) {
	return fields.find(name) != fields.end();
}

SemanticAxisKind ParseKind(const std::string &name) {
	if (name == "time") return SemanticAxisKind::Time;
	if (name == "level") return SemanticAxisKind::Level;
	if (name == "lead_time") return SemanticAxisKind::LeadTime;
	if (name == "member") return SemanticAxisKind::Member;
	if (name == "run") return SemanticAxisKind::Run;
	throw BinderException("read_om axes contains unknown semantic key '" + name + "'");
}

void ValidateLevelMetadata(const FieldMap &fields, SemanticAxis &axis, const std::string &where) {
	axis.level_kind = ReadString(RequiredField(fields, "kind", where), where + "field 'kind'");
	axis.unit = ReadString(RequiredField(fields, "unit", where), where + "field 'unit'");
	const bool valid = (axis.level_kind == "pressure" && (axis.unit == "Pa" || axis.unit == "hPa")) ||
	                   (axis.level_kind == "height" && axis.unit == "m") ||
	                   (axis.level_kind == "model" && axis.unit == "1");
	if (!valid) {
		throw BinderException(where + "has unsupported level kind/unit '" + axis.level_kind + "/" + axis.unit + "'");
	}
}

void AppendRegular(SemanticAxis &axis, const Value &start, const Value &step, const std::string &where) {
	const auto count = axis.axis_length;
	if (count > std::numeric_limits<idx_t>::max()) {
		throw BinderException(where + "axis length exceeds the addressable coordinate count");
	}
	const auto last_index = count - 1;
	switch (axis.kind) {
	case SemanticAxisKind::Time:
	case SemanticAxisKind::Run: {
		const auto initial = ReadTimestamp(start, where + "field 'start'").value;
		const auto increment = IntervalMicros(step, where + "field 'step'");
		const __int128 last = static_cast<__int128>(initial) + static_cast<__int128>(increment) * last_index;
		if (last < std::numeric_limits<std::int64_t>::min() || last > std::numeric_limits<std::int64_t>::max() ||
		    !Value::IsFinite(timestamp_t(static_cast<std::int64_t>(last)))) {
			throw BinderException(where + "regular timestamp coordinate overflows at index " +
			                      std::to_string(last_index));
		}
		axis.regular = true;
		axis.timestamp_start = timestamp_t(initial);
		axis.timestamp_step_micros = increment;
		axis.output_type = LogicalType::TIMESTAMP;
		break;
	}
	case SemanticAxisKind::Level: {
		const auto initial = ReadFiniteDouble(start, where + "field 'start'");
		const auto increment = ReadFiniteDouble(step, where + "field 'step'");
		const auto last = initial + increment * static_cast<double>(last_index);
		if (!std::isfinite(last)) {
			throw BinderException(where + "regular level coordinate is not finite at index " +
			                      std::to_string(last_index));
		}
		if (axis.level_kind == "pressure" && std::min(initial, last) <= 0) {
			throw BinderException(where + "pressure level coordinates must be greater than zero");
		}
		if (axis.level_kind == "model" &&
		    (std::trunc(initial) != initial || std::trunc(last) != last ||
		     (count > 1 && std::trunc(increment) != increment))) {
			throw BinderException(where + "model level coordinates must be integers");
		}
		axis.regular = true;
		axis.number_start = initial;
		axis.number_step = increment;
		axis.output_type = LogicalType::DOUBLE;
		break;
	}
	case SemanticAxisKind::LeadTime: {
		const auto initial = IntervalMicros(start, where + "field 'start'");
		const auto increment = IntervalMicros(step, where + "field 'step'");
		const __int128 last = static_cast<__int128>(initial) + static_cast<__int128>(increment) * last_index;
		if (last < std::numeric_limits<std::int64_t>::min() || last > std::numeric_limits<std::int64_t>::max()) {
			throw BinderException(where + "regular lead_time overflows at index " + std::to_string(last_index));
		}
		axis.regular = true;
		axis.duration_start_micros = initial;
		axis.duration_step_micros = increment;
		axis.output_type = LogicalType::INTERVAL;
		break;
	}
	case SemanticAxisKind::Member: {
		const auto initial = ReadInt64(start, where + "field 'start'");
		const auto increment = ReadInt64(step, where + "field 'step'");
		const __int128 last = static_cast<__int128>(initial) + static_cast<__int128>(increment) * last_index;
		if (last < std::numeric_limits<std::int64_t>::min() || last > std::numeric_limits<std::int64_t>::max()) {
			throw BinderException(where + "regular member overflows at index " + std::to_string(last_index));
		}
		axis.regular = true;
		axis.integer_member_start = initial;
		axis.integer_member_step = increment;
		axis.output_type = LogicalType::BIGINT;
		break;
	}
	}
}

void AppendExplicit(SemanticAxis &axis, const Value &values, const std::string &where) {
	if (values.type().id() != LogicalTypeId::LIST) {
		throw BinderException(where + "field 'values' must be a typed LIST");
	}
	const auto &items = ListValue::GetChildren(values);
	if (items.empty() || items.size() != axis.axis_length) {
		throw BinderException(where + "coordinate count must equal positive axis length " +
		                      std::to_string(axis.axis_length));
	}
	const auto child_type = ListType::GetChildType(values.type()).id();
	switch (axis.kind) {
	case SemanticAxisKind::Time:
	case SemanticAxisKind::Run:
		if (child_type != LogicalTypeId::TIMESTAMP && child_type != LogicalTypeId::TIMESTAMP_TZ) {
			throw BinderException(where + "field 'values' must be TIMESTAMP[] or TIMESTAMPTZ[]");
		}
		axis.timestamps.reserve(items.size());
		for (const auto &item : items) axis.timestamps.emplace_back(ReadTimestamp(item, where + "coordinate"));
		axis.output_type = LogicalType::TIMESTAMP;
		break;
	case SemanticAxisKind::Level:
		if (!ListType::GetChildType(values.type()).IsNumeric()) {
			throw BinderException(where + "field 'values' must contain numeric levels");
		}
		axis.numbers.reserve(items.size());
		for (const auto &item : items) {
			auto number = ReadFiniteDouble(item, where + "coordinate");
			if (axis.level_kind == "pressure" && number <= 0) {
				throw BinderException(where + "pressure level coordinates must be greater than zero");
			}
			if (axis.level_kind == "model" && std::trunc(number) != number) {
				throw BinderException(where + "model level coordinates must be integers");
			}
			axis.numbers.emplace_back(number);
		}
		axis.output_type = LogicalType::DOUBLE;
		break;
	case SemanticAxisKind::LeadTime:
		if (child_type != LogicalTypeId::INTERVAL) {
			throw BinderException(where + "field 'values' must be INTERVAL[]");
		}
		axis.durations.reserve(items.size());
		for (const auto &item : items) axis.durations.push_back(interval_t{0, 0, IntervalMicros(item, where + "coordinate")});
		axis.output_type = LogicalType::INTERVAL;
		break;
	case SemanticAxisKind::Member:
		if (child_type == LogicalTypeId::VARCHAR) {
			axis.text_members.reserve(items.size());
			for (const auto &item : items) axis.text_members.emplace_back(ReadString(item, where + "coordinate"));
			axis.output_type = LogicalType::VARCHAR;
		} else if (ListType::GetChildType(values.type()).IsIntegral()) {
			axis.integer_members.reserve(items.size());
			for (const auto &item : items) axis.integer_members.emplace_back(ReadInt64(item, where + "coordinate"));
			axis.output_type = LogicalType::BIGINT;
		} else {
			throw BinderException(where + "member values must contain only integers or only VARCHAR identifiers");
		}
		break;
	}
}

SemanticAxis BindOne(const std::string &semantic_name, const Value &declaration,
	                 const BoundSchema &schema, const AxisDeclarations &declared_axes,
	                 const std::vector<std::string> &spatial_axis_names) {
	const auto kind = ParseKind(semantic_name);
	const auto where = "read_om axes." + semantic_name + " ";
	const auto fields = ReadFields(declaration, where);
	const auto axis_name = ReadString(RequiredField(fields, "axis", where), where + "field 'axis'");
	if (axis_name.empty()) {
		throw BinderException(where + "field 'axis' must not be empty");
	}
	if (schema.variables.empty() || declared_axes.empty() || declared_axes.front().empty()) {
		throw BinderException(where + "requires named logical axes; supply the dimensions parameter");
	}
	const auto &names = declared_axes.front();
	auto axis_entry = std::find(names.begin(), names.end(), axis_name);
	if (axis_entry == names.end()) {
		throw BinderException(where + "references unknown logical axis '" + axis_name + "'");
	}
	const auto axis_index = static_cast<idx_t>(axis_entry - names.begin());
	if (axis_index >= schema.shape.size() || schema.shape[axis_index] == 0) {
		throw BinderException(where + "references an invalid or empty logical axis '" + axis_name + "'");
	}
	if (std::find(spatial_axis_names.begin(), spatial_axis_names.end(), axis_name) != spatial_axis_names.end()) {
		throw BinderException(where + "cannot map spatial axis '" + axis_name + "' to a semantic coordinate");
	}
	for (std::size_t variable = 0; variable < schema.variables.size(); variable++) {
		if (variable >= declared_axes.size() || declared_axes[variable] != names ||
		    schema.variables[variable].shape != schema.shape) {
			throw BinderException(where + "cannot map incompatible multi-variable axis layouts");
		}
	}

	SemanticAxis result;
	result.kind = kind;
	result.axis_name = axis_name;
	result.axis_index = axis_index;
	result.axis_length = schema.shape[axis_index];
	result.stride = 1;
	for (idx_t index = axis_index + 1; index < schema.shape.size(); index++) {
		if (result.stride > std::numeric_limits<std::uint64_t>::max() / schema.shape[index]) {
			throw BinderException(where + "logical axis stride overflows");
		}
		result.stride *= schema.shape[index];
	}

	const bool has_values = HasField(fields, "values");
	const bool has_start = HasField(fields, "start");
	const bool has_step = HasField(fields, "step");
	if (has_values == (has_start || has_step) || has_start != has_step) {
		throw BinderException(where + "must specify either values or both start and step");
	}
	std::unordered_set<std::string> allowed = {"axis"};
	if (has_values) {
		allowed.emplace("values");
	} else {
		allowed.emplace("start");
		allowed.emplace("step");
	}
	if (kind == SemanticAxisKind::Level && !result.regular) {
		allowed.emplace("kind");
		allowed.emplace("unit");
		ValidateLevelMetadata(fields, result, where);
	}
	RejectUnknownFields(fields, allowed, where);
	if (kind != SemanticAxisKind::Level && (HasField(fields, "kind") || HasField(fields, "unit"))) {
		throw BinderException(where + "does not accept level kind/unit fields");
	}
	if (has_values) {
		AppendExplicit(result, RequiredField(fields, "values", where), where);
	} else {
		AppendRegular(result, RequiredField(fields, "start", where), RequiredField(fields, "step", where), where);
	}
	if (kind == SemanticAxisKind::Level) {
		for (const auto level : result.numbers) {
			if (result.level_kind == "pressure" && level <= 0) {
				throw BinderException(where + "pressure level coordinates must be greater than zero");
			}
			if (result.level_kind == "model" && std::trunc(level) != level) {
				throw BinderException(where + "model level coordinates must be integers");
			}
		}
	}

	for (const auto &variable : schema.variables) {
		if (!variable.time) continue;
		if (kind != SemanticAxisKind::Time) continue;
		if (variable.time->scalar) {
			throw BinderException(where + "conflicts with scalar valid_time file metadata");
		}
		if (axis_name != "time") {
			throw BinderException(where + "conflicts with file time coordinate evidence on axis 'time'");
		}
		if (variable.time->epoch_seconds.size() != result.axis_length ||
		    (!result.regular && result.timestamps.size() != result.axis_length)) {
			throw BinderException(where + "coordinate length conflicts with file time evidence");
		}
		for (idx_t coordinate = 0; coordinate < result.axis_length; coordinate++) {
			const auto seconds = variable.time->epoch_seconds[coordinate];
			const __int128 configured = result.regular
			                                ? static_cast<__int128>(result.timestamp_start.value) +
			                                      static_cast<__int128>(result.timestamp_step_micros) * coordinate
			                                : static_cast<__int128>(result.timestamps[coordinate].value);
			if (seconds > std::numeric_limits<std::int64_t>::max() / 1000000 ||
			    seconds < std::numeric_limits<std::int64_t>::min() / 1000000 ||
			    configured != static_cast<__int128>(seconds) * 1000000) {
				throw BinderException(where + "coordinates conflict with file time evidence at position " +
				                      std::to_string(coordinate));
			}
		}
	}
	return result;
}

} // namespace

idx_t SemanticAxis::CoordinateIndex(std::uint64_t logical_position) const {
	if (axis_length == 0 || stride == 0) {
		throw InternalException("semantic axis has an invalid length or stride");
	}
	return static_cast<idx_t>((logical_position / stride) % axis_length);
}

Value SemanticAxis::CoordinateValue(std::uint64_t logical_position) const {
	const auto index = CoordinateIndex(logical_position);
	switch (kind) {
	case SemanticAxisKind::Time:
	case SemanticAxisKind::Run:
		if (regular) {
			const __int128 value = static_cast<__int128>(timestamp_start.value) +
			                       static_cast<__int128>(timestamp_step_micros) * index;
			return Value::TIMESTAMP(timestamp_t(static_cast<std::int64_t>(value)));
		}
		return Value::TIMESTAMP(timestamps.at(index));
	case SemanticAxisKind::Level:
		return Value::DOUBLE(regular ? number_start + number_step * static_cast<double>(index) : numbers.at(index));
	case SemanticAxisKind::LeadTime:
		if (regular) {
			const __int128 value = static_cast<__int128>(duration_start_micros) +
			                       static_cast<__int128>(duration_step_micros) * index;
			return Value::INTERVAL(interval_t{0, 0, static_cast<std::int64_t>(value)});
		}
		return Value::INTERVAL(durations.at(index));
	case SemanticAxisKind::Member:
		if (output_type.id() == LogicalTypeId::VARCHAR) return Value(text_members.at(index));
		if (regular) {
			const __int128 value = static_cast<__int128>(integer_member_start) +
			                       static_cast<__int128>(integer_member_step) * index;
			return Value::BIGINT(static_cast<std::int64_t>(value));
		}
		return Value::BIGINT(integer_members.at(index));
	}
	throw InternalException("unknown semantic axis kind");
}

bool SemanticAxis::operator==(const SemanticAxis &other) const {
	return kind == other.kind && axis_name == other.axis_name && axis_index == other.axis_index &&
	       axis_length == other.axis_length && stride == other.stride && output_type == other.output_type &&
	       regular == other.regular && timestamp_start == other.timestamp_start &&
	       timestamp_step_micros == other.timestamp_step_micros && number_start == other.number_start &&
	       number_step == other.number_step && duration_start_micros == other.duration_start_micros &&
	       duration_step_micros == other.duration_step_micros && integer_member_start == other.integer_member_start &&
	       integer_member_step == other.integer_member_step && level_kind == other.level_kind && unit == other.unit &&
	       timestamps == other.timestamps &&
	       numbers == other.numbers && durations == other.durations && integer_members == other.integer_members &&
	       text_members == other.text_members;
}

SemanticAxes BindSemanticAxes(const Value *value, const BoundSchema &schema, const AxisDeclarations &declared_axes,
	                          const std::vector<std::string> &spatial_axis_names) {
	if (value == nullptr || value->IsNull()) return {};
	if (value->type().id() != LogicalTypeId::STRUCT) {
		throw BinderException("read_om axes must be a STRUCT with semantic keys time, level, lead_time, member, or run");
	}
	const auto &names = StructType::GetChildTypes(value->type());
	const auto &values = StructValue::GetChildren(*value);
	if (names.size() != values.size()) {
		throw BinderException("read_om axes STRUCT is malformed");
	}
	SemanticAxes result;
	std::unordered_set<std::string> used_names;
	std::unordered_set<std::string> used_axes;
	for (idx_t index = 0; index < names.size(); index++) {
		const auto semantic_name = IdentifierNameString(names[index].first);
		const auto kind = ParseKind(semantic_name);
		(void)kind;
		if (!used_names.emplace(semantic_name).second) {
			throw BinderException("read_om axes repeats semantic key '" + semantic_name + "'");
		}
		auto axis = BindOne(semantic_name, values[index], schema, declared_axes, spatial_axis_names);
		if (!used_axes.emplace(axis.axis_name).second) {
			throw BinderException("read_om axes maps logical axis '" + axis.axis_name + "' more than once");
		}
		result.emplace_back(std::move(axis));
	}
	auto order = [](SemanticAxisKind kind) {
		switch (kind) {
		case SemanticAxisKind::Time: return 0;
		case SemanticAxisKind::Level: return 1;
		case SemanticAxisKind::LeadTime: return 2;
		case SemanticAxisKind::Member: return 3;
		case SemanticAxisKind::Run: return 4;
		}
		return 5;
	};
	std::stable_sort(result.begin(), result.end(), [&](const SemanticAxis &left, const SemanticAxis &right) {
		return order(left.kind) < order(right.kind);
	});
	return result;
}

const char *SemanticAxisName(SemanticAxisKind kind) noexcept {
	switch (kind) {
	case SemanticAxisKind::Time: return "time";
	case SemanticAxisKind::Level: return "level";
	case SemanticAxisKind::LeadTime: return "lead_time";
	case SemanticAxisKind::Member: return "member";
	case SemanticAxisKind::Run: return "run";
	}
	return "unknown";
}

std::string SemanticOutputName(SemanticAxisKind kind) {
	return kind == SemanticAxisKind::Time ? "valid_time" : SemanticAxisName(kind);
}

} // namespace duckomo
} // namespace duckdb
