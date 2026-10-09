#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"

extern "C" {
#include "om_decoder.h"
#include "om_file.h"
}

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

constexpr const char *EXTENSION_PATH = "./build/release/extension/duckomo/duckomo.duckdb_extension";
constexpr const char *RAW_FIXTURE = "test/data/raw.om";
constexpr const char *RAW_LARGE_FIXTURE = "test/data/raw_large.om";

// Override DuckDB's generated loader hook so this native test loads the same
// quickstart extension artifact without pulling unrelated built-in extension
// archives into its link.
namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	const auto *extension_override = std::getenv("DUCKOMO_EXTENSION_PATH");
	const auto *extension_path = extension_override && extension_override[0] != '\0' ? extension_override : EXTENSION_PATH;
	ExtensionHelper::LoadExternalExtension(*database.instance, database.GetFileSystem(), extension_path);
}
} // namespace duckdb

namespace {

using duckdb::Connection;
using duckdb::DuckDB;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		if (character == '\'') {
			result += "''";
		} else {
			result += character;
		}
	}
	result += "'";
	return result;
}

std::string ScanQuery(const std::string &path) {
	return "SELECT value FROM read_om_raw(" + SqlLiteral(path) + ")";
}

void RequireNoQueryError(Connection &connection, const std::string &query, const std::string &description) {
	auto result = connection.Query(query);
	Require(result != nullptr, description + ": query returned no result");
	if (result->HasError()) {
		throw std::runtime_error(description + ": " + result->GetError());
	}
}

std::string RequireQueryError(Connection &connection, const std::string &query, const std::string &description) {
	auto result = connection.Query(query);
	Require(result != nullptr, description + ": query returned no result");
	Require(result->HasError(), description + ": expected query to fail");
	return result->GetError();
}

std::vector<std::uint8_t> ReadBytes(const std::string &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "cannot open official fixture: " + path);
	return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

class TemporaryPartialFixture final {
public:
	explicit TemporaryPartialFixture(const std::string &source_path) {
		auto bytes = ReadBytes(source_path);
		const auto trailer_size = om_trailer_size();
		Require(bytes.size() >= trailer_size, "official fixture is shorter than its OM v3 trailer");

		std::uint64_t root_offset = 0;
		std::uint64_t root_size = 0;
		const auto *trailer = bytes.data() + bytes.size() - trailer_size;
		Require(om_trailer_read(trailer, &root_offset, &root_size), "official fixture has no readable OM v3 trailer");
		const auto trailer_offset = static_cast<std::uint64_t>(bytes.size() - trailer_size);
		Require(root_offset + root_size == trailer_offset, "official fixture has an unexpected OM v3 layout");

		const auto *variable = om_variable_init(bytes.data() + root_offset);
		Require(variable != nullptr && om_variable_get_type(variable) == DATA_TYPE_FLOAT_ARRAY &&
		            om_variable_get_compression(variable) == COMPRESSION_FPX_XOR2D &&
		            om_variable_get_dimensions_count(variable) == 2,
		        "raw_large fixture must remain a rank-two Float32 FPX array");
		const auto *dimensions = om_variable_get_dimensions(variable);
		const auto *chunks = om_variable_get_chunks(variable);
		Require(dimensions != nullptr && chunks != nullptr && dimensions[0] == 73 && dimensions[1] == 61 &&
		            chunks[0] == 7 && chunks[1] == 13,
		        "raw_large fixture shape or chunk layout changed");
		const std::uint64_t read_offset[] = {70, 52};
		const std::uint64_t read_count[] = {3, 9};
		const std::uint64_t cube_offset[] = {0, 0};
		const std::uint64_t cube_dimensions[] = {3, 9};
		OmDecoder_t decoder{};
		Require(om_decoder_init(&decoder, variable, 2, read_offset, read_count, cube_offset, cube_dimensions, 512,
		                        64 * 1024) == ERROR_OK,
		        "official OM decoder could not initialize the final raw_large selection");

		std::uint64_t payload_offset = 0;
		bool found_payload = false;
		OmDecoder_indexRead_t index_read{};
		om_decoder_init_index_read(&decoder, &index_read);
		while (om_decoder_next_index_read(&decoder, &index_read)) {
			Require(index_read.offset <= bytes.size() && index_read.count <= bytes.size() - index_read.offset,
			        "official OM decoder requested an invalid lookup-table range");
			const auto *index_data = bytes.data() + index_read.offset;
			OmDecoder_dataRead_t data_read{};
			om_decoder_init_data_read(&data_read, &index_read);
			OmError_t read_error = ERROR_OK;
			while (om_decoder_next_data_read(&decoder, &data_read, index_data, index_read.count, &read_error)) {
				Require(data_read.count > 0 && data_read.offset < root_offset &&
				            data_read.count <= root_offset - data_read.offset,
				        "official OM decoder requested an invalid final-chunk payload range");
				payload_offset = data_read.offset;
				found_payload = true;
			}
			Require(read_error == ERROR_OK, "official OM decoder rejected the final raw_large lookup range");
		}
		Require(found_payload, "official OM decoder did not request payload bytes for the final raw_large selection");
		// FPX begins each encoded block with its shift. 255 is outside the legal
		// Float32 shift range and makes this final chunk fail during scan decoding.
		bytes[static_cast<std::size_t>(payload_offset)] = 255;

		static std::atomic<std::uint64_t> next_id{0};
		path_ = (std::filesystem::temp_directory_path() /
		         ("duckomo-lifecycle-" + std::to_string(static_cast<unsigned long long>(getpid())) + "-" +
		          std::to_string(next_id.fetch_add(1)) + ".om"))
		            .string();
		try {
			std::ofstream output(path_, std::ios::binary | std::ios::trunc);
			Require(output.good(), "cannot create temporary partial OM fixture");
			output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			Require(output.good(), "cannot write temporary partial OM fixture");
		} catch (...) {
			std::error_code ignored;
			std::filesystem::remove(path_, ignored);
			path_.clear();
			throw;
		}
	}

