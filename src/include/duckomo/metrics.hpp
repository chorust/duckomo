#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb {
namespace duckomo {

// Only successful positional reads contribute bytes and requests. Metadata is
// shared by the bound schema; index/data reads and successful decoded ranges
// are attributed to their canonical OM variable path.
enum class ScanReadPhase : std::uint8_t { Metadata, Coordinate, CoordinateIndex, CoordinateData, Index, Data };
enum class ScanDecodePurpose : std::uint8_t { Value, Coordinate };

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
	std::uint64_t logical_index_bytes = 0;
	std::uint64_t logical_index_requests = 0;
	std::uint64_t logical_data_bytes = 0;
	std::uint64_t logical_data_requests = 0;
	std::uint64_t index_bytes = 0;
	std::uint64_t index_requests = 0;
	std::uint64_t data_bytes = 0;
	std::uint64_t data_requests = 0;
	std::uint64_t decoded_chunks = 0;
	bool decode_count_complete = true;
};

struct ScanCoordinateMetrics final {
	std::uint64_t logical_index_bytes = 0;
	std::uint64_t logical_index_requests = 0;
	std::uint64_t logical_data_bytes = 0;
	std::uint64_t logical_data_requests = 0;
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
	std::uint64_t scan_id = 0;
	std::string scenario;
	std::string sql;
	std::string fixture_id;
	std::string fixture_sha256;
	std::map<std::string, std::string> dependency_commits;
	ScanStatus status = ScanStatus::Unknown;
	bool scan_complete = false;
	std::optional<std::uint64_t> result_rows;
	std::optional<bool> comparison_passed;
	std::uint64_t metadata_bytes = 0;
	std::uint64_t metadata_requests = 0;
	std::uint64_t logical_requested_bytes = 0;
	std::uint64_t logical_requests = 0;
	std::uint64_t physical_read_bytes = 0;
	std::uint64_t physical_read_requests = 0;
	std::uint64_t logical_bind_metadata_bytes = 0;
	std::uint64_t logical_bind_metadata_requests = 0;
	std::uint64_t logical_scan_metadata_bytes = 0;
	std::uint64_t logical_scan_metadata_requests = 0;
	std::uint64_t logical_coordinate_bytes = 0;
	std::uint64_t logical_coordinate_requests = 0;
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
	std::uint64_t scan_tasks_claimed = 0;
	std::uint64_t active_workers = 0;
	std::uint64_t scanned_rows = 0;
	std::uint64_t coordinate_bytes = 0;
	std::uint64_t coordinate_requests = 0;
	ScanCoordinateMetrics coordinate;
	std::uint64_t cache_hits = 0;
	std::uint64_t cache_misses = 0;
	std::uint64_t cache_hit_bytes = 0;
	std::optional<std::uint64_t> response_body_bytes;
	std::optional<std::uint64_t> transport_requests;
	std::optional<std::uint64_t> transport_responses;
	std::optional<bool> transport_count_complete;
	std::map<std::string, std::uint64_t> response_statuses;
	bool cache_enabled = true;
	std::uint64_t cache_capacity_bytes = 0;
	std::uint64_t cache_charged_bytes = 0;
	std::uint64_t cache_peak_charged_bytes = 0;
	std::uint64_t cache_control_bytes = 0;
	std::uint64_t cache_evictions = 0;
	std::string cache_bypass_reason;
	std::string version_strength = "unverified";
	std::uint64_t scan_tasks_created = 0;
	std::uint64_t scan_tasks_completed = 0;
	std::uint64_t scan_tasks_failed = 0;
	std::uint64_t scan_tasks_cancelled = 0;
	std::uint64_t max_active_workers = 0;
	std::optional<std::uint64_t> peak_query_owned_bytes;
	bool query_memory_count_complete = false;
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

