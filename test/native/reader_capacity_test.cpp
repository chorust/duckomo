#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"
#include "duckomo/reader.hpp"
#include "duckomo/batch.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "om_variable.h"
#include "om_encoder.h"
#include "om_file.h"
}

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb

namespace {

namespace fs = std::filesystem;
using duckdb::duckomo::OmDecoderState;
using duckdb::duckomo::OmV3Reader;
using duckdb::duckomo::ReadAtFile;
using duckdb::duckomo::ReaderError;
using duckdb::duckomo::ScanMetrics;
using duckdb::duckomo::ScanReadPhase;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> ReadBinaryFile(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "cannot open fixture: " + path.string());
	input.seekg(0, std::ios::end);
	const auto end = input.tellg();
	Require(end >= 0, "cannot determine fixture size: " + path.string());
	const auto size = static_cast<std::uint64_t>(end);
	Require(size <= std::numeric_limits<std::size_t>::max(), "fixture is too large: " + path.string());
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
	input.seekg(0, std::ios::beg);
	if (!bytes.empty()) {
		input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<std::size_t>(input.gcount()) == bytes.size(), "short fixture read: " + path.string());
	}
	return bytes;
}

class MemoryReadAtFile final : public ReadAtFile {
public:
	explicit MemoryReadAtFile(std::vector<std::uint8_t> bytes,
	                          std::shared_ptr<ScanMetrics> metrics = nullptr)
	    : bytes_(std::move(bytes)), metrics_(std::move(metrics)) {
	}

	std::uint64_t Size() const noexcept override {
		return bytes_.size();
	}
	const std::string &Path() const noexcept override {
		return path_;
	}
	const std::shared_ptr<ScanMetrics> &Metrics() const noexcept override {
		return metrics_;
	}

	void ReadRange(std::uint64_t offset, std::uint64_t size, void *destination, ScanReadPhase phase,
	               const std::string &) const override {
		Require(offset <= bytes_.size() && size <= bytes_.size() - offset, "read outside in-memory OM object");
		Require(destination != nullptr || size == 0, "null destination for non-empty OM read");
		if (phase == ScanReadPhase::Index) {
			index_reads_++;
			if (before_index_read_) before_index_read_();
		}
		if (phase == ScanReadPhase::Data) {
			data_reads_++;
			if (before_data_read_) before_data_read_();
		}
		if (size != 0) std::memcpy(destination, bytes_.data() + static_cast<std::size_t>(offset),
		                          static_cast<std::size_t>(size));
	}
	std::vector<std::uint8_t> ReadRange(std::uint64_t offset, std::uint64_t size, ScanReadPhase phase,
	                                    const std::string &variable_path) const override {
		Require(size <= std::numeric_limits<std::size_t>::max(), "metadata read exceeds addressable memory");
		std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
		ReadRange(offset, size, result.data(), phase, variable_path);
		return result;
	}

	std::uint64_t IndexReads() const noexcept { return index_reads_; }
	std::uint64_t DataReads() const noexcept { return data_reads_; }
	void SetBeforeIndexRead(std::function<void()> callback) {
		before_index_read_ = std::move(callback);
	}
	void SetBeforeDataRead(std::function<void()> callback) {
		before_data_read_ = std::move(callback);
	}

private:
	std::vector<std::uint8_t> bytes_;
	std::string path_ = "memory://reader-capacity-test";
	std::shared_ptr<ScanMetrics> metrics_;
	mutable std::uint64_t index_reads_ = 0;
	mutable std::uint64_t data_reads_ = 0;
	std::function<void()> before_index_read_;
	std::function<void()> before_data_read_;
};

