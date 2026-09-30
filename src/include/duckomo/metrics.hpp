#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb {
namespace duckomo {

// Only successful positional reads contribute bytes and requests. Metadata is
// shared by the bound schema; index/data reads and successful decoded ranges
// are attributed to their canonical OM variable path.
enum class ScanReadPhase : std::uint8_t { Metadata, Index, Data };

enum class ScanStatus : std::uint8_t { Unknown, Succeeded, Failed, Cancelled };
enum class ScanMetadataStage : std::uint8_t { Bind, Scan };

inline const char *ScanStatusName(ScanStatus status) noexcept {
	switch (status) {
	case ScanStatus::Succeeded:
		return "success";
	case ScanStatus::Failed:
		return "failure";
	case ScanStatus::Cancelled:
		return "cancelled";
	case ScanStatus::Unknown:
	default:
		return "unknown";
	}
}

struct ScanVariableMetrics final {
	std::uint64_t index_bytes = 0;
	std::uint64_t index_requests = 0;
	std::uint64_t data_bytes = 0;
	std::uint64_t data_requests = 0;
	std::uint64_t decoded_chunks = 0;
	bool decode_count_complete = true;
};

// A stable, transport-neutral copy of one query's evidence. The harness can
// consume this snapshot directly or use ToEvidenceJson() below.
struct ScanMetricsSnapshot final {
	std::uint32_t schema_version = 2;
	std::string query_id;
	std::string scenario;
	std::string sql;
	std::string fixture_id;
	std::string fixture_sha256;
	std::map<std::string, std::string> dependency_commits;
	ScanStatus status = ScanStatus::Unknown;
	std::optional<std::uint64_t> result_rows;
	std::optional<bool> comparison_passed;
	std::uint64_t metadata_bytes = 0;
	std::uint64_t metadata_requests = 0;
	std::uint64_t bind_metadata_bytes = 0;
	std::uint64_t bind_metadata_requests = 0;
	std::uint64_t scan_metadata_bytes = 0;
	std::uint64_t scan_metadata_requests = 0;
	std::string grid_definition;
	std::string spatial_layout;
	std::string grid_source;
	std::string selection_mode;
	bool residual_filter_retained = true;
	bool filter_callback_invoked = false;
	std::vector<std::string> fallback_reasons;
	std::uint64_t candidate_rows = 0;
	bool optimizer_empty = false;
	std::string reference_identity;
	double coordinate_tolerance = 1e-9;
	std::optional<bool> logical_positions_match;
	std::optional<bool> null_positions_match;
	std::map<std::string, ScanVariableMetrics> variables;
	std::uint64_t bytes_fetched = 0;
	std::uint64_t read_requests = 0;
	bool decode_count_complete = true;
	std::optional<double> elapsed_ms;
	std::map<std::string, std::string> environment;
	std::map<std::string, std::string> cache_policy;
	std::optional<std::uint64_t> peak_rss_bytes;
	std::string error_category;
};

// Per-query counters with no global registry or persistence. Instances may be
// shared by bind/scan state; updates and snapshots are safe across scan worker
// threads. RecordSuccessfulRead must be called only after a positional read
// returns successfully. For a failed decoder call, call
// MarkDecodeCountIncomplete because OM does not report how many subranges it
// may have processed before returning the error.
class ScanMetrics final {
public:
	static constexpr std::uint32_t SCHEMA_VERSION = 2;

	void SetQueryIdentity(std::string query_id, std::string scenario, std::string sql) {
		std::lock_guard<std::mutex> guard(mutex_);
		query_id_ = std::move(query_id);
		scenario_ = std::move(scenario);
		sql_ = std::move(sql);
	}

	void SetFixture(std::string fixture_id, std::string sha256) {
		std::lock_guard<std::mutex> guard(mutex_);
		fixture_id_ = std::move(fixture_id);
		fixture_sha256_ = std::move(sha256);
	}

	void SetDependencyCommit(std::string dependency, std::string commit) {
		std::lock_guard<std::mutex> guard(mutex_);
		dependency_commits_[std::move(dependency)] = std::move(commit);
	}