	void SetScanId(std::uint64_t scan_id) {
		std::lock_guard<std::mutex> guard(mutex_);
		scan_id_ = scan_id;
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

	void SetScanComplete(bool complete) {
		std::lock_guard<std::mutex> guard(mutex_);
		scan_complete_ = complete;
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

	void SetPeakQueryOwnedBytes(std::optional<std::uint64_t> peak_bytes, bool complete) {
		std::lock_guard<std::mutex> guard(mutex_);
		peak_query_owned_bytes_ = peak_bytes;
		query_memory_count_complete_ = complete;
	}

	void EnableQueryMemoryAccounting() {
		std::lock_guard<std::mutex> guard(mutex_);
		query_memory_accounting_enabled_ = true;
		query_memory_count_complete_ = true;
		peak_query_owned_bytes_ = 0;
	}

	std::uint64_t RegisterMemoryAccount() {
		std::lock_guard<std::mutex> guard(mutex_);
		const auto id = next_memory_account_id_++;
		memory_accounts_.emplace(id, 0);
		return id;
	}

	void UpdateMemoryAccount(std::uint64_t id, std::uint64_t bytes) {
		std::lock_guard<std::mutex> guard(mutex_);
		auto entry = memory_accounts_.find(id);
		if (entry == memory_accounts_.end()) return;
		if (bytes >= entry->second) {
			const auto delta = bytes - entry->second;
			if (memory_current_bytes_ > UINT64_MAX - delta) {
				query_memory_count_complete_ = false;
				peak_query_owned_bytes_.reset();
				return;
			}
			memory_current_bytes_ += delta;
		} else {
			memory_current_bytes_ -= entry->second - bytes;
		}
		entry->second = bytes;
		memory_peak_bytes_ = std::max(memory_peak_bytes_, memory_current_bytes_);
		if (query_memory_accounting_enabled_ && query_memory_count_complete_) {
			peak_query_owned_bytes_ = memory_peak_bytes_;
		}
	}

	void ReleaseMemoryAccount(std::uint64_t id) noexcept {
		std::lock_guard<std::mutex> guard(mutex_);
		auto entry = memory_accounts_.find(id);
		if (entry == memory_accounts_.end()) return;
		memory_current_bytes_ -= entry->second;
		memory_accounts_.erase(entry);
	}

	void MarkQueryMemoryAccountingIncomplete() {
		std::lock_guard<std::mutex> guard(mutex_);
		query_memory_count_complete_ = false;
		peak_query_owned_bytes_.reset();
	}

	void FinalizeQueryMemoryAccounting(bool query_succeeded) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (!query_memory_accounting_enabled_) return;
		if (!query_succeeded) query_memory_count_complete_ = false;
		if (query_memory_count_complete_) {
			peak_query_owned_bytes_ = memory_peak_bytes_;
		} else {
			peak_query_owned_bytes_.reset();
		}
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
		AddChecked(physical_read_bytes_, returned_bytes);
		AddChecked(physical_read_requests_, 1);
	}

	void RecordLogicalRead(ScanReadPhase phase, std::uint64_t requested_bytes,
	                       const std::string &variable_path = std::string(),
	                       ScanMetadataStage metadata_stage = ScanMetadataStage::Scan) {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(logical_requested_bytes_, requested_bytes);
		AddChecked(logical_requests_, 1);
		if (phase == ScanReadPhase::Metadata) {
			auto &bytes = metadata_stage == ScanMetadataStage::Bind ? logical_bind_metadata_bytes_
			                                                        : logical_scan_metadata_bytes_;
			auto &requests = metadata_stage == ScanMetadataStage::Bind ? logical_bind_metadata_requests_
			                                                           : logical_scan_metadata_requests_;
			AddChecked(bytes, requested_bytes);
			AddChecked(requests, 1);
		} else if (phase == ScanReadPhase::Coordinate) {
			AddChecked(logical_coordinate_bytes_, requested_bytes);
			AddChecked(logical_coordinate_requests_, 1);
		} else if (phase == ScanReadPhase::CoordinateIndex || phase == ScanReadPhase::CoordinateData) {
			AddChecked(logical_coordinate_bytes_, requested_bytes);
			AddChecked(logical_coordinate_requests_, 1);
			if (phase == ScanReadPhase::CoordinateIndex) {
				AddChecked(coordinate_metrics_.logical_index_bytes, requested_bytes);
				AddChecked(coordinate_metrics_.logical_index_requests, 1);
			} else {
				AddChecked(coordinate_metrics_.logical_data_bytes, requested_bytes);
				AddChecked(coordinate_metrics_.logical_data_requests, 1);
			}
		} else {
			RequireVariablePath(variable_path);
			auto &metrics = variables_[variable_path];
			if (phase == ScanReadPhase::Index) {
				AddChecked(metrics.logical_index_bytes, requested_bytes);
				AddChecked(metrics.logical_index_requests, 1);
			} else {
				AddChecked(metrics.logical_data_bytes, requested_bytes);
				AddChecked(metrics.logical_data_requests, 1);
			}
		}
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

	void RecordScanTaskClaimed() {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(scan_tasks_claimed_, 1);
	}

	void RecordScanTaskCreated() {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(scan_tasks_created_, 1);
	}

	void RecordScanTaskCompleted() {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(scan_tasks_completed_, 1);
	}

	void RecordScanTaskFailed() {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(scan_tasks_failed_, 1);
	}

	void RecordScanTaskCancelled() {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(scan_tasks_cancelled_, 1);
	}

	void RecordWorkerActive(std::uint64_t worker_id) {
		std::lock_guard<std::mutex> guard(mutex_);
		active_worker_ids_.insert(worker_id);
	}

	void BeginWorkerExecution(std::uint64_t worker_id) {
		std::lock_guard<std::mutex> guard(mutex_);
		active_worker_ids_.insert(worker_id);
		active_worker_execution_ids_.insert(worker_id);
		max_active_workers_ = std::max<std::uint64_t>(max_active_workers_, active_worker_execution_ids_.size());
	}

	void EndWorkerExecution(std::uint64_t worker_id) {
		std::lock_guard<std::mutex> guard(mutex_);
		active_worker_execution_ids_.erase(worker_id);
	}

	void SetCacheState(bool enabled, std::uint64_t capacity_bytes, std::uint64_t charged_bytes,
	                    std::uint64_t peak_charged_bytes, std::uint64_t control_bytes,
	                    std::uint64_t evictions, std::string bypass_reason, std::string version_strength) {
		std::lock_guard<std::mutex> guard(mutex_);
		cache_enabled_ = enabled;
		cache_capacity_bytes_ = capacity_bytes;
		cache_charged_bytes_ = charged_bytes;
		cache_peak_charged_bytes_ = peak_charged_bytes;
		cache_control_bytes_ = control_bytes;
		cache_evictions_ = cache_eviction_baseline_set_ && evictions >= cache_eviction_baseline_
		                       ? evictions - cache_eviction_baseline_
		                       : evictions;
		cache_bypass_reason_ = std::move(bypass_reason);
		version_strength_ = std::move(version_strength);
	}

	void SetCacheEvictionBaseline(std::uint64_t evictions) {
		std::lock_guard<std::mutex> guard(mutex_);
		cache_eviction_baseline_ = evictions;
		cache_eviction_baseline_set_ = true;
		cache_evictions_ = 0;
	}

	void SetTransportAvailable(bool available) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (!available) {
			response_body_bytes_ = 0;
			transport_requests_ = 0;
			transport_responses_ = 0;
			transport_count_complete_ = true;
		} else if (!transport_count_complete_) {
			response_body_bytes_ = 0;
			transport_requests_ = 0;
			transport_responses_ = 0;
			transport_count_complete_ = true;
		}
	}

	void RecordScannedRows(std::uint64_t rows) {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(scanned_rows_, rows);
	}

	void RecordCoordinateRead(std::uint64_t returned_bytes) {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(coordinate_bytes_, returned_bytes);
		AddChecked(coordinate_requests_, 1);
		AddChecked(physical_read_bytes_, returned_bytes);
		AddChecked(physical_read_requests_, 1);
	}

	void RecordCacheLookup(bool hit, std::uint64_t bytes = 0) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (hit) {
			AddChecked(cache_hits_, 1);
			AddChecked(cache_hit_bytes_, bytes);
		} else {
			AddChecked(cache_misses_, 1);
		}
	}