void CheckRawLargeBounds() {
	const auto object = ReadBinaryFile("test/data/raw_large.om");
	auto file = std::make_unique<MemoryReadAtFile>(object);
	auto *file_observer = file.get();
	OmV3Reader reader(std::move(file));
	const auto borrowed = reader.ReadRootVariable();
	const auto *variable = borrowed.Get();
	const auto rank = static_cast<std::size_t>(om_variable_get_dimensions_count(variable));
	const auto *dimensions_ptr = om_variable_get_dimensions(variable);
	const auto *chunks_ptr = om_variable_get_chunks(variable);
	Require(rank == 2 && dimensions_ptr != nullptr && chunks_ptr != nullptr, "raw_large fixture rank mismatch");
	const std::vector<std::uint64_t> dimensions(dimensions_ptr, dimensions_ptr + rank);
	const std::vector<std::uint64_t> chunks(chunks_ptr, chunks_ptr + rank);
	const auto *metadata = reinterpret_cast<const OmVariableArrayV3_t *>(variable);
	Require(file_observer->IndexReads() == 0 && file_observer->DataReads() == 0,
	        "reader construction and capacity inputs must not pre-read the value LUT or data");

	OmDecoderState state(borrowed);
	bool bounds_ready_before_index_read = false;
	std::uint64_t chunk_product = 1;
	for (const auto chunk : chunks) {
		Require(chunk_product <= std::numeric_limits<std::uint64_t>::max() / chunk,
		        "fixture chunk product must not overflow");
		chunk_product *= chunk;
	}
	OmError_t compressed_type_error = ERROR_OK;
	const auto compressed_bytes_per_element =
	    om_get_bytes_per_element_compressed(om_variable_get_type(variable),
	                                        om_variable_get_compression(variable), &compressed_type_error);
	Require(compressed_type_error == ERROR_OK, "fixture compressed element width must be valid");
	const auto expected_compressed_chunk_bound = chunk_product * compressed_bytes_per_element +
	    2 * (chunk_product / 128 + (chunk_product % 128 != 0)) + 4096;
	file_observer->SetBeforeIndexRead([&] {
		bounds_ready_before_index_read = state.capacity_bounds.index_buffer_upper_bound_bytes ==
		                                     std::min<std::uint64_t>(object.size(), metadata->lut_size) &&
		                                 state.capacity_bounds.data_buffer_upper_bound_bytes ==
		                                     std::min<std::uint64_t>(object.size(),
		                                         std::max<std::uint64_t>(512, expected_compressed_chunk_bound));
	});
	const std::vector<std::uint64_t> read_offset{0, 0};
	std::vector<std::uint64_t> first_chunk_count{chunks[0], chunks[1]};
	std::vector<float> first_chunk(static_cast<std::size_t>(chunks[0] * chunks[1]));
	reader.DecodeSelection(state, read_offset, first_chunk_count, read_offset, first_chunk_count,
	                       first_chunk.data(), first_chunk.size() * sizeof(float), 512, 512);

	const auto first_data_capacity = state.data_bytes.Capacity();
	std::vector<float> output(static_cast<std::size_t>(dimensions[0] * dimensions[1]));
	reader.DecodeSelection(state, read_offset, dimensions, read_offset, dimensions,
	                       output.data(), output.size() * sizeof(float), 512, 512);

	const auto &bounds = state.capacity_bounds;
	const auto expected_output_bytes = output.size() * sizeof(float);
	const auto expected_scratch_bytes = chunks[0] * chunks[1] * sizeof(float);
	const auto expected_index_bound = std::min<std::uint64_t>(object.size(), metadata->lut_size);
	Require(bounds.index_buffer_upper_bound_bytes == expected_index_bound,
	        "index capacity bound must come from declared v3 LUT bytes and object size");
	Require(bounds_ready_before_index_read,
	        "reader capacity bounds must be ready before the first value LUT read");
	Require(bounds.maximum_compressed_chunk_bytes == expected_compressed_chunk_bound &&
	            bounds.data_buffer_upper_bound_bytes ==
	                std::min<std::uint64_t>(object.size(), std::max<std::uint64_t>(512, expected_compressed_chunk_bound)),
	        "decoder data buffer bound must cover request merging and one worst-case compressed chunk");
	Require(bounds.output_bytes == expected_output_bytes, "output capacity must match the declared decode cube");
	Require(bounds.chunk_scratch_bytes == expected_scratch_bytes,
	        "chunk scratch bound must match the checked chunk shape and element width");
	Require(bounds.decoder_peak_upper_bound_bytes == bounds.decoder_control_bytes +
	            bounds.index_buffer_upper_bound_bytes + bounds.data_buffer_upper_bound_bytes +
	            bounds.chunk_scratch_bytes,
	        "decoder peak bound must include simultaneous index, data, scratch, and control capacities");
	Require(bounds.peak_including_output_upper_bound_bytes ==
	            bounds.decoder_peak_upper_bound_bytes + bounds.output_bytes,
	        "combined peak bound must keep the output capacity explicit");
	Require(state.index_bytes.Capacity() <= bounds.index_buffer_upper_bound_bytes &&
	            state.data_bytes.Capacity() <= bounds.data_buffer_upper_bound_bytes &&
	            state.chunk_scratch.Capacity() == bounds.chunk_scratch_bytes,
	        "actual reusable reader capacities must stay within their published bounds");
	Require(bounds.decoder_control_bytes + state.index_bytes.Capacity() + state.data_bytes.Capacity() +
	            state.chunk_scratch.Capacity() <= bounds.decoder_peak_upper_bound_bytes,
	        "actual decoder-owned capacity must stay below its peak bound");
	Require(state.data_bytes.Capacity() >= first_data_capacity && file_observer->IndexReads() > 0 &&
	            file_observer->DataReads() > 0,
	        "full decode must reuse or grow the range buffers and exercise official index/data reads");
	Require(bounds.data_buffer_upper_bound_bytes <= object.size() && bounds.data_buffer_upper_bound_bytes > 512,
	        "decoder request bound is capped by object size and includes an oversized first chunk");
}

