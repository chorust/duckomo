#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo/metrics.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

extern "C" {
#include "om_decoder.h"
#include "om_file.h"
#include "om_variable.h"
}

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	ExtensionHelper::LoadExternalExtension(*database.instance, database.GetFileSystem(),
	                                       "./build/release/extension/duckomo/duckomo.duckdb_extension");
}
} // namespace duckdb

namespace {

namespace fs = std::filesystem;

constexpr const char *PROJECTION_FIXTURE = "test/data/projection.om";
constexpr const char *RAW_FIXTURE = "test/data/raw.om";
constexpr const char *RAW_LARGE_FIXTURE = "test/data/raw_large.om";
constexpr const char *PROJECTION_SHA256 = "fb36284bf8f932c82b171c7bce28b6e767b95321d1d986dc0a1dbd3b84d88a43";
constexpr const char *RAW_SHA256 = "6a34044749250c0de21d65d40d4f6c270c3ae31d16ac623626bc6b2f7a01ced3";

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		result.push_back(character);
		if (character == '\'') {
			result.push_back('\'');
		}
	}
	result.push_back('\'');
	return result;
}

std::string ReadProjection(const std::string &path = PROJECTION_FIXTURE) {
	return "read_om(" + SqlLiteral(path) +
	       ", dimensions := map(['/humidity', '/pressure', '/temperature'], "
	       "[['row', 'column'], ['row', 'column'], ['row', 'column']]))";
}

std::unique_ptr<duckdb::MaterializedQueryResult> RequireSuccess(duckdb::Connection &connection,
	                                                               const std::string &sql,
	                                                               const std::string &description) {
	auto result = connection.Query(sql);
	Require(result != nullptr, description + ": query returned no result");
	if (result->HasError()) {
		throw std::runtime_error(description + ": " + result->GetError());
	}
	return result;
}

std::string RequireFailure(duckdb::Connection &connection, const std::string &sql,
	                         const std::string &description) {
	auto result = connection.Query(sql);
	Require(result != nullptr, description + ": query returned no result");
	Require(result->HasError(), description + ": query unexpectedly succeeded");
	const auto error = result->GetError();
	result.reset();
	return error;
}

std::vector<std::uint8_t> ReadBytes(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "cannot open OM fixture: " + path.string());
	return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	Require(output.good(), "cannot create temporary OM fixture: " + path.string());
	output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Require(output.good(), "cannot write temporary OM fixture: " + path.string());
}

std::uint64_t FirstPayloadOffset(const std::vector<std::uint8_t> &bytes, std::uint64_t root_offset,
	                              std::uint64_t root_size) {
	Require(root_offset <= bytes.size() && root_size <= bytes.size() - root_offset,
	        "OM root metadata lies outside the fixture");
	const auto *variable = om_variable_init(bytes.data() + root_offset);
	Require(variable != nullptr && om_variable_get_type(variable) == DATA_TYPE_FLOAT_ARRAY,
	        "payload corruption fixture must have a root Float32 array");
	const auto rank = om_variable_get_dimensions_count(variable);
	const auto *dimensions = om_variable_get_dimensions(variable);
	Require(rank > 0 && rank <= 8 && dimensions != nullptr,
	        "payload corruption fixture has invalid array dimensions");
	std::vector<std::uint64_t> read_offset(static_cast<std::size_t>(rank), 0);
	std::vector<std::uint64_t> read_count(dimensions, dimensions + rank);
	std::vector<std::uint64_t> cube_offset(static_cast<std::size_t>(rank), 0);
	std::vector<std::uint64_t> cube_dimensions = read_count;
	OmDecoder_t decoder{};
	Require(om_decoder_init(&decoder, variable, rank, read_offset.data(), read_count.data(), cube_offset.data(),
	                        cube_dimensions.data(), 512, 64 * 1024) == ERROR_OK,
	        "official OM decoder could not initialize the corruption fixture");

	const auto trailer_size = static_cast<std::uint64_t>(om_trailer_size());
	Require(bytes.size() >= trailer_size, "corruption fixture is shorter than an OM trailer");
	const auto body_end = static_cast<std::uint64_t>(bytes.size()) - trailer_size;
	OmDecoder_indexRead_t index_read{};
	om_decoder_init_index_read(&decoder, &index_read);
	while (om_decoder_next_index_read(&decoder, &index_read)) {
		Require(index_read.offset <= body_end && index_read.count <= body_end - index_read.offset,
		        "official decoder requested an invalid lookup-table range");
		OmDecoder_dataRead_t data_read{};
		om_decoder_init_data_read(&data_read, &index_read);
		OmError_t read_error = ERROR_OK;
		while (om_decoder_next_data_read(&decoder, &data_read, bytes.data() + index_read.offset,
		                                 index_read.count, &read_error)) {
			Require(data_read.count > 0 && data_read.offset < body_end &&
			            data_read.count <= body_end - data_read.offset,
			        "official decoder requested an invalid encoded payload range");
			return data_read.offset;
		}
		Require(read_error == ERROR_OK, "official decoder rejected the corruption fixture lookup table");
	}
	throw std::runtime_error("official decoder did not request an encoded payload range");
}

