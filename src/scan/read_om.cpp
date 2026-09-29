#include "duckomo/read_om.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb/common/table_column.hpp"
#include "duckdb/common/types/validity_mask.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "duckdb/planner/column_binding.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckomo/batch.hpp"
#include "duckomo/dimensions.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/metrics.hpp"
#include "duckomo/metadata.hpp"
#include "duckomo/projection.hpp"
#include "duckomo/reader.hpp"
#include "duckomo/schema.hpp"

namespace duckdb {
namespace duckomo {

namespace {

std::string EnvironmentValue(const char *name) {
	const auto *value = std::getenv(name);
	return value == nullptr ? std::string() : std::string(value);
}

void WriteMetricsSidecar(const ScanMetrics &metrics, const std::string &path) noexcept;

// A table function's global scan state can finish before DuckDB has finished
// consuming the query (for example, when a downstream operator is interrupted
// after the final source batch). Keep a query-end observer so the sidecar
// status describes the complete SQL query rather than only the scanner.
class ScanMetricsQueryState final : public ClientContextState {
public:
	ScanMetricsQueryState(std::string state_key_p, std::string output_path_p, std::shared_ptr<ScanMetrics> metrics_p)
	    : state_key(std::move(state_key_p)), output_path(std::move(output_path_p)), metrics(std::move(metrics_p)) {
	}

	void QueryEnd(ClientContext &context, optional_ptr<ErrorData> error) override {
		if (error) {
			const auto current = metrics->Snapshot().status;
			if (error->Type() == ExceptionType::INTERRUPT) {
				metrics->SetStatus(ScanStatus::Cancelled, "cancelled");
			} else if (current != ScanStatus::Failed) {
				metrics->SetStatus(ScanStatus::Failed, "query_error");
			}
		} else {
			// A successful query can stop before the scan reaches EOF (for
			// example, when LIMIT has enough rows). Query completion is the
			// authoritative success signal; the scan destructor may have marked
			// this case as incomplete_scan already.
			metrics->SetStatus(ScanStatus::Succeeded);
		}
		WriteMetricsSidecar(*metrics, output_path);
		context.registered_state->Remove(state_key);
	}

private:
	std::string state_key;
	std::string output_path;
	std::shared_ptr<ScanMetrics> metrics;
};

std::shared_ptr<ScanMetrics> CreateScanMetrics(ClientContext &context) {
	static std::atomic<std::uint64_t> next_query_id{0};
	auto metrics = std::make_shared<ScanMetrics>();
	const auto query_id = std::to_string(static_cast<unsigned long long>(context.GetConnectionId())) + "-" +
	                      std::to_string(static_cast<unsigned long long>(next_query_id.fetch_add(1)));
	metrics->SetQueryIdentity(query_id, EnvironmentValue("DUCKOMO_SCENARIO"), context.GetCurrentQuery());
	metrics->SetFixture(EnvironmentValue("DUCKOMO_FIXTURE_ID"), EnvironmentValue("DUCKOMO_FIXTURE_SHA256"));
	metrics->SetDependencyCommit("duckdb", "08e34c447bae34eaee3723cac61f2878b6bdf787");
	metrics->SetDependencyCommit("om-file-format", "d8855e418e2231ae8439f0c7e840fa3f93b371e3");
	metrics->SetDependencyCommit("extension-ci-tools", "b777c70d30942cca5bef62d6d4fa23a13362f398");
	metrics->SetEnvironmentValue("platform", "linux");
	metrics->SetEnvironmentValue("build", "release");
	metrics->SetCachePolicyValue("application_cache", "disabled");
	return metrics;
}

void RegisterScanMetricsQueryState(ClientContext &context, const std::shared_ptr<ScanMetrics> &metrics) {
	const auto output_path = EnvironmentValue("DUCKOMO_METRICS_OUTPUT");
	if (!output_path.empty()) {
		const auto state_key = "duckomo.scan_metrics." + metrics->Snapshot().query_id;
		context.registered_state->Insert(state_key,
		                                make_shared_ptr<ScanMetricsQueryState>(state_key, output_path, metrics));
	}
}

void WriteMetricsSidecar(const ScanMetrics &metrics) noexcept {
	WriteMetricsSidecar(metrics, EnvironmentValue("DUCKOMO_METRICS_OUTPUT"));
}

void WriteMetricsSidecar(const ScanMetrics &metrics, const std::string &path) noexcept {
	if (path.empty()) {
		return;
	}
	try {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (output) {
			output << metrics.ToEvidenceJson() << '\n';
		}
	} catch (...) {
		// Evidence sidecars are best-effort and must never change query results.
	}
}

virtual_column_map_t GetReadOmVirtualColumns(ClientContext &, optional_ptr<FunctionData>) {
	virtual_column_map_t result;
	result.emplace(COLUMN_IDENTIFIER_EMPTY, TableColumn("", LogicalType::BOOLEAN));
	return result;
}

bool SameSchema(const BoundSchema &left, const BoundSchema &right) {
	if (left.row_count != right.row_count || left.shape != right.shape ||
	    left.variables.size() != right.variables.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.variables.size(); index++) {
		const auto &left_variable = left.variables[index];
		const auto &right_variable = right.variables[index];
		if (left_variable.canonical_path != right_variable.canonical_path ||
		    left_variable.column_name != right_variable.column_name ||
		    left_variable.inferred_axes != right_variable.inferred_axes || left_variable.type != right_variable.type ||
		    left_variable.shape != right_variable.shape || left_variable.chunk_shape != right_variable.chunk_shape ||
		    left_variable.row_count != right_variable.row_count ||
		    left_variable.metadata_offset != right_variable.metadata_offset ||
		    left_variable.metadata_size != right_variable.metadata_size) {
			return false;
		}
	}
	return true;
}

struct ReadOmBindData final : TableFunctionData {
    ReadOmBindData(std::string path_p, BoundSchema schema_p, AxisDeclarations axes_p)
        : path(std::move(path_p)), schema(std::move(schema_p)), axes(std::move(axes_p)) {
    }