	void RecordTransportAttempt(std::uint64_t request_id, std::uint64_t attempt, int status,
	                            std::uint64_t body_bytes, bool response_received, bool complete) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (!response_body_bytes_) response_body_bytes_ = 0;
		if (!transport_requests_) transport_requests_ = 0;
		if (!transport_responses_) transport_responses_ = 0;
		if (!transport_count_complete_) transport_count_complete_ = true;
		if (!transport_attempt_ids_.emplace(request_id, attempt).second) return;
		AddChecked(*response_body_bytes_, body_bytes);
		AddChecked(*transport_requests_, 1);
		if (response_received) {
			AddChecked(*transport_responses_, 1);
			AddChecked(response_statuses_[std::to_string(status)], 1);
		}
		if (!complete) transport_count_complete_ = false;
	}

	void RecordTransportResponse(std::uint64_t body_bytes) {
		const auto id = synthetic_transport_id_.fetch_add(1, std::memory_order_relaxed);
		RecordTransportAttempt(id, 1, 200, body_bytes, true, true);
	}

	void MarkTransportCountIncomplete() {
		std::lock_guard<std::mutex> guard(mutex_);
		transport_count_complete_ = false;
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
		AddChecked(physical_read_bytes_, returned_bytes);
		AddChecked(physical_read_requests_, 1);
		switch (phase) {
		case ScanReadPhase::Metadata:
			AddChecked(scan_metadata_bytes_, returned_bytes);
			AddChecked(scan_metadata_requests_, 1);
			break;
		case ScanReadPhase::Coordinate:
			AddChecked(coordinate_bytes_, returned_bytes);
			AddChecked(coordinate_requests_, 1);
			break;
		case ScanReadPhase::CoordinateIndex:
			RecordCoordinateRangeLocked(coordinate_metrics_.index_bytes, coordinate_metrics_.index_requests,
			                            returned_bytes);
			break;
		case ScanReadPhase::CoordinateData:
			RecordCoordinateRangeLocked(coordinate_metrics_.data_bytes, coordinate_metrics_.data_requests,
			                            returned_bytes);
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

	void RecordSuccessfulCoordinateDecode(std::uint64_t successful_ranges) {
		std::lock_guard<std::mutex> guard(mutex_);
		AddChecked(coordinate_metrics_.decoded_chunks, successful_ranges);
	}

	void MarkCoordinateDecodeCountIncomplete() {
		std::lock_guard<std::mutex> guard(mutex_);
		coordinate_metrics_.decode_count_complete = false;
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
		snapshot.scan_id = scan_id_;
		snapshot.scenario = scenario_;
		snapshot.sql = sql_;
		snapshot.fixture_id = fixture_id_;
		snapshot.fixture_sha256 = fixture_sha256_;
		snapshot.dependency_commits = dependency_commits_;
		snapshot.status = status_;
		snapshot.scan_complete = scan_complete_;
		snapshot.result_rows = result_rows_;
		snapshot.comparison_passed = comparison_passed_;
		snapshot.logical_requested_bytes = logical_requested_bytes_;
		snapshot.logical_requests = logical_requests_;
		snapshot.physical_read_bytes = physical_read_bytes_;
		snapshot.physical_read_requests = physical_read_requests_;
		snapshot.logical_bind_metadata_bytes = logical_bind_metadata_bytes_;
		snapshot.logical_bind_metadata_requests = logical_bind_metadata_requests_;
		snapshot.logical_scan_metadata_bytes = logical_scan_metadata_bytes_;
		snapshot.logical_scan_metadata_requests = logical_scan_metadata_requests_;
		snapshot.logical_coordinate_bytes = logical_coordinate_bytes_;
		snapshot.logical_coordinate_requests = logical_coordinate_requests_;
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
		snapshot.scan_tasks_claimed = scan_tasks_claimed_;
		snapshot.active_workers = active_worker_ids_.size();
		snapshot.scanned_rows = scanned_rows_;
		snapshot.coordinate_bytes = coordinate_bytes_;
		snapshot.coordinate_requests = coordinate_requests_;
		snapshot.coordinate = coordinate_metrics_;
		snapshot.cache_hits = cache_hits_;
		snapshot.cache_misses = cache_misses_;
		snapshot.cache_hit_bytes = cache_hit_bytes_;
		snapshot.response_body_bytes = response_body_bytes_;
		snapshot.transport_requests = transport_requests_;
		snapshot.transport_responses = transport_responses_;
		snapshot.transport_count_complete = transport_count_complete_;
		snapshot.response_statuses = response_statuses_;
		snapshot.cache_enabled = cache_enabled_;
		snapshot.cache_capacity_bytes = cache_capacity_bytes_;
		snapshot.cache_charged_bytes = cache_charged_bytes_;
		snapshot.cache_peak_charged_bytes = cache_peak_charged_bytes_;
		snapshot.cache_control_bytes = cache_control_bytes_;
		snapshot.cache_evictions = cache_evictions_;
		snapshot.cache_bypass_reason = cache_bypass_reason_;
		snapshot.version_strength = version_strength_;
		snapshot.scan_tasks_created = scan_tasks_created_;
		snapshot.scan_tasks_completed = scan_tasks_completed_;
		snapshot.scan_tasks_failed = scan_tasks_failed_;
		snapshot.scan_tasks_cancelled = scan_tasks_cancelled_;
	snapshot.max_active_workers = max_active_workers_;
	snapshot.peak_query_owned_bytes = peak_query_owned_bytes_;
	snapshot.query_memory_count_complete = query_memory_count_complete_;
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
		snapshot.decode_count_complete = coordinate_metrics_.decode_count_complete;
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
		auto normalized_sql = snapshot.sql;
		std::transform(normalized_sql.begin(), normalized_sql.end(), normalized_sql.begin(),
		               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		const bool contains_remote_uri = normalized_sql.find("http://") != std::string::npos ||
		                                normalized_sql.find("https://") != std::string::npos ||
		                                normalized_sql.find("s3://") != std::string::npos;
		std::ostringstream json;
		json << "{\"schema_version\":" << snapshot.schema_version
		     << ",\"query_id\":" << JsonString(snapshot.query_id)
		     << ",\"scenario\":" << JsonString(snapshot.scenario)
		     << ",\"sql\":" << JsonString(contains_remote_uri ? "<redacted>" : snapshot.sql)
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
		     << ",\"scan_tasks_claimed\":" << snapshot.scan_tasks_claimed
		     << ",\"active_workers\":" << snapshot.active_workers
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

	// v3 is the stable SQL-facing profile. The prior evidence shape remains
	// available to the existing release harness through ToEvidenceJson().
	std::string ToMetricsV3Json() const {
		const auto snapshot = Snapshot();
		auto legacy = ToEvidenceJson();
		const auto sql_key = legacy.find("\"sql\":");
		if (sql_key != std::string::npos) {
			const auto value_start = legacy.find('"', sql_key + 6);
			if (value_start != std::string::npos) {
				bool escaped = false;
				std::size_t value_end = value_start + 1;
				for (; value_end < legacy.size(); value_end++) {
					const auto ch = legacy[value_end];
					if (escaped) {
						escaped = false;
					} else if (ch == '\\') {
						escaped = true;
					} else if (ch == '"') {
						break;
					}
				}
				if (value_end < legacy.size()) legacy.replace(value_start, value_end - value_start + 1, "\"<redacted>\"");
			}
		}
		std::ostringstream json;
		json << "{\"schema_version\":3,\"scan_id\":" << snapshot.scan_id
		     << ",\"query_id\":" << JsonString(snapshot.query_id)
		     << ",\"status\":" << JsonString(ScanStatusName(snapshot.status))
		     << ",\"scan_complete\":" << JsonBool(snapshot.scan_complete)
		     << ",\"axes\":{\"selection_mode\":" << JsonString(snapshot.selection_mode)
		     << ",\"fallback_reasons\":" << JsonStringArray(snapshot.fallback_reasons)
		     << ",\"optimizer_empty\":" << JsonBool(snapshot.optimizer_empty) << '}'
		     << ",\"metadata\":{\"bind\":{\"logical_bytes\":" << snapshot.logical_bind_metadata_bytes
		     << ",\"logical_requests\":" << snapshot.logical_bind_metadata_requests
		     << ",\"physical_bytes\":" << snapshot.bind_metadata_bytes
		     << ",\"physical_requests\":" << snapshot.bind_metadata_requests
		     << "},\"scan\":{\"logical_bytes\":" << snapshot.logical_scan_metadata_bytes
		     << ",\"logical_requests\":" << snapshot.logical_scan_metadata_requests
		     << ",\"physical_bytes\":" << snapshot.scan_metadata_bytes
		     << ",\"physical_requests\":" << snapshot.scan_metadata_requests << "}}"
		     << ",\"coordinate\":{\"logical_bytes\":" << snapshot.logical_coordinate_bytes
		     << ",\"logical_requests\":" << snapshot.logical_coordinate_requests
		     << ",\"physical_bytes\":" << snapshot.coordinate_bytes
		     << ",\"physical_requests\":" << snapshot.coordinate_requests
		     << ",\"logical_index_bytes\":" << snapshot.coordinate.logical_index_bytes
		     << ",\"logical_index_requests\":" << snapshot.coordinate.logical_index_requests
		     << ",\"logical_data_bytes\":" << snapshot.coordinate.logical_data_bytes
		     << ",\"logical_data_requests\":" << snapshot.coordinate.logical_data_requests
		     << ",\"index_bytes\":" << snapshot.coordinate.index_bytes
		     << ",\"index_requests\":" << snapshot.coordinate.index_requests
		     << ",\"data_bytes\":" << snapshot.coordinate.data_bytes
		     << ",\"data_requests\":" << snapshot.coordinate.data_requests
		     << ",\"decoded_chunks\":" << snapshot.coordinate.decoded_chunks
		     << ",\"decode_count_complete\":" << JsonBool(snapshot.coordinate.decode_count_complete) << '}'
		     << ",\"variables\":{";
		bool first_variable = true;
		for (const auto &entry : snapshot.variables) {
			if (!first_variable) json << ',';
			first_variable = false;
			const auto &metrics = entry.second;
			json << JsonString(entry.first) << ":{\"logical_index_bytes\":" << metrics.logical_index_bytes
			     << ",\"logical_index_requests\":" << metrics.logical_index_requests
			     << ",\"logical_data_bytes\":" << metrics.logical_data_bytes
			     << ",\"logical_data_requests\":" << metrics.logical_data_requests
			     << ",\"index_bytes\":" << metrics.index_bytes
			     << ",\"index_requests\":" << metrics.index_requests
			     << ",\"data_bytes\":" << metrics.data_bytes
			     << ",\"data_requests\":" << metrics.data_requests
			     << ",\"decoded_chunks\":" << metrics.decoded_chunks
			     << ",\"decode_count_complete\":" << JsonBool(metrics.decode_count_complete) << '}';
		}
		json << "},\"logical_requested_bytes\":" << snapshot.logical_requested_bytes
		     << ",\"logical_requests\":" << snapshot.logical_requests
		     << ",\"physical_read_bytes\":" << snapshot.physical_read_bytes
		     << ",\"physical_read_requests\":" << snapshot.physical_read_requests
		     << ",\"bytes_fetched\":" << snapshot.bytes_fetched
		     << ",\"read_requests\":" << snapshot.read_requests
		     << ",\"response_body_bytes\":" << JsonOptionalUint(snapshot.response_body_bytes)
		     << ",\"transport_attempts\":" << JsonOptionalUint(snapshot.transport_requests)
		     << ",\"transport_responses\":" << JsonOptionalUint(snapshot.transport_responses)
		     << ",\"transport_count_complete\":" << JsonOptionalBool(snapshot.transport_count_complete)
		     << ",\"response_statuses\":{";
		bool first_status = true;
		for (const auto &entry : snapshot.response_statuses) {
			if (!first_status) json << ',';
			first_status = false;
			json << JsonString(entry.first) << ':' << entry.second;
		}
		json << "},\"cache\":{\"enabled\":" << JsonBool(snapshot.cache_enabled)
		     << ",\"capacity_bytes\":" << snapshot.cache_capacity_bytes
		     << ",\"charged_bytes\":" << snapshot.cache_charged_bytes
		     << ",\"peak_charged_bytes\":" << snapshot.cache_peak_charged_bytes
		     << ",\"control_bytes\":" << snapshot.cache_control_bytes
		     << ",\"hits\":" << snapshot.cache_hits << ",\"misses\":" << snapshot.cache_misses
		     << ",\"hit_bytes\":" << snapshot.cache_hit_bytes
		     << ",\"evictions\":" << snapshot.cache_evictions
		     << ",\"bypass_reason\":" << JsonString(snapshot.cache_bypass_reason)
		     << ",\"version_strength\":" << JsonString(snapshot.version_strength)
		     << ",\"scope\":\"connection\"}"
		     << ",\"tasks\":{\"created\":" << snapshot.scan_tasks_created
		     << ",\"claimed\":" << snapshot.scan_tasks_claimed
		     << ",\"completed\":" << snapshot.scan_tasks_completed
		     << ",\"failed\":" << snapshot.scan_tasks_failed
		     << ",\"cancelled\":" << snapshot.scan_tasks_cancelled
		     << ",\"active_workers\":" << snapshot.active_workers
		     << ",\"max_active_workers\":" << snapshot.max_active_workers << '}'
		     << ",\"candidate_rows\":" << snapshot.candidate_rows
		     << ",\"scanner_rows\":" << snapshot.scanned_rows
		     << ",\"result_rows\":" << JsonOptionalUint(snapshot.result_rows)
		     << ",\"result_rows_state\":" << JsonString(snapshot.result_rows ? "observed" : "unobserved")
		     << ",\"decode_count_complete\":" << JsonBool(snapshot.decode_count_complete)
		     << ",\"elapsed_ms\":" << JsonOptionalDouble(snapshot.elapsed_ms)
		     << ",\"peak_query_owned_bytes\":" << JsonOptionalUint(snapshot.peak_query_owned_bytes)
		     << ",\"query_memory_count_complete\":" << JsonBool(snapshot.query_memory_count_complete)
		     << ",\"query_memory_scope\":\"duckomo_owned_buffer_decoder_selection_capacities\""
		     << ",\"peak_rss_bytes\":" << JsonOptionalUint(snapshot.peak_rss_bytes)
		     << ",\"memory_scope\":\"process\",\"legacy_v2\":" << legacy << '}';
		return json.str();
	}

private:
	void RecordCoordinateRangeLocked(std::uint64_t &bytes, std::uint64_t &requests,
	                                 std::uint64_t returned_bytes) {
		AddChecked(bytes, returned_bytes);
		AddChecked(requests, 1);
		AddChecked(coordinate_bytes_, returned_bytes);
		AddChecked(coordinate_requests_, 1);
	}

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
	std::uint64_t scan_id_ = 0;
	std::string scenario_;
	std::string sql_;
	std::string fixture_id_;
	std::string fixture_sha256_;
	std::map<std::string, std::string> dependency_commits_;
	ScanStatus status_ = ScanStatus::Unknown;
	bool scan_complete_ = false;
	std::optional<std::uint64_t> result_rows_;
	std::optional<bool> comparison_passed_;
	std::uint64_t logical_requested_bytes_ = 0;
	std::uint64_t logical_requests_ = 0;
	std::uint64_t physical_read_bytes_ = 0;
	std::uint64_t physical_read_requests_ = 0;
	std::uint64_t logical_bind_metadata_bytes_ = 0;
	std::uint64_t logical_bind_metadata_requests_ = 0;
	std::uint64_t logical_scan_metadata_bytes_ = 0;
	std::uint64_t logical_scan_metadata_requests_ = 0;
	std::uint64_t logical_coordinate_bytes_ = 0;
	std::uint64_t logical_coordinate_requests_ = 0;
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
	std::uint64_t scan_tasks_claimed_ = 0;
	std::uint64_t scan_tasks_created_ = 0;
	std::uint64_t scan_tasks_completed_ = 0;
	std::uint64_t scan_tasks_failed_ = 0;
	std::uint64_t scan_tasks_cancelled_ = 0;
	std::uint64_t scanned_rows_ = 0;
	std::uint64_t coordinate_bytes_ = 0;
	std::uint64_t coordinate_requests_ = 0;
	ScanCoordinateMetrics coordinate_metrics_;
	std::uint64_t cache_hits_ = 0;
	std::uint64_t cache_misses_ = 0;
	std::uint64_t cache_hit_bytes_ = 0;
	std::optional<std::uint64_t> response_body_bytes_;
	std::optional<std::uint64_t> transport_requests_;
	std::optional<std::uint64_t> transport_responses_;
	std::optional<bool> transport_count_complete_;
	std::map<std::string, std::uint64_t> response_statuses_;
	std::set<std::pair<std::uint64_t, std::uint64_t>> transport_attempt_ids_;
	std::atomic<std::uint64_t> synthetic_transport_id_{1};
	bool cache_enabled_ = true;
	std::uint64_t cache_capacity_bytes_ = 0;
	std::uint64_t cache_charged_bytes_ = 0;
	std::uint64_t cache_peak_charged_bytes_ = 0;
	std::uint64_t cache_control_bytes_ = 0;
	std::uint64_t cache_evictions_ = 0;
	std::uint64_t cache_eviction_baseline_ = 0;
	bool cache_eviction_baseline_set_ = false;
	std::string cache_bypass_reason_;
	std::string version_strength_ = "unverified";
	std::set<std::uint64_t> active_worker_ids_;
	std::set<std::uint64_t> active_worker_execution_ids_;
	std::uint64_t max_active_workers_ = 0;
	std::optional<std::uint64_t> peak_query_owned_bytes_;
	bool query_memory_count_complete_ = false;
	bool query_memory_accounting_enabled_ = false;
	std::uint64_t next_memory_account_id_ = 1;
	std::uint64_t memory_current_bytes_ = 0;
	std::uint64_t memory_peak_bytes_ = 0;
	std::map<std::uint64_t, std::uint64_t> memory_accounts_;
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

// Accounts the vector payload capacities owned by one bind/scan/decoder scope.
// The metrics object aggregates concurrent scopes and retains their high-water
// mark until QueryEnd publishes the final snapshot.
class ScanMemoryAccount final {
public:
	explicit ScanMemoryAccount(std::shared_ptr<ScanMetrics> metrics)
	    : metrics_(std::move(metrics)), id_(metrics_ ? metrics_->RegisterMemoryAccount() : 0) {
	}
	~ScanMemoryAccount() {
		if (metrics_) metrics_->ReleaseMemoryAccount(id_);
	}
	ScanMemoryAccount(const ScanMemoryAccount &) = delete;
	ScanMemoryAccount &operator=(const ScanMemoryAccount &) = delete;

	void Set(std::uint64_t bytes) const {
		if (metrics_) metrics_->UpdateMemoryAccount(id_, bytes);
	}
	const std::shared_ptr<ScanMetrics> &Metrics() const noexcept {
		return metrics_;
	}

private:
	std::shared_ptr<ScanMetrics> metrics_;
	std::uint64_t id_;
};

} // namespace duckomo
} // namespace duckdb