class TemporaryCorruptFixture final {
public:
	explicit TemporaryCorruptFixture(const fs::path &source) {
		auto bytes = ReadBytes(source);
		const auto trailer_size = static_cast<std::uint64_t>(om_trailer_size());
		Require(bytes.size() >= trailer_size, "source OM fixture is shorter than its trailer");
		std::uint64_t root_offset = 0;
		std::uint64_t root_size = 0;
		Require(om_trailer_read(bytes.data() + bytes.size() - trailer_size, &root_offset, &root_size),
		        "source OM fixture has no valid trailer");
		Require(root_offset + root_size == bytes.size() - trailer_size,
		        "source OM root metadata is not at the end of the body");
		const auto payload_offset = FirstPayloadOffset(bytes, root_offset, root_size);
		// FPX blocks start with the shift amount. 255 is outside the supported
		// range and makes the official decoder fail after the query scan starts.
		bytes.at(static_cast<std::size_t>(payload_offset)) = 255;

		static std::atomic<std::uint64_t> next_id{0};
		path_ = fs::temp_directory_path() /
		        ("duckomo-projection-corrupt-" + std::to_string(static_cast<unsigned long long>(getpid())) + "-" +
		         std::to_string(next_id.fetch_add(1)) + ".om");
		try {
			WriteBytes(path_, bytes);
		} catch (...) {
			std::error_code ignored;
			fs::remove(path_, ignored);
			path_.clear();
			throw;
		}
	}

	TemporaryCorruptFixture(const TemporaryCorruptFixture &) = delete;
	TemporaryCorruptFixture &operator=(const TemporaryCorruptFixture &) = delete;
	~TemporaryCorruptFixture() {
		if (!path_.empty()) {
			std::error_code ignored;
			fs::remove(path_, ignored);
		}
	}

	std::string Path() const {
		return path_.string();
	}

private:
	fs::path path_;
};

struct JsonValue final {
	enum class Kind { Null, Boolean, Number, String, Object, Array } kind = Kind::Null;
	bool boolean = false;
	std::string scalar;
	std::map<std::string, JsonValue> object;
	std::vector<JsonValue> array;

	const JsonValue &At(const std::string &key) const {
		Require(kind == Kind::Object, "expected a JSON object while reading key '" + key + "'");
		const auto entry = object.find(key);
		Require(entry != object.end(), "metrics JSON is missing field '" + key + "'");
		return entry->second;
	}

	const JsonValue *Find(const std::string &key) const {
		Require(kind == Kind::Object, "expected a JSON object while looking up key '" + key + "'");
		const auto entry = object.find(key);
		return entry == object.end() ? nullptr : &entry->second;
	}

	std::string AsString(const std::string &description) const {
		Require(kind == Kind::String, description + " must be a JSON string");
		return scalar;
	}

	std::uint64_t AsUInt(const std::string &description) const {
		Require(kind == Kind::Number && !scalar.empty() && scalar.find_first_of("-.eE") == std::string::npos,
		        description + " must be an unsigned JSON integer");
		std::size_t parsed = 0;
		const auto result = std::stoull(scalar, &parsed);
		Require(parsed == scalar.size(), description + " is not a valid unsigned JSON integer");
		return result;
	}

	bool AsBool(const std::string &description) const {
		Require(kind == Kind::Boolean, description + " must be a JSON boolean");
		return boolean;
	}
};