    ReadOmBindData(const ReadOmBindData &other)
        : TableFunctionData(other), path(other.path), schema(other.schema), axes(other.axes) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<ReadOmBindData>(*this);
	}

	bool Equals(const FunctionData &other_p) const override {
		const auto &other = other_p.Cast<ReadOmBindData>();
		return path == other.path && axes == other.axes && SameSchema(schema, other.schema);
	}

    std::string path;
    BoundSchema schema;
    AxisDeclarations axes;
};

struct ReadOmGlobalState final : GlobalTableFunctionState {
    ReadOmGlobalState(ClientContext &context, const ReadOmBindData &bind_data,
                      const std::vector<column_t> &column_ids, std::shared_ptr<ScanMetrics> metrics_p)
        : path(bind_data.path), schema(bind_data.schema), projection(schema, column_ids), metrics(std::move(metrics_p)),
          reader(make_uniq<OmV3Reader>(LocalFile::Open(context, bind_data.path, metrics))) {
		// The file may have changed since binding. Revalidate its complete tree
		// before constructing decoder state, while keeping binding metadata-only.
		auto current_schema = BuildBoundSchema(ReadMetadataTree(*reader));
		if (!SameSchema(schema, current_schema)) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM array metadata changed between bind and scan for local file '" + path + "'");
		}

        decoders.reserve(projection.GetRequiredVariableIds().size());
        for (const auto variable_index : projection.GetRequiredVariableIds()) {
            const auto &variable = current_schema.variables.at(variable_index);
            decoders.emplace_back(make_uniq<OmDecoderState>(BorrowedOmVariable(variable.metadata_owner)));
        }
    }