	void DeclareVariable(const std::string &variable_path) {
		RequireVariablePath(variable_path);
		std::lock_guard<std::mutex> guard(mutex_);
		variables_.try_emplace(variable_path);
	}

	void SetStatus(ScanStatus status, std::string error_category = {}) {
		std::lock_guard<std::mutex> guard(mutex_);
		status_ = status;
		error_category_ = std::move(error_category);
	}

	void SetResult(std::optional<std::uint64_t> result_rows, std::optional<bool> comparison_passed) {
		std::lock_guard<std::mutex> guard(mutex_);
		result_rows_ = result_rows;
		comparison_passed_ = comparison_passed;
	}

	void SetElapsedMilliseconds(double elapsed_ms) {
		if (!std::isfinite(elapsed_ms) || elapsed_ms < 0) {
			throw std::invalid_argument("elapsed_ms must be a finite non-negative value");
		}
		std::lock_guard<std::mutex> guard(mutex_);
		elapsed_ms_ = elapsed_ms;
	}

	void SetPeakRssBytes(std::uint64_t peak_rss_bytes) {
		std::lock_guard<std::mutex> guard(mutex_);
		peak_rss_bytes_ = peak_rss_bytes;
	}

	void SetEnvironmentValue(std::string key, std::string value) {
		std::lock_guard<std::mutex> guard(mutex_);
		environment_[std::move(key)] = std::move(value);
	}

	void SetCachePolicyValue(std::string key, std::string value) {
		std::lock_guard<std::mutex> guard(mutex_);
		cache_policy_[std::move(key)] = std::move(value);
	}

	void RecordMetadataRead(ScanMetadataStage stage, std::uint64_t returned_bytes) {
		std::lock_guard<std::mutex> guard(mutex_);
		auto &bytes = stage == ScanMetadataStage::Bind ? bind_metadata_bytes_ : scan_metadata_bytes_;
		auto &requests = stage == ScanMetadataStage::Bind ? bind_metadata_requests_ : scan_metadata_requests_;
		AddChecked(bytes, returned_bytes);
		AddChecked(requests, 1);
	}

	void SetSpatialContext(std::string grid_definition, std::string spatial_layout, std::string grid_source) {
		std::lock_guard<std::mutex> guard(mutex_);
		grid_definition_ = std::move(grid_definition);
		spatial_layout_ = std::move(spatial_layout);
		grid_source_ = std::move(grid_source);
	}

	void MarkFilterCallbackInvoked() {
		std::lock_guard<std::mutex> guard(mutex_);
		filter_callback_invoked_ = true;
	}

	void SetSpatialSelection(std::string selection_mode, bool residual_filter_retained,
	                         std::vector<std::string> fallback_reasons, std::uint64_t candidate_rows,
	                         bool optimizer_empty = false) {
		std::lock_guard<std::mutex> guard(mutex_);
		selection_mode_ = std::move(selection_mode);
		residual_filter_retained_ = residual_filter_retained;
		fallback_reasons_ = std::move(fallback_reasons);
		candidate_rows_ = candidate_rows;
		optimizer_empty_ = optimizer_empty;
	}

	void SetSpatialReference(std::string reference_identity, double coordinate_tolerance,
	                         std::optional<bool> logical_positions_match, std::optional<bool> null_positions_match) {
		if (!std::isfinite(coordinate_tolerance) || coordinate_tolerance < 0) {
			throw std::invalid_argument("coordinate_tolerance must be finite and non-negative");
		}
		std::lock_guard<std::mutex> guard(mutex_);
		reference_identity_ = std::move(reference_identity);
		coordinate_tolerance_ = coordinate_tolerance;
		logical_positions_match_ = logical_positions_match;
		null_positions_match_ = null_positions_match;
	}