class JsonParser final {
public:
	explicit JsonParser(const std::string &text_p) : text(text_p) {
	}

	JsonValue Parse() {
		auto value = ParseValue();
		SkipWhitespace();
		Require(position == text.size(), "metrics JSON has trailing data");
		return value;
	}

private:
	void SkipWhitespace() {
		while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) {
			position++;
		}
	}

	char Take() {
		Require(position < text.size(), "metrics JSON ended unexpectedly");
		return text[position++];
	}

	void Expect(char expected) {
		SkipWhitespace();
		Require(Take() == expected, std::string("metrics JSON expected '") + expected + "'");
	}

	static void AppendUtf8(std::string &result, std::uint32_t codepoint) {
		if (codepoint <= 0x7f) {
			result.push_back(static_cast<char>(codepoint));
		} else if (codepoint <= 0x7ff) {
			result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
			result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		} else {
			result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
			result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		}
	}

	std::uint32_t ParseHexQuad() {
		Require(position + 4 <= text.size(), "metrics JSON has a truncated Unicode escape");
		std::uint32_t value = 0;
		for (std::size_t offset = 0; offset < 4; offset++) {
			const auto character = text[position++];
			value <<= 4;
			if (character >= '0' && character <= '9') {
				value |= static_cast<std::uint32_t>(character - '0');
			} else if (character >= 'a' && character <= 'f') {
				value |= static_cast<std::uint32_t>(character - 'a' + 10);
			} else if (character >= 'A' && character <= 'F') {
				value |= static_cast<std::uint32_t>(character - 'A' + 10);
			} else {
				throw std::runtime_error("metrics JSON has an invalid Unicode escape");
			}
		}
		return value;
	}

	std::string ParseString() {
		Expect('"');
		std::string result;
		while (true) {
			const auto character = Take();
			if (character == '"') {
				return result;
			}
			if (character != '\\') {
				Require(static_cast<unsigned char>(character) >= 0x20, "metrics JSON has an unescaped control character");
				result.push_back(character);
				continue;
			}
			switch (Take()) {
			case '"':
				result.push_back('"');
				break;
			case '\\':
				result.push_back('\\');
				break;
			case '/':
				result.push_back('/');
				break;
			case 'b':
				result.push_back('\b');
				break;
			case 'f':
				result.push_back('\f');
				break;
			case 'n':
				result.push_back('\n');
				break;
			case 'r':
				result.push_back('\r');
				break;
			case 't':
				result.push_back('\t');
				break;
			case 'u':
				AppendUtf8(result, ParseHexQuad());
				break;
			default:
				throw std::runtime_error("metrics JSON has an invalid string escape");
			}
		}
	}

	JsonValue ParseValue() {
		SkipWhitespace();
		Require(position < text.size(), "metrics JSON ended before a value");
		const auto next = text[position];
		if (next == '{') {
			position++;
			JsonValue result;
			result.kind = JsonValue::Kind::Object;
			SkipWhitespace();
			if (position < text.size() && text[position] == '}') {
				position++;
				return result;
			}
			while (true) {
				const auto key = ParseString();
				Expect(':');
				auto inserted = result.object.emplace(key, ParseValue());
				Require(inserted.second, "metrics JSON repeats object key '" + key + "'");
				SkipWhitespace();
				const auto separator = Take();
				if (separator == '}') {
					return result;
				}
				Require(separator == ',', "metrics JSON object has an invalid separator");
			}
		}
		if (next == '[') {
			position++;
			JsonValue result;
			result.kind = JsonValue::Kind::Array;
			SkipWhitespace();
			if (position < text.size() && text[position] == ']') {
				position++;
				return result;
			}
			while (true) {
				result.array.emplace_back(ParseValue());
				SkipWhitespace();
				const auto separator = Take();
				if (separator == ']') {
					return result;
				}
				Require(separator == ',', "metrics JSON array has an invalid separator");
			}
		}
		if (next == '"') {
			JsonValue result;
			result.kind = JsonValue::Kind::String;
			result.scalar = ParseString();
			return result;
		}
		if (text.compare(position, 4, "true") == 0 || text.compare(position, 5, "false") == 0) {
			JsonValue result;
			result.kind = JsonValue::Kind::Boolean;
			result.boolean = text.compare(position, 4, "true") == 0;
			position += result.boolean ? 4 : 5;
			return result;
		}
		if (text.compare(position, 4, "null") == 0) {
			position += 4;
			return JsonValue();
		}
		JsonValue result;
		result.kind = JsonValue::Kind::Number;
		const auto start = position;
		if (text[position] == '-') {
			position++;
		}
		while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
			position++;
		}
		if (position < text.size() && text[position] == '.') {
			position++;
			while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
				position++;
			}
		}
		if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
			position++;
			if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
				position++;
			}
			while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
				position++;
			}
		}
		Require(position > start, "metrics JSON contains an invalid value");
		result.scalar = text.substr(start, position - start);
		return result;
	}

	const std::string &text;
	std::size_t position = 0;
};