    ~ReadOmGlobalState() override {
        if (!metrics) {
            return;
        }
        if (!scan_completed && !metrics_status_set) {
            metrics->SetStatus(ScanStatus::Failed, "incomplete_scan");
        }
        WriteMetricsSidecar(*metrics);
    }

    std::string path;
    BoundSchema schema;
    ProjectionPlan projection;
    std::shared_ptr<ScanMetrics> metrics;
    std::uint64_t next_linear_index = 0;
    unique_ptr<OmV3Reader> reader;
    vector<unique_ptr<OmDecoderState>> decoders;
    bool scan_completed = false;
    bool metrics_status_set = false;
};

unique_ptr<FunctionData> BindReadOm(ClientContext &context, TableFunctionBindInput &input,
	                                vector<LogicalType> &return_types, vector<std::string> &names) {
	if (input.inputs.size() != 1) {
		throw BinderException("read_om expects one constant VARCHAR path");
	}
	const auto path = LocalFilePathFromValue(input.inputs[0]);
	OmV3Reader reader(LocalFile::Open(context, path));
	auto schema = BuildBoundSchema(ReadMetadataTree(reader));

	const auto dimensions_entry = input.named_parameters.find("dimensions");
	const auto *dimensions = dimensions_entry == input.named_parameters.end() ? nullptr : &dimensions_entry->second;
	auto axes = ValidateAxisDeclarations(dimensions, schema);

	return_types.reserve(return_types.size() + schema.variables.size());
	names.reserve(names.size() + schema.variables.size());
	for (const auto &variable : schema.variables) {
		return_types.emplace_back(variable.type);
		names.emplace_back(variable.column_name);
	}
	return make_uniq<ReadOmBindData>(path, std::move(schema), std::move(axes));
}

unique_ptr<GlobalTableFunctionState> InitReadOm(ClientContext &context, TableFunctionInitInput &input) {
	if (!input.bind_data) {
		throw InternalException("read_om was initialized without bind data");
	}
	auto metrics = CreateScanMetrics(context);
	RegisterScanMetricsQueryState(context, metrics);
	return make_uniq<ReadOmGlobalState>(context, input.bind_data->Cast<ReadOmBindData>(), input.column_ids,
	                                    std::move(metrics));
}

void ScanReadOmImpl(ClientContext &context, ReadOmGlobalState &state, DataChunk &output) {
	output.SetCardinality(0);
    if (state.next_linear_index >= state.schema.row_count) {
        state.scan_completed = true;
        if (state.metrics) {
            state.metrics->SetStatus(ScanStatus::Succeeded);
            state.metrics_status_set = true;
        }
        return;
	}
    const auto &slots = state.projection.GetOutputSlots();
    const auto &required_variables = state.projection.GetRequiredVariableIds();
    if (output.data.size() != slots.size() || state.decoders.size() != required_variables.size()) {
        throw InternalException("read_om output column count does not match its projection plan");
	}

	const auto count = std::min<std::uint64_t>(state.schema.row_count - state.next_linear_index,
	                                           static_cast<std::uint64_t>(STANDARD_VECTOR_SIZE));
	auto segments = BuildBatchSegments(state.schema.shape, state.next_linear_index, count);
    for (std::size_t output_column = 0; output_column < slots.size(); output_column++) {
        if (!slots[output_column].is_cardinality) {
            continue;
        }
        auto *values = FlatVector::GetData<bool>(output.data[output_column]);
        std::fill_n(values, static_cast<idx_t>(count), true);
        FlatVector::Validity(output.data[output_column]).SetAllValid(static_cast<idx_t>(count));
    }

    for (std::size_t required_index = 0; required_index < required_variables.size(); required_index++) {
        idx_t primary_output = DConstants::INVALID_INDEX;
        for (idx_t output_column = 0; output_column < slots.size(); output_column++) {
            if (!slots[output_column].is_cardinality &&
                slots[output_column].required_variable_index == required_index) {
                primary_output = output_column;
                break;
            }
        }
        if (primary_output == DConstants::INVALID_INDEX) {
            throw InternalException("read_om projection plan has an unreferenced required variable");
        }
        auto *values = FlatVector::GetData<float>(output.data[primary_output]);
        auto &validity = FlatVector::Validity(output.data[primary_output]);
        validity.SetAllValid(static_cast<idx_t>(count));
        for (const auto &segment : segments) {
            if (context.IsInterrupted()) {
                throw InterruptException();
            }
            std::vector<std::uint64_t> cube_offset(segment.read_count.size(), 0);
            const auto variable_index = required_variables[required_index];
            const auto &variable = state.schema.variables.at(variable_index);
            state.reader->DecodeSelection(*state.decoders[required_index], variable.canonical_path,
                                          segment.read_offset, segment.read_count,
                                          cube_offset, segment.read_count, values + segment.batch_offset,
                                          segment.count * sizeof(float));
			for (std::uint64_t index = 0; index < segment.count; index++) {
				const auto batch_index = segment.batch_offset + index;
				if (std::isnan(values[batch_index])) {
					validity.SetInvalid(static_cast<idx_t>(batch_index));
                }
            }
        }

        for (idx_t output_column = 0; output_column < slots.size(); output_column++) {
            if (output_column == primary_output || slots[output_column].is_cardinality ||
                slots[output_column].required_variable_index != required_index) {
                continue;
            }
            auto *duplicate_values = FlatVector::GetData<float>(output.data[output_column]);
            std::memcpy(duplicate_values, values, static_cast<std::size_t>(count) * sizeof(float));
            auto &duplicate_validity = FlatVector::Validity(output.data[output_column]);
            duplicate_validity.SetAllValid(static_cast<idx_t>(count));
            for (idx_t row = 0; row < count; row++) {
                if (!validity.RowIsValid(row)) {
                    duplicate_validity.SetInvalid(row);
                }
            }
        }
    }
	if (context.IsInterrupted()) {
		throw InterruptException();
	}

	// Commit both the visible cardinality and shared range only after every
	// array has decoded successfully. Exceptions leave a zero-row chunk.
	state.next_linear_index += count;
	output.SetCardinality(static_cast<idx_t>(count));
}

void ScanReadOm(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	if (!input.global_state) {
		throw InternalException("read_om scan has no global state");
	}
	auto &state = input.global_state->Cast<ReadOmGlobalState>();
	try {
		ScanReadOmImpl(context, state, output);
	} catch (const InterruptException &) {
		if (state.metrics) {
			state.metrics->SetStatus(ScanStatus::Cancelled, "cancelled");
			state.metrics_status_set = true;
		}
		throw;
	} catch (const ReaderError &error) {
		if (state.metrics) {
			if (error.Code() == ReaderErrorCode::Cancelled) {
				state.metrics->SetStatus(ScanStatus::Cancelled, "cancelled");
			} else {
				state.metrics->SetStatus(ScanStatus::Failed,
				                         "reader_error_" + std::to_string(static_cast<unsigned int>(error.Code())));
			}
			state.metrics_status_set = true;
		}
		throw;
	} catch (const std::exception &) {
		if (state.metrics) {
			state.metrics->SetStatus(ScanStatus::Failed, "scan_error");
			state.metrics_status_set = true;
		}
		throw;
	}
}

} // namespace

TableFunction GetReadOmFunction() {
	TableFunction function("read_om", {LogicalType::VARCHAR}, ScanReadOm, BindReadOm, InitReadOm);
	function.named_parameters["dimensions"] = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR));
    function.get_virtual_columns = GetReadOmVirtualColumns;
    function.projection_pushdown = true;
	function.filter_pushdown = false;
	function.filter_prune = false;
	function.order_preservation_type = OrderPreservationType::INSERTION_ORDER;
	return function;
}

} // namespace duckomo
} // namespace duckdb