void CheckIncompressibleFpxChunk() {
	const std::uint64_t dimensions[] = {2048, 2048}, offset[] = {0, 0};
	const auto elements = dimensions[0] * dimensions[1];
	std::vector<float> values(elements);
	std::uint32_t seed = 173;
	for (auto &value : values) {
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		auto bits = seed;
		if ((bits & 0x7f800000U) == 0x7f800000U) bits ^= 0x00800000U;
		std::memcpy(&value, &bits, sizeof(value));
	}
	OmEncoder_t encoder{};
	Require(om_encoder_init(&encoder, 1, 0, COMPRESSION_FPX_XOR2D, DATA_TYPE_FLOAT_ARRAY,
	                        dimensions, dimensions, 2) == ERROR_OK, "FPX encoder initialization failed");
	std::vector<std::uint8_t> compressed(elements * 8 + 65536);
	std::vector<std::uint8_t> scratch(om_encoder_chunk_buffer_size(&encoder));
	const auto bytes = om_encoder_compress_chunk(&encoder, values.data(), dimensions, offset, dimensions,
	                                             0, 0, compressed.data(), scratch.data());
	Require(bytes > elements * sizeof(float) + 4096, "FPX regression must exceed the old fixed padding bound");
	std::vector<std::uint8_t> object(om_header_write_size());
	om_header_write(object.data());
	const std::uint64_t lut[] = {object.size(), object.size() + bytes};
	object.insert(object.end(), compressed.begin(), compressed.begin() + bytes);
	std::vector<std::uint8_t> compressed_lut(om_encoder_lut_buffer_size(lut, 2));
	const auto lut_bytes = om_encoder_compress_lut(lut, 2, compressed_lut.data(), compressed_lut.size());
	const auto lut_offset = object.size();
	object.insert(object.end(), compressed_lut.begin(), compressed_lut.begin() + lut_bytes);
	while (object.size() % 8) object.push_back(0);
	const auto root_offset = object.size();
	std::vector<std::uint8_t> root(om_variable_write_numeric_array_size(0, 0, 2));
	om_variable_write_numeric_array(root.data(), 0, 0, nullptr, nullptr, "", DATA_TYPE_FLOAT_ARRAY,
	                                COMPRESSION_FPX_XOR2D, 1, 0, 2, dimensions, dimensions, lut_bytes, lut_offset);
	object.insert(object.end(), root.begin(), root.end());
	while (object.size() % 8) object.push_back(0);
	const auto trailer_offset = object.size();
	object.resize(trailer_offset + om_trailer_size());
	om_trailer_write(object.data() + trailer_offset, root_offset, root.size());
	OmV3Reader reader(std::make_unique<MemoryReadAtFile>(std::move(object)));
	OmDecoderState state(reader.ReadRootVariable());
	float actual = 0;
	reader.DecodeSelection(state, {0, 0}, {1, 1}, {0, 0}, {1, 1}, &actual, sizeof(actual));
	Require(std::memcmp(&actual, values.data(), sizeof(actual)) == 0,
	        "large incompressible FPX chunk must decode with exact value bits");
	Require(bytes <= state.capacity_bounds.maximum_compressed_chunk_bytes,
	        "compressed chunk bound must include framing for every FPX block");
}