class MetricsOutput final {
public:
	MetricsOutput() {
		static std::atomic<std::uint64_t> next_id{0};
		path = fs::temp_directory_path() /
		       ("duckomo-projection-metrics-" + std::to_string(static_cast<unsigned long long>(getpid())) + "-" +
		        std::to_string(next_id.fetch_add(1)) + ".json");
		SetEnvironment("DUCKOMO_METRICS_OUTPUT", path.string());
		SetEnvironment("DUCKOMO_SCENARIO", "native_projection_evidence");
	}

	MetricsOutput(const MetricsOutput &) = delete;
	MetricsOutput &operator=(const MetricsOutput &) = delete;
	~MetricsOutput() {
		for (const auto &entry : previous_environment) {
			if (entry.second) {
				setenv(entry.first.c_str(), entry.second->c_str(), 1);
			} else {
				unsetenv(entry.first.c_str());
			}
		}
		std::error_code ignored;
		fs::remove(path, ignored);
	}

	void SetFixture(const std::string &fixture_id, const std::string &sha256) {
		SetEnvironment("DUCKOMO_FIXTURE_ID", fixture_id);
		SetEnvironment("DUCKOMO_FIXTURE_SHA256", sha256);
	}

	void SetScenario(const std::string &scenario) {
		SetEnvironment("DUCKOMO_SCENARIO", scenario);
	}

	void Clear() const {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(output.good(), "cannot clear query metrics sidecar");
	}

	JsonValue Read() const {
		std::ifstream input(path, std::ios::binary);
		Require(input.good(), "query did not write its metrics sidecar");
		std::string line;
		Require(static_cast<bool>(std::getline(input, line)) && !line.empty(),
		        "query metrics sidecar does not contain a JSON record");
		std::string extra;
		Require(!std::getline(input, extra), "query metrics sidecar contains more than one query record");
		return JsonParser(line).Parse();
	}

private:
	void SetEnvironment(const std::string &name, const std::string &value) {
		if (previous_environment.find(name) == previous_environment.end()) {
			const auto *old_value = std::getenv(name.c_str());
			previous_environment.emplace(name, old_value == nullptr ? std::optional<std::string>()
			                                                     : std::optional<std::string>(old_value));
		}
		Require(setenv(name.c_str(), value.c_str(), 1) == 0, "cannot set environment variable " + name);
	}

	fs::path path;
	std::map<std::string, std::optional<std::string>> previous_environment;
};

JsonValue RunSuccessWithMetrics(duckdb::Connection &connection, MetricsOutput &metrics,
	                           const std::string &fixture_id, const std::string &fixture_hash,
	                           const std::string &scenario, const std::string &sql,
	                           const std::string &description) {
	metrics.SetFixture(fixture_id, fixture_hash);
	metrics.SetScenario(scenario);
	metrics.Clear();
	auto result = RequireSuccess(connection, sql, description);
	result.reset();
	auto record = metrics.Read();
	Require(record.At("status").AsString("metrics status") == "success",
	        description + ": expected a successful query-scoped metric record");
	Require(record.At("scenario").AsString("metrics scenario") == scenario,
	        description + ": sidecar scenario does not match this query");
	Require(!record.At("query_id").AsString("metrics query_id").empty(),
	        description + ": sidecar query_id is empty");
	return record;
}

struct VariableCounts final {
	std::uint64_t index_bytes = 0;
	std::uint64_t index_requests = 0;
	std::uint64_t data_bytes = 0;
	std::uint64_t data_requests = 0;
	std::uint64_t decoded_chunks = 0;
	bool decode_count_complete = true;
};