	void RecordSuccessfulRead(ScanReadPhase phase, std::uint64_t returned_bytes,
	                          const std::string &variable_path = std::string()) {
		std::lock_guard<std::mutex> guard(mutex_);
		switch (phase) {
		case ScanReadPhase::Metadata:
			AddChecked(scan_metadata_bytes_, returned_bytes);
			AddChecked(scan_metadata_requests_, 1);
			break;
		case ScanReadPhase::Index: {
			RequireVariablePath(variable_path);
			auto &metrics = variables_[variable_path];
			AddChecked(metrics.index_bytes, returned_bytes);
			AddChecked(metrics.index_requests, 1);
			break;
		}
		case ScanReadPhase::Data: {
			RequireVariablePath(variable_path);
			auto &metrics = variables_[variable_path];
			AddChecked(metrics.data_bytes, returned_bytes);
			AddChecked(metrics.data_requests, 1);
			break;
		}
		}
	}

	// `successful_ranges` is the number of actual chunk ranges successfully
	// returned by one om_decoder_decode_chunks call; repeated ranges count again.
	void RecordSuccessfulDecode(const std::string &variable_path, std::uint64_t successful_ranges) {
		RequireVariablePath(variable_path);
		std::lock_guard<std::mutex> guard(mutex_);
		auto &metrics = variables_[variable_path];
		AddChecked(metrics.decoded_chunks, successful_ranges);
	}

	void MarkDecodeCountIncomplete(const std::string &variable_path) {
		RequireVariablePath(variable_path);
		std::lock_guard<std::mutex> guard(mutex_);
		variables_[variable_path].decode_count_complete = false;
	}

	ScanMetricsSnapshot Snapshot() const {
		std::lock_guard<std::mutex> guard(mutex_);
		ScanMetricsSnapshot snapshot;
		snapshot.query_id = query_id_;
		snapshot.scenario = scenario_;
		snapshot.sql = sql_;
		snapshot.fixture_id = fixture_id_;
		snapshot.fixture_sha256 = fixture_sha256_;
		snapshot.dependency_commits = dependency_commits_;
		snapshot.status = status_;
		snapshot.result_rows = result_rows_;
		snapshot.comparison_passed = comparison_passed_;
		snapshot.bind_metadata_bytes = bind_metadata_bytes_;
		snapshot.bind_metadata_requests = bind_metadata_requests_;
		snapshot.scan_metadata_bytes = scan_metadata_bytes_;
		snapshot.scan_metadata_requests = scan_metadata_requests_;
		AddChecked(snapshot.metadata_bytes, bind_metadata_bytes_);
		AddChecked(snapshot.metadata_bytes, scan_metadata_bytes_);
		AddChecked(snapshot.metadata_requests, bind_metadata_requests_);
		AddChecked(snapshot.metadata_requests, scan_metadata_requests_);
		snapshot.grid_definition = grid_definition_;
		snapshot.spatial_layout = spatial_layout_;
		snapshot.grid_source = grid_source_;
		snapshot.selection_mode = selection_mode_;
		snapshot.residual_filter_retained = residual_filter_retained_;
		snapshot.filter_callback_invoked = filter_callback_invoked_;
		snapshot.fallback_reasons = fallback_reasons_;
		snapshot.candidate_rows = candidate_rows_;
		snapshot.optimizer_empty = optimizer_empty_;
		snapshot.reference_identity = reference_identity_;
		snapshot.coordinate_tolerance = coordinate_tolerance_;
		snapshot.logical_positions_match = logical_positions_match_;
		snapshot.null_positions_match = null_positions_match_;
		snapshot.variables = variables_;
		snapshot.elapsed_ms = elapsed_ms_;
		snapshot.environment = environment_;
		snapshot.cache_policy = cache_policy_;
		snapshot.peak_rss_bytes = peak_rss_bytes_;
		snapshot.error_category = error_category_;
		snapshot.decode_count_complete = true;
		snapshot.bytes_fetched = snapshot.metadata_bytes;
		snapshot.read_requests = snapshot.metadata_requests;
		for (const auto &entry : variables_) {
			const auto &metrics = entry.second;
			AddChecked(snapshot.bytes_fetched, metrics.index_bytes);
			AddChecked(snapshot.bytes_fetched, metrics.data_bytes);
			AddChecked(snapshot.read_requests, metrics.index_requests);
			AddChecked(snapshot.read_requests, metrics.data_requests);
			snapshot.decode_count_complete = snapshot.decode_count_complete && metrics.decode_count_complete;
		}
		return snapshot;
	}

