#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

extern "C" {
#include "om_file.h"
#include "om_variable.h"
}

// Native test executables link DuckDB's static library and this extension's
// static library directly. The generated extension-loader archive is not part
// of the native-test link line, so provide the narrow startup hook needed by
// DuckDB and load the extension under test itself.
namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb

namespace {

namespace fs = std::filesystem;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::vector<std::uint8_t> ReadBinaryFile(const fs::path &path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.good(), "cannot open fixture: " + path.string());
	stream.seekg(0, std::ios::end);
	const auto end = stream.tellg();
	Require(end >= 0, "cannot determine fixture size: " + path.string());
	const auto size = static_cast<std::uint64_t>(end);
	Require(size <= std::numeric_limits<std::size_t>::max(), "fixture is too large: " + path.string());
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
	stream.seekg(0, std::ios::beg);
	if (!bytes.empty()) {
		stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<std::size_t>(stream.gcount()) == bytes.size(), "short read from fixture: " + path.string());
	}
	return bytes;
}

void WriteBinaryFile(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	Require(stream.good(), "cannot create fixture mutation: " + path.string());
	if (!bytes.empty()) {
		stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	Require(stream.good(), "cannot write fixture mutation: " + path.string());
}

std::uint32_t FloatBits(float value) {
	std::uint32_t bits = 0;
	static_assert(sizeof(bits) == sizeof(value), "Float32 must be 32 bits");
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

float ParseReferenceValue(const std::string &text) {
	if (text == "nan") {
		return std::numeric_limits<float>::quiet_NaN();
	}
	if (text == "inf") {
		return std::numeric_limits<float>::infinity();
	}
	if (text == "-inf") {
		return -std::numeric_limits<float>::infinity();
	}
	if (text == "-0") {
		return -0.0F;
	}
	std::size_t parsed = 0;
	const auto value = std::stof(text, &parsed);
	Require(parsed == text.size(), "invalid Float32 reference value: " + text);
	return value;
}

std::vector<float> ReadReferenceCsv(const fs::path &path) {
	std::ifstream stream(path);
	Require(stream.good(), "cannot open oracle CSV: " + path.string());
	std::string line;
	Require(static_cast<bool>(std::getline(stream, line)) && line == "index,value",
	        "oracle CSV has an unexpected header: " + path.string());
	std::vector<float> values;
	while (std::getline(stream, line)) {
		const auto comma = line.find(',');
		Require(comma != std::string::npos && line.find(',', comma + 1) == std::string::npos,
		        "malformed oracle CSV row in " + path.string());
		const auto row_index = std::stoull(line.substr(0, comma));
		Require(row_index == values.size(), "oracle CSV row indexes are not contiguous: " + path.string());
		values.push_back(ParseReferenceValue(line.substr(comma + 1)));
	}
	Require(!values.empty(), "oracle CSV contains no values: " + path.string());
	return values;
}

std::string SqlLiteral(const fs::path &path) {
	std::string escaped;
	for (const auto character : path.string()) {
		escaped.push_back(character);
		if (character == '\'') {
			escaped.push_back('\'');
		}
	}
	return "'" + escaped + "'";
}

void CheckQueryAgainstOracle(duckdb::Connection &connection, const fs::path &om_path, const fs::path &csv_path,
                             std::size_t minimum_batches = 1) {
	const auto expected = ReadReferenceCsv(csv_path);
	auto result = connection.Query("SELECT value FROM read_om_raw(" + SqlLiteral(om_path) + ")");
	Require(!result->HasError(), "read_om_raw failed for " + om_path.string() + ": " + result->GetError());
	Require(result->ColumnCount() == 1 && result->ColumnName(0) == "value",
	        "read_om_raw must expose exactly one value column for " + om_path.string());
	Require(result->types.size() == 1 && result->types[0] == duckdb::LogicalType::FLOAT,
	        "read_om_raw value column must be FLOAT for " + om_path.string());

	std::size_t row_index = 0;
	std::size_t batch_count = 0;
	while (auto batch = result->Fetch()) {
		batch_count++;
		Require(batch->ColumnCount() == 1, "read_om_raw returned an unexpected batch schema");
		for (duckdb::idx_t row = 0; row < batch->size(); row++, row_index++) {
			Require(row_index < expected.size(), "read_om_raw returned more rows than the official oracle");
			const auto expected_value = expected[row_index];
			const auto actual = batch->GetValue(0, row);
			if (std::isnan(expected_value)) {
				Require(actual.IsNull(), "official NaN at flattened position " + std::to_string(row_index) +
				                            " must be returned as SQL NULL");
				continue;
			}
			Require(!actual.IsNull(), "unexpected SQL NULL at flattened position " + std::to_string(row_index));
			const auto actual_value = actual.GetValue<float>();
			if (std::isinf(expected_value)) {
				Require(std::isinf(actual_value) && std::signbit(actual_value) == std::signbit(expected_value),
				        "infinity sign mismatch at flattened position " + std::to_string(row_index));
			} else {
				Require(FloatBits(actual_value) == FloatBits(expected_value),
				        "Float32 bit mismatch in " + om_path.string() + " at flattened position " +
			                std::to_string(row_index) + " (expected bits " +
			                std::to_string(FloatBits(expected_value)) + ", actual bits " +
			                std::to_string(FloatBits(actual_value)) + ")");
			}
		}
	}
	Require(row_index == expected.size(), "read_om_raw returned fewer rows than the official oracle for " + om_path.string());
	Require(batch_count >= minimum_batches,
	        "read_om_raw did not cross the expected DuckDB batch boundary for " + om_path.string());
}

struct RootMetadata final {
	std::vector<std::uint8_t> file;
	std::vector<std::uint8_t> metadata;
	std::uint64_t root_offset = 0;
};

RootMetadata ReadRootMetadata(const fs::path &path) {
	RootMetadata result;
	result.file = ReadBinaryFile(path);
	const auto trailer_size = static_cast<std::uint64_t>(om_trailer_size());
	Require(result.file.size() >= trailer_size, "OM fixture has no complete trailer: " + path.string());
	const auto trailer_offset = static_cast<std::uint64_t>(result.file.size()) - trailer_size;
	std::uint64_t root_size = 0;
	Require(om_trailer_read(result.file.data() + static_cast<std::size_t>(trailer_offset), &result.root_offset,
	                        &root_size),
	        "cannot read fixture trailer: " + path.string());
	Require(result.root_offset <= trailer_offset && root_size <= trailer_offset - result.root_offset,
	        "fixture root metadata is outside its body: " + path.string());
	Require(root_size <= std::numeric_limits<std::size_t>::max(), "fixture root metadata is too large");
	const auto begin = result.file.begin() + static_cast<std::ptrdiff_t>(result.root_offset);
	result.metadata.assign(begin, begin + static_cast<std::ptrdiff_t>(root_size));
	Require(om_variable_validate(result.metadata.data(), result.metadata.size()) == ERROR_OK,
	        "source fixture root metadata is invalid: " + path.string());
	Require(om_variable_get_type(om_variable_init(result.metadata.data())) == DATA_TYPE_FLOAT_ARRAY,
	        "source fixture is not a Float32 array: " + path.string());
	return result;
}

std::vector<std::uint8_t> MetadataWithRank(const std::vector<std::uint8_t> &source, std::uint64_t rank) {
	Require(om_variable_validate(source.data(), source.size()) == ERROR_OK, "cannot mutate invalid root metadata");
	const auto *variable = om_variable_init(source.data());
	const auto source_rank = om_variable_get_dimensions_count(variable);
	const auto *source_dimensions = om_variable_get_dimensions(variable);
	const auto *source_chunks = om_variable_get_chunks(variable);
	Require(source_rank > 0 && source_rank <= 8 && source_dimensions != nullptr && source_chunks != nullptr,
	        "source fixture rank or shape is invalid");
	Require(rank <= std::numeric_limits<std::size_t>::max(), "requested fixture rank is too large");

	OmVariableArrayV3_t header{};
	std::memcpy(&header, source.data(), sizeof(header));
	header.dimension_count = rank;
	const auto dimension_offset = sizeof(header) + static_cast<std::size_t>(header.children_count) * 16;
	const auto name_offset = dimension_offset + static_cast<std::size_t>(source_rank) * 16;
	const auto new_name_offset = dimension_offset + static_cast<std::size_t>(rank) * 16;
	Require(new_name_offset <= std::numeric_limits<std::size_t>::max() - header.name_size,
	        "mutated metadata size overflows addressable memory");
	std::vector<std::uint8_t> mutated(new_name_offset + header.name_size, 0);
	std::memcpy(mutated.data(), &header, sizeof(header));

	for (std::uint64_t axis = 0; axis < rank; axis++) {
		const auto dimension = axis < source_rank ? source_dimensions[axis] : 1;
		const auto chunk = axis < source_rank ? source_chunks[axis] : 1;
		std::memcpy(mutated.data() + dimension_offset + static_cast<std::size_t>(axis) * sizeof(std::uint64_t),
		            &dimension, sizeof(dimension));
		std::memcpy(mutated.data() + dimension_offset + static_cast<std::size_t>(rank) * sizeof(std::uint64_t) +
		                static_cast<std::size_t>(axis) * sizeof(std::uint64_t),
		            &chunk, sizeof(chunk));
	}
	if (header.name_size != 0) {
		std::memcpy(mutated.data() + new_name_offset, source.data() + name_offset, header.name_size);
	}
	return mutated;
}

void WriteRootMutation(const RootMetadata &source, const std::vector<std::uint8_t> &metadata, const fs::path &output) {
	std::vector<std::uint8_t> file(source.file.begin(),
	                               source.file.begin() + static_cast<std::ptrdiff_t>(source.root_offset));
	file.insert(file.end(), metadata.begin(), metadata.end());
	while (file.size() % alignof(OmTrailer_t) != 0) {
		file.push_back(0);
	}
	const auto trailer_offset = file.size();
	file.resize(trailer_offset + om_trailer_size(), 0);
	om_trailer_write(file.data() + trailer_offset, source.root_offset, metadata.size());
	WriteBinaryFile(output, file);
}

template <class Mutation>
fs::path MakeMetadataMutation(const RootMetadata &source, const fs::path &output, Mutation mutation) {
	auto metadata = source.metadata;
	mutation(metadata);
	WriteRootMutation(source, metadata, output);
	return output;
}

void CheckRejectedInput(duckdb::Connection &connection, const fs::path &path, const std::string &case_name) {
	auto result = connection.Query("SELECT value FROM read_om_raw(" + SqlLiteral(path) + ")");
	Require(result->HasError(), "read_om_raw unexpectedly accepted unsupported " + case_name + " fixture");
}

class TemporaryDirectory final {
public:
	TemporaryDirectory() {
		path_ = fs::temp_directory_path() / ("duckomo-raw-reader-" + std::to_string(static_cast<long long>(getpid())));
		std::error_code error;
		fs::remove_all(path_, error);
		fs::create_directories(path_);
	}

	~TemporaryDirectory() {
		std::error_code error;
		fs::remove_all(path_, error);
	}

	const fs::path &Path() const {
		return path_;
	}

private:
	fs::path path_;
};

void TestSupportBoundaries(duckdb::Connection &connection, const fs::path &raw_path, const fs::path &raw_csv,
                           const TemporaryDirectory &temporary) {
	const auto source = ReadRootMetadata(raw_path);

	const auto rank8_path = temporary.Path() / "rank8.om";
	WriteRootMutation(source, MetadataWithRank(source.metadata, 8), rank8_path);
	// This mutation proves rank-eight metadata is accepted at bind time. It
	// changes the logical shape metadata without re-encoding the payload, so it
	// cannot serve as a valid rank-eight decoding fixture.
	auto rank8_bind = connection.Query("DESCRIBE SELECT * FROM read_om_raw(" + SqlLiteral(rank8_path) + ")");
	Require(!rank8_bind->HasError(), "read_om_raw rejected supported rank-eight metadata: " + rank8_bind->GetError());

	const auto rank0_path = MakeMetadataMutation(source, temporary.Path() / "rank0.om", [](auto &metadata) {
		OmVariableArrayV3_t header{};
		std::memcpy(&header, metadata.data(), sizeof(header));
		header.dimension_count = 0;
		std::memcpy(metadata.data(), &header, sizeof(header));
	});
	CheckRejectedInput(connection, rank0_path, "zero-rank");

	const auto rank9_path = temporary.Path() / "rank9.om";
	WriteRootMutation(source, MetadataWithRank(source.metadata, 9), rank9_path);
	CheckRejectedInput(connection, rank9_path, "rank-nine");

	const auto zero_axis_path = MakeMetadataMutation(source, temporary.Path() / "zero-axis.om", [](auto &metadata) {
		OmVariableArrayV3_t header{};
		std::memcpy(&header, metadata.data(), sizeof(header));
		const std::uint64_t zero = 0;
		const auto dimension_offset = sizeof(header) + static_cast<std::size_t>(header.children_count) * 16;
		std::memcpy(metadata.data() + dimension_offset, &zero, sizeof(zero));
	});
	CheckRejectedInput(connection, zero_axis_path, "zero-length axis");

	const auto overflow_path = MakeMetadataMutation(source, temporary.Path() / "shape-overflow.om", [](auto &metadata) {
		OmVariableArrayV3_t header{};
		std::memcpy(&header, metadata.data(), sizeof(header));
		const auto dimension_offset = sizeof(header) + static_cast<std::size_t>(header.children_count) * 16;
		const auto too_large = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
		const std::uint64_t second_axis = 3;
		std::memcpy(metadata.data() + dimension_offset, &too_large, sizeof(too_large));
		std::memcpy(metadata.data() + dimension_offset + sizeof(std::uint64_t), &second_axis, sizeof(second_axis));
	});
	CheckRejectedInput(connection, overflow_path, "shape product above INT64_MAX");

	const auto non_fpx_path = MakeMetadataMutation(source, temporary.Path() / "non-fpx.om", [](auto &metadata) {
		OmVariableArrayV3_t header{};
		std::memcpy(&header, metadata.data(), sizeof(header));
		header.compression_type = static_cast<std::uint8_t>(COMPRESSION_NONE);
		std::memcpy(metadata.data(), &header, sizeof(header));
	});
	CheckRejectedInput(connection, non_fpx_path, "non-FPX compression");
}

} // namespace

int main() {
	try {
		const fs::path fixture_directory("test/data");
		const fs::path raw_path = fixture_directory / "raw.om";
		const fs::path raw_csv = fixture_directory / "raw.reference.csv";
		duckdb::DuckDB database(nullptr);
		duckdb::Connection connection(database);

		CheckQueryAgainstOracle(connection, raw_path, raw_csv);
		CheckQueryAgainstOracle(connection, fixture_directory / "special.om", fixture_directory / "special.reference.csv");
		CheckQueryAgainstOracle(connection, fixture_directory / "raw_large.om",
		                        fixture_directory / "raw_large.reference.csv", 3);

		TemporaryDirectory temporary;
		TestSupportBoundaries(connection, raw_path, raw_csv, temporary);
		std::cout << "raw reader oracle checks passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "raw reader oracle checks failed: " << exception.what() << '\n';
		return 1;
	}
}