VariableCounts CountsFor(const JsonValue &record, const std::string &path) {
	const auto &variables = record.At("variables");
	const auto *variable = variables.Find(path);
	if (variable == nullptr) {
		return VariableCounts();
	}
	VariableCounts counts;
	counts.index_bytes = variable->At("index_bytes").AsUInt(path + ".index_bytes");
	counts.index_requests = variable->At("index_requests").AsUInt(path + ".index_requests");
	counts.data_bytes = variable->At("data_bytes").AsUInt(path + ".data_bytes");
	counts.data_requests = variable->At("data_requests").AsUInt(path + ".data_requests");
	counts.decoded_chunks = variable->At("decoded_chunks").AsUInt(path + ".decoded_chunks");
	counts.decode_count_complete = variable->At("decode_count_complete").AsBool(path + ".decode_count_complete");
	return counts;
}

std::uint64_t SumDataBytes(const JsonValue &record) {
	std::uint64_t result = 0;
	for (const auto *path : {"/humidity", "/pressure", "/temperature"}) {
		result += CountsFor(record, path).data_bytes;
	}
	return result;
}

std::uint64_t SumDataRequests(const JsonValue &record) {
	std::uint64_t result = 0;
	for (const auto *path : {"/humidity", "/pressure", "/temperature"}) {
		result += CountsFor(record, path).data_requests;
	}
	return result;
}

std::uint64_t SumDecodedChunks(const JsonValue &record) {
	std::uint64_t result = 0;
	for (const auto *path : {"/humidity", "/pressure", "/temperature"}) {
		result += CountsFor(record, path).decoded_chunks;
	}
	return result;
}

void RequireVariableRead(const JsonValue &record, const std::string &path, const std::string &description) {
	const auto counts = CountsFor(record, path);
	Require(counts.index_bytes > 0 && counts.index_requests > 0 && counts.data_bytes > 0 &&
	            counts.data_requests > 0 && counts.decoded_chunks > 0 && counts.decode_count_complete,
	        description + ": expected actual index/data reads and completed decoder calls for " + path);
}

void RequireVariableUnread(const JsonValue &record, const std::string &path, const std::string &description) {
	const auto counts = CountsFor(record, path);
	Require(counts.index_bytes == 0 && counts.index_requests == 0 && counts.data_bytes == 0 &&
	            counts.data_requests == 0 && counts.decoded_chunks == 0,
	        description + ": omitted variable incurred physical reads or decode work: " + path);
}

bool EligibleForPerformance(const JsonValue &record) {
	if (record.At("status").AsString("metrics status") != "success" ||
	    !record.At("decode_count_complete").AsBool("decode_count_complete")) {
		return false;
	}
	const auto &variables = record.At("variables");
	for (const auto &entry : variables.object) {
		if (!entry.second.At("decode_count_complete").AsBool(entry.first + ".decode_count_complete")) {
			return false;
		}
	}
	return true;
}

