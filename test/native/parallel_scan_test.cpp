#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"

extern "C" {
#include "om_decoder.h"
#include "om_file.h"
}

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::string Scalar(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result != nullptr && !result->HasError(), sql + ": " + (result ? result->GetError() : "no result"));
	Require(result->RowCount() == 1 && result->ColumnCount() == 1, "expected one scalar result: " + sql);
	return result->GetValue(0, 0).ToString();
}

void RequireSuccess(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result != nullptr && !result->HasError(), sql + ": " + (result ? result->GetError() : "no result"));
}

void RequireFailure(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result != nullptr && result->HasError(), "expected query to fail: " + sql);
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		result.push_back(character);
		if (character == '\'') result.push_back('\'');
	}
	result.push_back('\'');
	return result;
}

std::string PerfRead(const std::string &path = "test/data/dimensions_perf.om") {
	return "read_om(" + SqlLiteral(path) + ", dimensions := map(['value'], [['time','member']]), axes := {"
	       "'time': {'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},"
	       "'member': {'axis':'member','start':0,'step':1}})";
}

class CorruptFinalChunkFixture final {
public:
	CorruptFinalChunkFixture() {
		std::ifstream input("test/data/dimensions_perf.om", std::ios::binary);
		Require(input.good(), "cannot read fixed dimensions performance fixture");
		std::vector<std::uint8_t> bytes;
		bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		const auto trailer_size = om_trailer_size();
		Require(bytes.size() >= trailer_size, "dimensions fixture is shorter than its OM v3 trailer");
		std::uint64_t root_offset = 0;
		std::uint64_t root_size = 0;
		Require(om_trailer_read(bytes.data() + bytes.size() - trailer_size, &root_offset, &root_size),
		        "dimensions fixture has an invalid OM v3 trailer");
		const auto *variable = om_variable_init(bytes.data() + root_offset);
		Require(variable != nullptr && om_variable_get_dimensions_count(variable) == 2,
		        "dimensions performance fixture must contain one rank-two array");
		const std::uint64_t read_offset[] = {511, 255};
		const std::uint64_t read_count[] = {1, 1};
		const std::uint64_t cube_offset[] = {0, 0};
		const std::uint64_t cube_dimensions[] = {1, 1};
		OmDecoder_t decoder{};
		Require(om_decoder_init(&decoder, variable, 2, read_offset, read_count, cube_offset, cube_dimensions, 512,
		                        64 * 1024) == ERROR_OK,
		        "official decoder rejected the final performance-fixture position");
		std::uint64_t payload_offset = 0;
		bool found_payload = false;
		OmDecoder_indexRead_t index_read{};
		om_decoder_init_index_read(&decoder, &index_read);
		while (om_decoder_next_index_read(&decoder, &index_read)) {
			Require(index_read.offset <= bytes.size() && index_read.count <= bytes.size() - index_read.offset,
			        "official decoder requested an invalid lookup range");
			OmDecoder_dataRead_t data_read{};
			om_decoder_init_data_read(&data_read, &index_read);
			OmError_t error = ERROR_OK;
			while (om_decoder_next_data_read(&decoder, &data_read, bytes.data() + index_read.offset,
			                                 index_read.count, &error)) {
				Require(data_read.offset < root_offset && data_read.offset < bytes.size(),
				        "official decoder requested an invalid compressed-data range");
				payload_offset = data_read.offset;
				found_payload = true;
			}
			Require(error == ERROR_OK, "official decoder rejected the final lookup range");
		}
		Require(found_payload, "official decoder found no final-chunk payload to corrupt");
		// FPX stores a shift byte at the beginning of each block; 255 is invalid.
		bytes[static_cast<std::size_t>(payload_offset)] = 255;
		path_ = (std::filesystem::temp_directory_path() /
		        ("duckomo-parallel-corrupt-" + std::to_string(static_cast<unsigned long long>(getpid())) + ".om"))
		           .string();
		std::ofstream output(path_, std::ios::binary | std::ios::trunc);
		Require(output.good(), "cannot create corrupt parallel-scan fixture");
		output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Require(output.good(), "cannot write corrupt parallel-scan fixture");
	}

	CorruptFinalChunkFixture(const CorruptFinalChunkFixture &) = delete;
	CorruptFinalChunkFixture &operator=(const CorruptFinalChunkFixture &) = delete;
	~CorruptFinalChunkFixture() {
		std::error_code ignored;
		if (!path_.empty()) std::filesystem::remove(path_, ignored);
	}
	const std::string &Path() const noexcept { return path_; }

private:
	std::string path_;
};

std::string LastMetric(duckdb::Connection &connection, const std::string &field) {
	return Scalar(connection, "SELECT regexp_extract(metrics, '\"" + field + "\":([0-9]+)', 1)::BIGINT "
	                        "FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1");
}