	std::string ToEvidenceJson() const {
		const auto snapshot = Snapshot();
		std::ostringstream json;
		json << "{\"schema_version\":" << snapshot.schema_version
		     << ",\"query_id\":" << JsonString(snapshot.query_id)
		     << ",\"scenario\":" << JsonString(snapshot.scenario)
		     << ",\"sql\":" << JsonString(snapshot.sql)
		     << ",\"fixture_id\":" << JsonString(snapshot.fixture_id)
		     << ",\"fixture_sha256\":" << JsonString(snapshot.fixture_sha256)
		     << ",\"dependency_commits\":" << JsonStringMap(snapshot.dependency_commits)
		     << ",\"status\":" << JsonString(ScanStatusName(snapshot.status))
	     << ",\"result_rows\":" << JsonOptionalUint(snapshot.result_rows)
		     << ",\"comparison_passed\":" << JsonOptionalBool(snapshot.comparison_passed)
		     << ",\"metadata_bytes\":" << snapshot.metadata_bytes
		     << ",\"metadata_requests\":" << snapshot.metadata_requests
		     << ",\"bind_metadata_bytes\":" << snapshot.bind_metadata_bytes
		     << ",\"bind_metadata_requests\":" << snapshot.bind_metadata_requests
		     << ",\"scan_metadata_bytes\":" << snapshot.scan_metadata_bytes
		     << ",\"scan_metadata_requests\":" << snapshot.scan_metadata_requests
		     << ",\"grid_definition\":" << JsonString(snapshot.grid_definition)
		     << ",\"spatial_layout\":" << JsonString(snapshot.spatial_layout)
		     << ",\"grid_source\":" << JsonString(snapshot.grid_source)
		     << ",\"selection_mode\":" << JsonString(snapshot.selection_mode)
		     << ",\"residual_filter_retained\":" << JsonBool(snapshot.residual_filter_retained)
		     << ",\"filter_callback_invoked\":" << JsonBool(snapshot.filter_callback_invoked)
		     << ",\"fallback_reasons\":" << JsonStringArray(snapshot.fallback_reasons)
		     << ",\"candidate_rows\":" << snapshot.candidate_rows
		     << ",\"optimizer_empty\":" << JsonBool(snapshot.optimizer_empty)
		     << ",\"reference_identity\":" << JsonString(snapshot.reference_identity)
		     << ",\"coordinate_tolerance\":" << std::setprecision(17) << snapshot.coordinate_tolerance
		     << ",\"logical_positions_match\":" << JsonOptionalBool(snapshot.logical_positions_match)
		     << ",\"null_positions_match\":" << JsonOptionalBool(snapshot.null_positions_match)
		     << ",\"variables\":{";
		bool first = true;
		for (const auto &entry : snapshot.variables) {
			if (!first) {
				json << ',';
			}
			first = false;
			const auto &metrics = entry.second;
			json << JsonString(entry.first) << ":{\"index_bytes\":" << metrics.index_bytes
			     << ",\"index_requests\":" << metrics.index_requests
			     << ",\"data_bytes\":" << metrics.data_bytes
			     << ",\"data_requests\":" << metrics.data_requests
			     << ",\"decoded_chunks\":" << metrics.decoded_chunks
			     << ",\"decode_count_complete\":" << JsonBool(metrics.decode_count_complete) << '}';
		}
		json << "},\"bytes_fetched\":" << snapshot.bytes_fetched
		     << ",\"read_requests\":" << snapshot.read_requests
		     << ",\"decode_count_complete\":" << JsonBool(snapshot.decode_count_complete)
		     << ",\"elapsed_ms\":" << JsonOptionalDouble(snapshot.elapsed_ms)
		     << ",\"environment\":" << JsonStringMap(snapshot.environment)
		     << ",\"cache_policy\":" << JsonStringMap(snapshot.cache_policy)
		     << ",\"peak_rss_bytes\":" << JsonOptionalUint(snapshot.peak_rss_bytes)
		     << ",\"error_category\":" << JsonString(snapshot.error_category) << '}';
		return json.str();
	}

private:
	static void AddChecked(std::uint64_t &target, std::uint64_t increment) {
		if (target > UINT64_MAX - increment) {
			throw std::overflow_error("scan metrics counter overflow");
		}
		target += increment;
	}