void TestProjectionMetrics(duckdb::Connection &connection, MetricsOutput &metrics) {
	const auto read_om = ReadProjection();
	auto full = RunSuccessWithMetrics(connection, metrics, "projection", PROJECTION_SHA256, "native_full_scan",
	                                  "SELECT \"/humidity\", \"/pressure\", \"/temperature\" FROM " + read_om,
	                                  "full projection metrics scan");
	Require(full.At("schema_version").AsUInt("schema_version") == 1, "metrics schema version must be 1");
	Require(full.At("metadata_bytes").AsUInt("metadata_bytes") > 0 &&
	            full.At("metadata_requests").AsUInt("metadata_requests") > 0,
	        "full scan must record metadata reads separately");
	for (const auto *path : {"/humidity", "/pressure", "/temperature"}) {
		RequireVariableRead(full, path, "full scan");
	}
	Require(EligibleForPerformance(full), "complete successful full scan must be eligible for performance comparisons");

	metrics.SetFixture("projection", PROJECTION_SHA256);
	metrics.SetScenario("native_single_variable");
	// The environment must be set before BindReadOm reads its query identity.
	metrics.Clear();
	auto single_result = RequireSuccess(connection, "SELECT \"/temperature\" FROM " + read_om,
	                                    "single-variable metrics scan");
	Require(single_result->RowCount() == 10541, "single-variable projection returned the wrong row count");
	single_result.reset();
	auto single = metrics.Read();
	Require(single.At("status").AsString("single status") == "success", "single-variable scan must succeed");
	RequireVariableRead(single, "/temperature", "single-variable scan");
	RequireVariableUnread(single, "/humidity", "single-variable scan");
	RequireVariableUnread(single, "/pressure", "single-variable scan");
	Require(SumDataBytes(single) < SumDataBytes(full),
	        "single-variable projection must read strictly fewer actual data bytes than the full scan");
	Require(SumDataRequests(single) < SumDataRequests(full),
	        "single-variable projection must issue fewer data reads than the full scan");
	Require(SumDecodedChunks(single) < SumDecodedChunks(full),
	        "single-variable projection must decode fewer chunks than the full scan");
	Require(EligibleForPerformance(single), "complete successful single-variable scan must be performance eligible");

	metrics.SetFixture("projection", PROJECTION_SHA256);
	metrics.SetScenario("native_filter_dependency");
	metrics.Clear();
	auto filtered = RequireSuccess(connection,
	                               "SELECT \"/temperature\" FROM " + read_om + " WHERE \"/humidity\" = 96",
	                               "projection with an unselected filter variable");
	Require(filtered->RowCount() == 108, "filter-dependency query returned an unexpected row count");
	filtered.reset();
	auto filter_metrics = metrics.Read();
	Require(filter_metrics.At("status").AsString("filter status") == "success",
	        "filter-dependency scan must succeed");
	RequireVariableRead(filter_metrics, "/temperature", "filter dependency");
	RequireVariableRead(filter_metrics, "/humidity", "unselected filter dependency");
	RequireVariableUnread(filter_metrics, "/pressure", "filter dependency");

	metrics.SetFixture("projection", PROJECTION_SHA256);
	metrics.SetScenario("native_count_only");
	metrics.Clear();
	auto count = RequireSuccess(connection, "SELECT count(*) FROM " + read_om, "cardinality-only query");
	Require(count->RowCount() == 1 && count->GetValue(0, 0).GetValue<std::int64_t>() == 10541,
	        "count-only query returned an unexpected count");
	count.reset();
	auto count_metrics = metrics.Read();
	Require(count_metrics.At("status").AsString("count status") == "success", "count-only scan must succeed");
	for (const auto *path : {"/humidity", "/pressure", "/temperature"}) {
		RequireVariableUnread(count_metrics, path, "count-only query");
	}
	Require(SumDataBytes(count_metrics) == 0 && SumDataRequests(count_metrics) == 0 &&
	            SumDecodedChunks(count_metrics) == 0,
	        "count-only query must perform zero index/data reads and decoder calls");
	Require(count_metrics.At("bytes_fetched").AsUInt("bytes_fetched") ==
	            count_metrics.At("metadata_bytes").AsUInt("metadata_bytes") &&
	            count_metrics.At("read_requests").AsUInt("read_requests") ==
	                count_metrics.At("metadata_requests").AsUInt("metadata_requests"),
	        "count-only query may fetch only shared metadata");
}

std::size_t CountOpenFileDescriptors() {
	DIR *directory = opendir("/proc/self/fd");
	Require(directory != nullptr, "cannot inspect process file descriptors");
	std::size_t count = 0;
	while (const auto *entry = readdir(directory)) {
		if (entry->d_name[0] != '.') {
			count++;
		}
	}
	closedir(directory);
	return count;
}

