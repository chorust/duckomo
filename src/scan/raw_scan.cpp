#include "duckomo/raw_scan.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/validity_mask.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckomo/batch.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/reader.hpp"

extern "C" {
#include "om_variable.h"
}

namespace duckdb {
namespace duckomo {

namespace {

struct RawBindData final : TableFunctionData {
	RawBindData(std::string path_p, std::vector<std::uint64_t> shape_p, std::uint64_t row_count_p)
	    : path(std::move(path_p)), shape(std::move(shape_p)), row_count(row_count_p) {
	}

	RawBindData(const RawBindData &other)
	    : TableFunctionData(other), path(other.path), shape(other.shape), row_count(other.row_count) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<RawBindData>(*this);
	}

	bool Equals(const FunctionData &other_p) const override {
		const auto &other = other_p.Cast<RawBindData>();
		return path == other.path && shape == other.shape && row_count == other.row_count;
	}

	std::string path;
	std::vector<std::uint64_t> shape;
	std::uint64_t row_count;
};

struct RootArrayDescriptor final {
	std::vector<std::uint64_t> shape;
	std::uint64_t row_count;
};

RootArrayDescriptor InspectRootArray(const OmVariable_t *variable, const std::string &path) {
	if (variable == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata, "OM root metadata is missing in local file '" + path + "'");
	}
	if (om_variable_get_children_count(variable) != 0) {
		throw ReaderError(ReaderErrorCode::UnsupportedNode,
		                  "read_om_raw only supports a root Float32 array without child nodes in '" + path + "'");
	}
	if (om_variable_get_type(variable) != DATA_TYPE_FLOAT_ARRAY) {
		throw ReaderError(ReaderErrorCode::UnsupportedDataType,
		                  "read_om_raw only supports a root Float32 array in '" + path + "'");
	}
	if (om_variable_get_compression(variable) != COMPRESSION_FPX_XOR2D) {
		throw ReaderError(ReaderErrorCode::UnsupportedCompression,
		                  "read_om_raw only supports FPX_XOR2D compression in '" + path + "'");
	}

	const auto rank = om_variable_get_dimensions_count(variable);
	if (rank == 0 || rank > OM_MAX_RANK) {
		throw ReaderError(ReaderErrorCode::InvalidShape,
		                  "read_om_raw requires a root array rank from 1 through 8 in '" + path + "'");
	}
	const auto *dimensions = om_variable_get_dimensions(variable);
	const auto *chunks = om_variable_get_chunks(variable);
	if (dimensions == nullptr || chunks == nullptr) {
		throw ReaderError(ReaderErrorCode::InvalidMetadata,
		                  "read_om_raw root array has no dimensions or chunk shape in '" + path + "'");
	}

	const auto dimension_count = static_cast<std::size_t>(rank);
	const auto row_count = CheckedShapeProduct(dimensions, dimension_count);
	(void)CheckedShapeProduct(chunks, dimension_count);
	std::vector<std::uint64_t> shape(dimensions, dimensions + dimension_count);
	for (std::size_t axis = 0; axis < dimension_count; axis++) {
		if (chunks[axis] == 0 || chunks[axis] > dimensions[axis]) {
			throw ReaderError(ReaderErrorCode::InvalidShape,
			                  "read_om_raw requires positive chunk lengths no larger than the array axes in '" + path + "'");
		}
	}
	return {std::move(shape), row_count};
}

struct RawGlobalState final : GlobalTableFunctionState {
	RawGlobalState(ClientContext &context, const RawBindData &bind_data)
	    : path(bind_data.path), shape(bind_data.shape), row_count(bind_data.row_count),
	      reader(make_uniq<OmV3Reader>(LocalFile::Open(context, bind_data.path))) {
		auto root = reader->ReadRootVariable();
		auto descriptor = InspectRootArray(root.Get(), path);
		if (descriptor.shape != shape || descriptor.row_count != row_count) {
			throw ReaderError(ReaderErrorCode::InvalidMetadata,
			                  "OM root array metadata changed between bind and scan for local file '" + path + "'");
		}
		decoder = make_uniq<OmDecoderState>(std::move(root));
	}

	std::string path;
	std::vector<std::uint64_t> shape;
	std::uint64_t row_count;
	std::uint64_t next_linear_index = 0;
	unique_ptr<OmV3Reader> reader;
	unique_ptr<OmDecoderState> decoder;
};

unique_ptr<FunctionData> BindRaw(ClientContext &context, TableFunctionBindInput &input,
	                             vector<LogicalType> &return_types, TableFunctionColumnNames &names) {
	if (input.inputs.size() != 1) {
		throw BinderException("read_om_raw expects one constant VARCHAR path");
	}
	const auto path = LocalFilePathFromValue(input.inputs[0]);
	OmV3Reader reader(LocalFile::Open(context, path));
	auto root = reader.ReadRootVariable();
	auto descriptor = InspectRootArray(root.Get(), path);

	return_types.emplace_back(LogicalType::FLOAT);
	names.emplace_back("value");
	return make_uniq<RawBindData>(path, std::move(descriptor.shape), descriptor.row_count);
}

unique_ptr<GlobalTableFunctionState> InitRaw(ClientContext &context, TableFunctionInitInput &input) {
	if (!input.bind_data) {
		throw InternalException("read_om_raw was initialized without bind data");
	}
	return make_uniq<RawGlobalState>(context, input.bind_data->Cast<RawBindData>());
}

void ScanRaw(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	if (!input.global_state) {
		throw InternalException("read_om_raw scan has no global state");
	}
	output.SetCardinality(0);
	auto &state = input.global_state->Cast<RawGlobalState>();
	if (state.next_linear_index >= state.row_count) {
		return;
	}

	const auto count = std::min<std::uint64_t>(state.row_count - state.next_linear_index,
	                                           static_cast<std::uint64_t>(STANDARD_VECTOR_SIZE));
	auto segments = BuildBatchSegments(state.shape, state.next_linear_index, count);
	auto *values = MutableVectorData<float>(output.data[0]);
	auto &validity = MutableVectorValidity(output.data[0]);
	validity.SetAllValid(static_cast<idx_t>(count));

	for (const auto &segment : segments) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		std::vector<std::uint64_t> cube_offset(segment.read_count.size(), 0);
		state.reader->DecodeSelection(*state.decoder, segment.read_offset, segment.read_count, cube_offset,
		                              segment.read_count, values + segment.batch_offset,
		                              segment.count * sizeof(float));
		for (std::uint64_t index = 0; index < segment.count; index++) {
			if (std::isnan(values[segment.batch_offset + index])) {
				validity.SetInvalid(static_cast<idx_t>(segment.batch_offset + index));
			}
		}
	}
	if (context.IsInterrupted()) {
		throw InterruptException();
	}

	state.next_linear_index += count;
	output.SetCardinality(static_cast<idx_t>(count));
}

} // namespace

TableFunction GetReadOmRawFunction() {
	TableFunction function("read_om_raw", {LogicalType::VARCHAR}, ScanRaw, BindRaw, InitRaw);
	function.projection_pushdown = false;
	function.filter_pushdown = false;
	function.filter_prune = false;
	function.order_preservation_type = OrderPreservationType::INSERTION_ORDER;
	return function;
}

} // namespace duckomo
} // namespace duckdb