void CheckAdjacentSelectionsReuseRanges() {
	auto file = std::make_unique<MemoryReadAtFile>(ReadBinaryFile("test/data/raw_large.om"));
	auto *observed = file.get();
	OmV3Reader reader(std::move(file));
	OmDecoderState state(reader.ReadRootVariable());
	float first = 0, adjacent = 0, repeated = 0;
	reader.DecodeSelection(state, {0, 0}, {1, 1}, {0, 0}, {1, 1}, &first, sizeof(first));
	const auto index_reads = observed->IndexReads();
	const auto data_reads = observed->DataReads();
	reader.DecodeSelection(state, {0, 1}, {1, 1}, {0, 0}, {1, 1}, &adjacent, sizeof(adjacent));
	reader.DecodeSelection(state, {0, 0}, {1, 1}, {0, 0}, {1, 1}, &repeated, sizeof(repeated));
	Require(first == repeated && adjacent != first, "range reuse must preserve each requested output slice");
	Require(observed->IndexReads() == index_reads && observed->DataReads() == data_reads,
	        "adjacent points within one chunk must reuse index and compressed data reads");
	OmDecoderState independent(reader.ReadRootVariable());
	reader.DecodeSelection(independent, {0, 0}, {1, 1}, {0, 0}, {1, 1}, &repeated, sizeof(repeated));
	Require(observed->DataReads() == data_reads + 1,
	        "independent decoder states must not share range reuse");
	// A larger request followed by a contained subrange uses the right byte
	// offsets, rather than interpreting the retained buffer from its beginning.
	std::vector<float> full(om_variable_get_dimensions(state.variable_owner.Get())[0] *
	                        om_variable_get_dimensions(state.variable_owner.Get())[1]);
	const auto *dims = om_variable_get_dimensions(state.variable_owner.Get());
	reader.DecodeSelection(state, {0, 0}, {dims[0], dims[1]}, {0, 0}, {dims[0], dims[1]},
	                       full.data(), full.size() * sizeof(float));
	reader.DecodeSelection(state, {0, 1}, {1, 1}, {0, 0}, {1, 1}, &adjacent, sizeof(adjacent));
	Require(adjacent == full[1], "contained range reuse must preserve byte offsets");
	bool failed = false;
	observed->SetBeforeIndexRead([&] { throw std::runtime_error("injected range read failure"); });
	state.index_range_size = 0;
	try {
		reader.DecodeSelection(state, {0, 0}, {1, 1}, {0, 0}, {1, 1}, &repeated, sizeof(repeated));
	} catch (const std::runtime_error &) { failed = true; }
	Require(failed && state.index_range_size == 0, "failed index replacement must invalidate retained range");
	observed->SetBeforeIndexRead({});
	const auto failed_reads = observed->IndexReads();
	reader.DecodeSelection(state, {0, 0}, {1, 1}, {0, 0}, {1, 1}, &repeated, sizeof(repeated));
	Require(observed->IndexReads() == failed_reads + 1 && repeated == first,
	        "retry after a failed replacement must read and validate the new bytes");
	observed->SetBeforeDataRead([&] { throw std::runtime_error("injected data read failure"); });
	state.data_range_size = 0;
	failed = false;
	try {
		reader.DecodeSelection(state, {0, 0}, {1, 1}, {0, 0}, {1, 1}, &repeated, sizeof(repeated));
	} catch (const std::runtime_error &) { failed = true; }
	Require(failed && state.data_range_size == 0, "failed data replacement must invalidate retained range");
	observed->SetBeforeDataRead({});
	const auto failed_data_reads = observed->DataReads();
	reader.DecodeSelection(state, {0, 0}, {1, 1}, {0, 0}, {1, 1}, &repeated, sizeof(repeated));
	Require(observed->DataReads() == failed_data_reads + 1 && repeated == first,
	        "retry after data read failure must not reuse incomplete bytes");
}