	static void RequireVariablePath(const std::string &variable_path) {
		if (variable_path.empty()) {
			throw std::invalid_argument("index, data, and decode metrics require a variable path");
		}
	}

	static std::string JsonBool(bool value) {
		return value ? "true" : "false";
	}

	static std::string JsonString(const std::string &value) {
		static constexpr char HEX[] = "0123456789abcdef";
		std::string escaped;
		escaped.reserve(value.size() + 2);
		escaped.push_back('"');
		for (const auto raw_character : value) {
			const auto character = static_cast<unsigned char>(raw_character);
			switch (character) {
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\b':
				escaped += "\\b";
				break;
			case '\f':
				escaped += "\\f";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				if (character < 0x20) {
					escaped += "\\u00";
					escaped.push_back(HEX[character >> 4]);
					escaped.push_back(HEX[character & 0x0f]);
				} else {
					escaped.push_back(static_cast<char>(character));
				}
			}
		}
		escaped.push_back('"');
		return escaped;
	}

	static std::string JsonStringArray(const std::vector<std::string> &values) {
		std::ostringstream json;
		json << '[';
		for (std::size_t index = 0; index < values.size(); index++) {
			if (index != 0) json << ',';
			json << JsonString(values[index]);
		}
		json << ']';
		return json.str();
	}

	template <class VALUE_TYPE>
	static std::string JsonStringMap(const std::map<std::string, VALUE_TYPE> &values) {
		std::ostringstream json;
		json << '{';
		bool first = true;
		for (const auto &entry : values) {
			if (!first) {
				json << ',';
			}
			first = false;
			json << JsonString(entry.first) << ':' << JsonString(entry.second);
		}
		json << '}';
		return json.str();
	}

	static std::string JsonOptionalUint(const std::optional<std::uint64_t> &value) {
		return value ? std::to_string(*value) : "null";
	}

	static std::string JsonOptionalBool(const std::optional<bool> &value) {
		return value ? JsonBool(*value) : "null";
	}

	static std::string JsonOptionalDouble(const std::optional<double> &value) {
		if (!value) {
			return "null";
		}
		std::ostringstream json;
		json << std::setprecision(17) << *value;
		return json.str();
	}

	mutable std::mutex mutex_;
	std::string query_id_;
	std::string scenario_;
	std::string sql_;
	std::string fixture_id_;
	std::string fixture_sha256_;
	std::map<std::string, std::string> dependency_commits_;
	ScanStatus status_ = ScanStatus::Unknown;
	std::optional<std::uint64_t> result_rows_;
	std::optional<bool> comparison_passed_;
	std::uint64_t bind_metadata_bytes_ = 0;
	std::uint64_t bind_metadata_requests_ = 0;
	std::uint64_t scan_metadata_bytes_ = 0;
	std::uint64_t scan_metadata_requests_ = 0;
	std::string grid_definition_;
	std::string spatial_layout_;
	std::string grid_source_;
	std::string selection_mode_;
	bool residual_filter_retained_ = true;
	bool filter_callback_invoked_ = false;
	std::vector<std::string> fallback_reasons_;
	std::uint64_t candidate_rows_ = 0;
	bool optimizer_empty_ = false;
	std::string reference_identity_;
	double coordinate_tolerance_ = 1e-9;
	std::optional<bool> logical_positions_match_;
	std::optional<bool> null_positions_match_;
	std::map<std::string, ScanVariableMetrics> variables_;
	std::optional<double> elapsed_ms_;
	std::map<std::string, std::string> environment_;
	std::map<std::string, std::string> cache_policy_;
	std::optional<std::uint64_t> peak_rss_bytes_;
	std::string error_category_;
};

} // namespace duckomo
} // namespace duckdb