void TestFailureMetricsAndRecovery(duckdb::Connection &connection, MetricsOutput &metrics,
	                               const TemporaryCorruptFixture &corrupt) {
	const auto valid_sql = "SELECT value FROM read_om(" + SqlLiteral(RAW_FIXTURE) + ")";
	const auto error_sql = "SELECT value FROM read_om(" + SqlLiteral(corrupt.Path()) + ")";
	auto valid = RunSuccessWithMetrics(connection, metrics, "raw", RAW_SHA256, "native_lifecycle_valid_warmup",
	                                   valid_sql, "valid lifecycle warmup");
	Require(EligibleForPerformance(valid), "valid lifecycle scan must be eligible for performance comparisons");
	metrics.SetFixture("raw", RAW_SHA256);
	metrics.SetScenario("native_lifecycle_error_warmup");
	metrics.Clear();
	Require(!RequireFailure(connection, error_sql, "corrupt-payload lifecycle warmup").empty(),
	        "corrupt-payload warmup must report a decoder error");
	auto failed = metrics.Read();
	Require(failed.At("status").AsString("failed status") == "failure",
	        "a decoder error must write a failed query-scoped metric record");
	Require(!failed.At("decode_count_complete").AsBool("failed decode_count_complete"),
	        "a failed decoder call must mark query decode counts incomplete");
	Require(!EligibleForPerformance(failed), "failed or incomplete scans must be excluded from performance comparisons");

	const auto baseline = CountOpenFileDescriptors();
	std::size_t maximum = baseline;
	std::map<std::string, bool> observed_query_ids;
	for (std::size_t iteration = 0; iteration < 100; iteration++) {
		metrics.SetFixture("raw", RAW_SHA256);
		metrics.SetScenario("native_lifecycle_valid_" + std::to_string(iteration));
		metrics.Clear();
		auto result = RequireSuccess(connection, valid_sql, "valid lifecycle iteration " + std::to_string(iteration));
		Require(result->RowCount() == 6, "valid lifecycle scan returned the wrong row count");
		result.reset();
		auto valid_metrics = metrics.Read();
		Require(valid_metrics.At("status").AsString("valid status") == "success" &&
		            EligibleForPerformance(valid_metrics),
		        "valid lifecycle iteration must have complete success metrics");
		const auto query_id = valid_metrics.At("query_id").AsString("valid query_id");
		Require(observed_query_ids.emplace(query_id, true).second,
		        "each valid lifecycle query must have a distinct query-scoped metric record");

		metrics.SetScenario("native_lifecycle_error_" + std::to_string(iteration));
		metrics.Clear();
		Require(!RequireFailure(connection, error_sql, "error lifecycle iteration " + std::to_string(iteration)).empty(),
		        "corrupt-payload lifecycle iteration must report an error");
		auto error_metrics = metrics.Read();
		Require(error_metrics.At("status").AsString("error status") == "failure" &&
		            !error_metrics.At("decode_count_complete").AsBool("error decode_count_complete") &&
		            !EligibleForPerformance(error_metrics),
		        "failed lifecycle iterations must remain incomplete and excluded from comparisons");
		const auto current = CountOpenFileDescriptors();
		maximum = std::max(maximum, current);
	}
	Require(maximum <= baseline, "100 valid/error metric iterations leaked file descriptors");

	metrics.SetFixture("raw", RAW_SHA256);
	metrics.SetScenario("native_lifecycle_recovery");
	metrics.Clear();
	auto recovered = RequireSuccess(connection, valid_sql, "valid scan after repeated decoder errors");
	Require(recovered->RowCount() == 6, "recovery scan returned the wrong row count");
	recovered.reset();
	const auto recovered_metrics = metrics.Read();
	Require(recovered_metrics.At("status").AsString("recovery status") == "success" &&
	            EligibleForPerformance(recovered_metrics),
	        "a valid scan after errors must recover with complete success metrics");
}

std::string CancellationSql() {
	std::string expression = "\"/temperature\"";
	for (std::size_t index = 0; index < 128; index++) {
		expression = "sin(" + expression + ")";
	}
	return "SELECT " + expression + " FROM " + ReadProjection();
}