void CheckRectangularBatchAvoidsRepeatedChunkDecoding() {
	const auto object = ReadBinaryFile("test/data/raw_large.om");
	auto metrics = std::make_shared<ScanMetrics>();
	OmV3Reader reader(std::make_unique<MemoryReadAtFile>(object, metrics));
	OmDecoderState merged(reader.ReadRootVariable()), rows(reader.ReadRootVariable());
	const std::vector<std::uint64_t> shape{73, 61};
	const auto segments = duckdb::duckomo::BuildBatchSegments(shape, 0, 7 * 61);
	Require(segments.size() == 1, "seven complete rows should use one rectangular selection");
	std::vector<float> merged_output(7 * 61), row_output(7 * 61);
	const auto &segment = segments.front();
	reader.DecodeSelection(merged, "merged", segment.read_offset, segment.read_count, {0, 0},
	                       segment.read_count, merged_output.data(), merged_output.size() * sizeof(float));
	for (std::uint64_t row = 0; row < 7; ++row) {
		reader.DecodeSelection(rows, "rows", {row, 0}, {1, 61}, {0, 0}, {1, 61},
		                       row_output.data() + row * 61, 61 * sizeof(float));
	}
	Require(std::memcmp(merged_output.data(), row_output.data(), merged_output.size() * sizeof(float)) == 0,
	        "rectangular selections must match row-at-a-time official decode bit for bit");
	const auto snapshot = metrics->Snapshot();
	const auto merged_chunks = snapshot.variables.at("merged").decoded_chunks;
	const auto row_chunks = snapshot.variables.at("rows").decoded_chunks;
	Require(merged_chunks == 5 && row_chunks == 35,
	        "joining seven rows must decode each of the five physical chunks once rather than seven times");
	Require(merged.capacity_bounds.chunk_scratch_bytes == rows.capacity_bounds.chunk_scratch_bytes &&
	            merged.capacity_bounds.output_bytes == merged_output.size() * sizeof(float),
	        "joining selected rows must preserve the reader scratch bound and exact output capacity");
	std::cout << "rectangular batch decoded chunks: " << row_chunks << " -> " << merged_chunks << '\n';
}

void CheckBufferReplacementReleasesBeforeAllocation() {
	using duckdb::duckomo::OmByteBuffer;

	OmByteBuffer buffer;
	buffer.EnsureSize(64);
	Require(buffer.Data() != nullptr && buffer.Capacity() == 64,
	        "reader buffer setup must allocate the initial range capacity");

	bool replacement_failed = false;
	try {
		buffer.EnsureSize(std::numeric_limits<std::size_t>::max());
	} catch (const std::bad_alloc &) {
		replacement_failed = true;
	}
	Require(replacement_failed, "impossible replacement size must fail allocation");
	Require(buffer.Data() == nullptr && buffer.Capacity() == 0 && buffer.Size() == 0,
	        "failed replacement must not retain the old reader buffer allocation");
}

} // namespace

int main() {
	try {
		CheckRawLargeBounds();
		CheckIncompressibleFpxChunk();
		CheckAdjacentSelectionsReuseRanges();
		CheckRectangularBatchAvoidsRepeatedChunkDecoding();
		CheckBufferReplacementReleasesBeforeAllocation();
		std::cout << "reader capacity bounds passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "reader capacity bounds failed: " << exception.what() << '\n';
		return 1;
	}
}