	TemporaryPartialFixture(const TemporaryPartialFixture &) = delete;
	TemporaryPartialFixture &operator=(const TemporaryPartialFixture &) = delete;
	~TemporaryPartialFixture() {
		if (!path_.empty()) {
			std::error_code ignored;
			std::filesystem::remove(path_, ignored);
		}
	}

	const std::string &Path() const {
		return path_;
	}

private:
	std::string path_;
};

void LoadExtension(Connection &connection) {
	RequireNoQueryError(connection, "LOAD " + SqlLiteral(EXTENSION_PATH), "load quickstart extension");
}

void TestScanFailureRecovery(Connection &connection, const TemporaryPartialFixture &partial_fixture) {
	for (const auto expected_rows : {2048U, 4096U}) {
		auto prefix = connection.Query("SELECT value FROM read_om_raw(" + SqlLiteral(partial_fixture.Path()) +
		                              ") LIMIT " + std::to_string(expected_rows));
		Require(prefix != nullptr, "prefix scan returned no query result");
		if (prefix->HasError()) {
			throw std::runtime_error("prefix scan failed before the corrupted final chunk: " + prefix->GetError());
		}
		Require(prefix->RowCount() == expected_rows,
		        "prefix scan did not return the expected rows before the corrupted final chunk");
	}
	RequireQueryError(connection, ScanQuery(partial_fixture.Path()), "full scan with a corrupt final chunk");

	auto recovered = connection.Query(ScanQuery(RAW_FIXTURE));
	Require(recovered != nullptr, "valid raw scan returned no result after partial scan error");
	if (recovered->HasError()) {
		throw std::runtime_error("valid raw scan failed after partial scan error: " + recovered->GetError());
	}
	Require(recovered->RowCount() == 6, "valid raw scan returned the wrong row count after partial scan error");
	for (duckdb::idx_t row = 0; row < 6; row++) {
		Require(recovered->GetValue(0, row).GetValue<float>() == static_cast<float>(row),
		        "valid raw scan returned an incorrect value after partial scan error");
	}
}

