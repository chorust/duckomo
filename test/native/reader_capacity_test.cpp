#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"
#include "duckomo/reader.hpp"

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
	explicit MemoryReadAtFile(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
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
		if (phase == ScanReadPhase::Data) data_reads_++;
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

private:
	std::vector<std::uint8_t> bytes_;
	std::string path_ = "memory://reader-capacity-test";
	std::shared_ptr<ScanMetrics> metrics_;
	mutable std::uint64_t index_reads_ = 0;
	mutable std::uint64_t data_reads_ = 0;
	std::function<void()> before_index_read_;
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
	file_observer->SetBeforeIndexRead([&] {
		bounds_ready_before_index_read = state.capacity_bounds.data_bound_uses_object_size &&
		                                 state.capacity_bounds.index_buffer_upper_bound_bytes ==
		                                     std::min<std::uint64_t>(object.size(), metadata->lut_size);
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
	Require(bounds.data_buffer_upper_bound_bytes == object.size() && bounds.data_bound_uses_object_size,
	        "unknown compressed block Cmax must use the complete object size as its conservative bound");
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
	Require(bounds.data_buffer_upper_bound_bytes > 512,
	        "io_size_max remains a planner target, not a hard data-buffer memory cap");
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
		CheckBufferReplacementReleasesBeforeAllocation();
		std::cout << "reader capacity bounds passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "reader capacity bounds failed: " << exception.what() << '\n';
		return 1;
	}
}