void TestCancellationMetricsAndRecovery(duckdb::Connection &connection, MetricsOutput &metrics) {
	bool query_cancelled = false;
	std::string final_error;
	JsonValue cancelled_metrics;
	for (std::size_t attempt = 0; attempt < 3 && !query_cancelled; attempt++) {
		metrics.SetFixture("projection", PROJECTION_SHA256);
		metrics.SetScenario("native_cancel_attempt_" + std::to_string(attempt));
		metrics.Clear();
		std::atomic<bool> first_batch{false};
		std::atomic<bool> continue_fetch{false};
		std::atomic<bool> finished{false};
		std::string query_error;
		std::thread query_thread([&] {
			try {
				auto result = connection.SendQuery(CancellationSql());
				if (!result) {
					query_error = "cancel query returned no result";
				} else if (result->HasError()) {
					query_error = result->GetError();
				} else {
					auto first_chunk = result->Fetch();
					first_batch.store(first_chunk != nullptr, std::memory_order_release);
					while (!continue_fetch.load(std::memory_order_acquire)) {
						std::this_thread::yield();
					}
					while (result->Fetch()) {
					}
					if (result->HasError()) {
						query_error = result->GetError();
					}
				}
			} catch (const std::exception &exception) {
				query_error = exception.what();
			}
			finished.store(true, std::memory_order_release);
		});
		while (!first_batch.load(std::memory_order_acquire) && !finished.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		if (first_batch.load(std::memory_order_acquire)) {
			connection.Interrupt();
		}
		continue_fetch.store(true, std::memory_order_release);
		query_thread.join();
		final_error = query_error;
		std::transform(final_error.begin(), final_error.end(), final_error.begin(),
		               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		if (final_error.find("interrupt") == std::string::npos && final_error.find("cancel") == std::string::npos) {
			continue;
		}
		query_cancelled = true;
		cancelled_metrics = metrics.Read();
	}
	Require(query_cancelled, "long-running read_om query was not cancelled: " + final_error);

	Require(cancelled_metrics.At("status").AsString("cancelled status") == "cancelled",
	        "the cancelled SQL query must update its final metrics sidecar to cancelled status");
	Require(!EligibleForPerformance(cancelled_metrics),
	        "cancelled query metrics must be excluded from performance comparisons");

	metrics.SetFixture("projection", PROJECTION_SHA256);
	metrics.SetScenario("native_cancel_recovery");
	metrics.Clear();
	auto recovered = RequireSuccess(connection,
	                               "SELECT \"/temperature\" FROM " + ReadProjection(),
	                               "valid projection after cancellation");
	Require(recovered->RowCount() == 10541, "post-cancellation recovery query returned the wrong row count");
	recovered.reset();
	const auto recovery_metrics = metrics.Read();
	const auto recovery_status = recovery_metrics.At("status").AsString("cancel recovery status");
	const auto recovery_complete = recovery_metrics.At("decode_count_complete").AsBool("cancel recovery completeness");
	Require(recovery_status == "success" && EligibleForPerformance(recovery_metrics),
	        "valid query after cancellation must have complete success metrics (status=" + recovery_status +
	            ", decode_count_complete=" + (recovery_complete ? "true" : "false") + ")");
}

void LoadExtension(duckdb::Connection &connection) {
	fs::path core_functions_path;
	const fs::path repository("build/release/repository/v1.5.4");
	if (fs::exists(repository)) {
		for (const auto &entry : fs::recursive_directory_iterator(repository)) {
			if (entry.is_regular_file() && entry.path().filename() == "core_functions.duckdb_extension") {
				core_functions_path = entry.path();
				break;
			}
		}
	}
	Require(!core_functions_path.empty(), "cannot find the built core_functions extension in build/release/repository");
	auto core_functions = connection.Query("LOAD " + SqlLiteral(core_functions_path.string()));
	Require(core_functions != nullptr && !core_functions->HasError(),
	        "cannot load core_functions" +
	            (core_functions && core_functions->HasError() ? ": " + core_functions->GetError() : ""));
	auto result = connection.Query("LOAD './build/release/extension/duckomo/duckomo.duckdb_extension'");
	Require(result != nullptr && !result->HasError(),
	        "cannot load release extension" + (result && result->HasError() ? ": " + result->GetError() : ""));
}

} // namespace

int main() {
	try {
		duckdb::DBConfig config;
		config.SetOptionByName("allow_unsigned_extensions", true);
		config.options.maximum_threads = 1;
		duckdb::DuckDB database(nullptr, &config);
		duckdb::Connection connection(database);
		LoadExtension(connection);
		MetricsOutput metrics;
		TemporaryCorruptFixture corrupt(RAW_FIXTURE);

		TestProjectionMetrics(connection, metrics);
		TestFailureMetricsAndRecovery(connection, metrics, corrupt);
		TestCancellationMetricsAndRecovery(connection, metrics);
		std::cout << "projection metrics and lifecycle checks passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "projection metrics and lifecycle checks failed: " << exception.what() << '\n';
		return 1;
	}
}