std::string HeavyExpression() {
	std::string expression = "value";
	for (std::size_t i = 0; i < 128; i++) expression = "sin(" + expression + ")";
	return expression;
}

void TestThreadLimitsAndEmptySelection(duckdb::Connection &connection) {
	std::string reference_sum;
	for (const auto workers : {1, 2, 4}) {
		RequireSuccess(connection, "SET threads=" + std::to_string(workers));
		RequireSuccess(connection, "SET duckomo_max_threads=" + std::to_string(workers));
		const auto sum = Scalar(connection, "SELECT sum(value) FROM " + PerfRead());
		if (reference_sum.empty()) reference_sum = sum;
		Require(sum == reference_sum, "thread limit changed the complete scan result");
		if (workers > 1) {
			Require(std::stoll(LastMetric(connection, "active_workers")) >= 2,
			        "parallel scan did not activate at least two workers");
			Require(std::stoll(LastMetric(connection, "scan_tasks_claimed")) >= 2,
			        "parallel scan did not claim multiple independent windows");
		}
	}

	const auto empty_query = "SELECT count(*) FROM " + PerfRead() +
	                          " WHERE valid_time < TIMESTAMP '1900-01-01 00:00:00'";
	Require(Scalar(connection, empty_query) == "0", "empty semantic selection returned rows");
	const auto value_reads = Scalar(connection,
	                                "SELECT regexp_extract(metrics, '\"data_bytes\":([0-9]+)', 1)::BIGINT "
	                                "FROM duckomo_last_scan_metrics() ORDER BY scan_id DESC LIMIT 1");
	Require(value_reads == "0", "empty parallel selection performed value reads");
}

void TestCancellationFailureAndRecovery(duckdb::Connection &connection) {
	std::atomic<bool> first_chunk{false};
	std::atomic<bool> allow_fetch{false};
	std::atomic<bool> finished{false};
	std::string query_error;
	std::thread query_thread([&] {
		try {
			auto result = connection.SendQuery("SELECT " + HeavyExpression() + " FROM " + PerfRead());
			if (!result) {
				query_error = "cancel query returned no result";
			} else if (result->HasError()) {
				query_error = result->GetError();
			} else {
				const auto chunk = result->Fetch();
				first_chunk.store(chunk != nullptr, std::memory_order_release);
				while (!allow_fetch.load(std::memory_order_acquire)) std::this_thread::yield();
				while (result->Fetch()) {
				}
				if (result->HasError()) query_error = result->GetError();
			}
		} catch (const std::exception &exception) {
			query_error = exception.what();
		}
		finished.store(true, std::memory_order_release);
	});
	while (!first_chunk.load(std::memory_order_acquire) && !finished.load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}
	if (first_chunk.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		connection.Interrupt();
	}
	allow_fetch.store(true, std::memory_order_release);
	query_thread.join();
	std::string lowered = query_error;
	for (auto &character : lowered) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	Require(lowered.find("interrupt") != std::string::npos || lowered.find("cancel") != std::string::npos,
	        "long parallel read was not cancelled: " + query_error);
	Require(Scalar(connection,
	               "SELECT count(*) FROM duckomo_last_scan_metrics() WHERE metrics LIKE '%\"status\":\"cancelled\"%'") == "1",
	        "cancelled parallel scan did not publish its terminal status");

	CorruptFinalChunkFixture corrupt;
	RequireFailure(connection, "SELECT sum(value) FROM " + PerfRead(corrupt.Path()));
	Require(Scalar(connection,
	               "SELECT count(*) FROM duckomo_last_scan_metrics() WHERE metrics LIKE '%\"status\":\"failure\"%'") == "1",
	        "failed parallel decode did not publish its terminal status");
	double expected_sum = 0;
	for (std::uint64_t position = 0; position < 131072; position++) {
		expected_sum += static_cast<double>(position) * 0.25 - 100.0;
	}
	Require(std::abs(std::stod(Scalar(connection, "SELECT sum(value) FROM " + PerfRead())) - expected_sum) < 0.01,
	        "a valid local query did not recover after parallel cancellation and failure");
}

} // namespace

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb

int main() {
	try {
		duckdb::DBConfig config;
		config.SetOptionByName("allow_unsigned_extensions", true);
		duckdb::DuckDB database(nullptr, &config);
		duckdb::Connection connection(database);
		auto core = connection.Query("LOAD './build/release/extension/core_functions/core_functions.duckdb_extension'");
		Require(core != nullptr && !core->HasError(),
		        "could not load CoreFunctions: " + (core ? core->GetError() : "no result"));
		TestThreadLimitsAndEmptySelection(connection);
		TestCancellationFailureAndRecovery(connection);
		std::cout << "parallel_scan_test: worker limits, empty selection, cancellation, and recovery checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "parallel_scan_test: " << error.what() << '\n';
		return 1;
	}
}