void TestCancellationAndRecovery(Connection &connection) {
	std::atomic<bool> started{false};
	std::atomic<bool> completed{false};
	bool failed_with_interrupt = false;
	std::string query_error;
	std::thread query_thread([&] {
		try {
			auto result = connection.SendQuery("SELECT a.value FROM read_om_raw(" + SqlLiteral(RAW_LARGE_FIXTURE) +
			                                  ") AS a CROSS JOIN read_om_raw(" + SqlLiteral(RAW_LARGE_FIXTURE) +
			                                  ") AS b CROSS JOIN read_om_raw(" + SqlLiteral(RAW_LARGE_FIXTURE) +
			                                  ") AS c");
			if (!result) {
				query_error = "query returned no result";
			} else if (result->HasError()) {
				query_error = result->GetError();
			} else {
				started.store(true, std::memory_order_release);
				while (result->Fetch()) {
				}
				if (result->HasError()) {
					query_error = result->GetError();
				}
			}
		} catch (const std::exception &exception) {
			query_error = exception.what();
		}
		std::string lowered = query_error;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
		               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		failed_with_interrupt = lowered.find("interrupt") != std::string::npos;
		completed.store(true, std::memory_order_release);
	});
	while (!started.load(std::memory_order_acquire) && !completed.load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}
	if (started.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		connection.Interrupt();
	}
	query_thread.join();
	Require(failed_with_interrupt, "long-running raw scan query was not cancelled: " + query_error);

	auto recovered = connection.Query(ScanQuery(RAW_FIXTURE));
	Require(recovered != nullptr, "valid raw scan returned no result after cancellation");
	if (recovered->HasError()) {
		throw std::runtime_error("valid raw scan failed after cancellation: " + recovered->GetError());
	}
}

std::size_t CountOpenFileDescriptors() {
	DIR *directory = opendir("/proc/self/fd");
	Require(directory != nullptr, "cannot open /proc/self/fd");
	std::size_t count = 0;
	while (const auto *entry = readdir(directory)) {
		if (entry->d_name[0] != '.') {
			++count;
		}
	}
	closedir(directory);
	return count;
}

void TestFileDescriptorStability(Connection &connection, const TemporaryPartialFixture &partial_fixture) {
	// Warm up both success and error paths before taking the baseline so one-time
	// runtime initialization does not look like a per-query descriptor leak.
	RequireNoQueryError(connection, ScanQuery(RAW_FIXTURE), "file descriptor warmup scan");
	RequireQueryError(connection, ScanQuery(partial_fixture.Path()), "file descriptor warmup error scan");
	const auto baseline = CountOpenFileDescriptors();
	std::size_t maximum = baseline;
	std::vector<std::size_t> samples;
	samples.reserve(100);
	for (std::size_t iteration = 0; iteration < 100; iteration++) {
		RequireNoQueryError(connection, ScanQuery(RAW_FIXTURE), "valid descriptor iteration " + std::to_string(iteration));
		RequireQueryError(connection, ScanQuery(partial_fixture.Path()),
		                  "error descriptor iteration " + std::to_string(iteration));
		const auto current = CountOpenFileDescriptors();
		samples.push_back(current);
		maximum = std::max(maximum, current);
	}

	bool strictly_increasing = samples.size() > 1;
	for (std::size_t index = 1; index < samples.size(); index++) {
		if (samples[index] <= samples[index - 1]) {
			strictly_increasing = false;
		}
	}
	Require(!strictly_increasing, "file descriptor count increased monotonically over 100 valid/error query pairs");
	Require(maximum <= baseline, "file descriptor count grew after 100 valid/error query pairs");
}

} // namespace

int main() {
	try {
	duckdb::DBConfig config;
	config.SetOptionByName("allow_unsigned_extensions", true);
	DuckDB database(nullptr, &config);
		Connection connection(database);
		LoadExtension(connection);
		TemporaryPartialFixture partial_fixture(RAW_LARGE_FIXTURE);
		TestCancellationAndRecovery(connection);
		TestScanFailureRecovery(connection, partial_fixture);
		TestFileDescriptorStability(connection, partial_fixture);
		std::cout << "lifecycle checks passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "lifecycle checks failed: " << exception.what() << '\n';
		return 1;
	}
}
