#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "duckdb/main/connection.hpp"
#include "duckdb/main/materialized_query_result.hpp"

namespace duckomo_test {
namespace spatial_reference {

constexpr std::uint64_t DOMAIN_NX = 1440;
constexpr std::uint64_t DOMAIN_NY = 721;
constexpr std::uint64_t DOMAIN_ROWS = DOMAIN_NX * DOMAIN_NY;
constexpr double COORDINATE_TOLERANCE = 1e-9;
constexpr const char *DOMAIN_SAMPLE_SHA256 = "0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd";
constexpr const char *DOMAIN_UPSTREAM_COMMIT = "34b9cea169395be9b4686f2b5b23eca26dfef7a2";

struct VariableReference final {
	const char *path;
	const char *csv;
	const char *sha256;
};

inline const std::array<VariableReference, 15> &DomainVariables() {
	static const std::array<VariableReference, 15> variables = {{
	    {"/secondary_swell_wave_direction", "secondary_swell_wave_direction.csv", "c5869e3890039ed88b684fb2916f6b74925c08243d36692fe906c255e7c4eed5"},
	    {"/secondary_swell_wave_height", "secondary_swell_wave_height.csv", "41446f0736f55a5ad9bc1878059cebd7b168a67501cf3a402224c069451c7a48"},
	    {"/secondary_swell_wave_period", "secondary_swell_wave_period.csv", "e438f165d10dc7669ad2e6cc60797530453e32797c43a3745705261102dcf5fb"},
	    {"/swell_wave_direction", "swell_wave_direction.csv", "f7ea32de7afc46b242715f08cbf7e71ca4723624e82f2956fec365b96c94b956"},
	    {"/swell_wave_height", "swell_wave_height.csv", "e69cb9dd35f2834bf360cd74ff5796d2276a9ac894b6002e00077eb03e692993"},
	    {"/swell_wave_period", "swell_wave_period.csv", "9dc94349e8450e08465884a0adc1cf8a5da7f2d6a1c0c57d522765626776f4e1"},
	    {"/tertiary_swell_wave_direction", "tertiary_swell_wave_direction.csv", "e1052e3024573087d74cc7231da3198f4ca6d6ef70322a83aca08e9275c425cc"},
	    {"/tertiary_swell_wave_height", "tertiary_swell_wave_height.csv", "a978bc1ac061e4eaf5084c7c07543cf9edad268e80363f84322668867b224c8a"},
	    {"/tertiary_swell_wave_period", "tertiary_swell_wave_period.csv", "6403b60f88d31837c42ccdc928dcfe4bf5f1fc80aa407337316941a299fe81a0"},
	    {"/wave_direction", "wave_direction.csv", "4ef1d56c20de5739bdf8719aab09ad491dea9ccd5d7b3606fffe348067295a83"},
	    {"/wave_height", "wave_height.csv", "c5a0bd2bc2e0457ad1a3a0e4b1da877a655b2a8c37a654f8d25a3730c388e778"},
	    {"/wave_period", "wave_period.csv", "cebaca77f2a0c2c3131e6648de8e70ad9cdeb39c55f05452792e8690e2d85a9a"},
	    {"/wind_wave_direction", "wind_wave_direction.csv", "1b25c4905b056cefc8345e2f402a38ee5864e13302dc86182b50f272ca68e5ef"},
	    {"/wind_wave_height", "wind_wave_height.csv", "ab149d25f08b77714a06ce8030fc795315a7664dbd3600b9c566b70c1220c090"},
	    {"/wind_wave_period", "wind_wave_period.csv", "dce257e8a4d70893cfa42e25c2dd78d4989cf9ae15870df4a7ba20ccbd122dc3"},
	}};
	return variables;
}

inline void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

inline std::string ReadText(const std::string &path) {
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		throw std::runtime_error("cannot open reference asset: " + path);
	}
	std::ostringstream buffer;
	buffer << input.rdbuf();
	if (!input.good() && !input.eof()) {
		throw std::runtime_error("failed reading reference asset: " + path);
	}
	return buffer.str();
}

inline std::string SqlLiteral(const std::string &text) {
	std::string result = "'";
	for (const auto character : text) {
		result.push_back(character);
		if (character == '\'') {
			result.push_back('\'');
		}
	}
	result.push_back('\'');
	return result;
}

inline std::string Sha256(duckdb::Connection &connection, const std::string &path) {
	auto result = connection.Query("SELECT sha256(content) FROM read_blob(" + SqlLiteral(path) + ")");
	Require(result && !result->HasError(), "cannot hash file " + path + ": " +
	                                           (result ? result->GetError() : "no query result"));
	Require(result->RowCount() == 1, "hash query returned an unexpected number of rows for " + path);
	return result->GetValue(0, 0).GetValue<std::string>();
}

inline void VerifyPinnedDomainManifest(const std::string &manifest_path) {
	const auto manifest = ReadText(manifest_path);
	for (const auto *identity : {"\"domain\": \"ncep_gfswave025\"", "\"bytes\": 5812040",
	                             DOMAIN_SAMPLE_SHA256, DOMAIN_UPSTREAM_COMMIT,
	                             "\"shape\": [721, 1440]", "\"axes\": [\"lat\", \"lon\"]",
	                             "\"rows_per_variable\": 1038240", "\"coordinate_absolute_tolerance_degrees\": 1e-9"}) {
		Require(manifest.find(identity) != std::string::npos,
		        "domain manifest does not contain its pinned identity field: " + std::string(identity));
	}
	for (const auto &variable : DomainVariables()) {
		const auto path_token = "\"path\": \"" + std::string(variable.path) + "\"";
		const auto csv_token = "\"reference_csv\": \"" + std::string(variable.csv) + "\"";
		const auto hash_token = "\"sha256\": \"" + std::string(variable.sha256) + "\"";
		const auto path_position = manifest.find(path_token);
		Require(path_position != std::string::npos, "domain manifest is missing " + std::string(variable.path));
		const auto end_position = manifest.find('}', path_position);
		const auto row = manifest.substr(path_position, end_position - path_position);
		Require(row.find(csv_token) != std::string::npos && row.find(hash_token) != std::string::npos,
		        "domain manifest reference identity does not match " + std::string(variable.path));
	}
}

inline std::vector<float> ReadOfficialReference(const std::string &path, std::uint64_t expected_rows) {
	std::ifstream input(path);
	if (!input) {
		throw std::runtime_error("cannot open official-value reference: " + path);
	}
	input.imbue(std::locale::classic());
	std::string line;
	Require(static_cast<bool>(std::getline(input, line)) && line == "index,value",
	        "official-value reference has an unexpected header: " + path);
	std::vector<float> values;
	values.reserve(static_cast<std::size_t>(expected_rows));
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const auto comma = line.find(',');
		Require(comma != std::string::npos, "invalid CSV row in " + path);
		std::uint64_t index = 0;
		try {
			index = std::stoull(line.substr(0, comma));
		} catch (const std::exception &) {
			throw std::runtime_error("invalid CSV logical index in " + path);
		}
		Require(index == values.size(), "official-value CSV has missing or reordered logical positions: " + path);
		const auto text = line.substr(comma + 1);
		char *end = nullptr;
		const auto value = std::strtof(text.c_str(), &end);
		Require(end == text.c_str() + text.size(), "invalid Float32 value in " + path);
		values.push_back(value);
	}
	Require(values.size() == expected_rows, "official-value reference row count mismatch: " + path);
	return values;
}

inline std::pair<double, double> IndependentDomainCoordinate(std::uint64_t logical_index) {
	Require(logical_index < DOMAIN_ROWS, "logical index is outside the pinned domain");
	const auto latitude_index = logical_index / DOMAIN_NX;
	const auto longitude_index = logical_index % DOMAIN_NX;
	// This deliberately repeats the published scalar source formula instead
	// of calling RegularGrid or SpatialLayout from the production extension.
	return {-90.0 + static_cast<double>(latitude_index) * 0.25,
	        -180.0 + static_cast<double>(longitude_index) * 0.25};
}

inline void CompareDomainResult(duckdb::MaterializedQueryResult &result,
	                            const std::array<std::vector<float>, 15> &references) {
	Require(result.ColumnCount() == 18, "domain query must return 15 values, two coordinates, and valid_time");
	Require(result.RowCount() == DOMAIN_ROWS, "domain query returned an unexpected full-grid row count");
	std::uint64_t logical_index = 0;
	while (auto chunk = result.Fetch()) {
		for (duckdb::idx_t row = 0; row < chunk->size(); row++, logical_index++) {
			Require(chunk->GetValue(17, row).ToString() == "2026-10-02 03:00:00",
			        "domain valid_time differs from the pinned spatial snapshot");
			const auto coordinate = IndependentDomainCoordinate(logical_index);
			const auto latitude = chunk->GetValue(15, row).GetValue<double>();
			const auto longitude = chunk->GetValue(16, row).GetValue<double>();
			Require(std::abs(latitude - coordinate.first) <= COORDINATE_TOLERANCE &&
				        std::abs(longitude - coordinate.second) <= COORDINATE_TOLERANCE,
			        "domain coordinates differ from the independent formula at logical position " +
			            std::to_string(logical_index));
			for (std::size_t variable = 0; variable < references.size(); variable++) {
				const auto expected = references[variable][static_cast<std::size_t>(logical_index)];
				const auto actual = chunk->GetValue(static_cast<duckdb::idx_t>(variable), row);
				if (std::isnan(expected)) {
					Require(actual.IsNull(), std::string("official NaN/null position differs for variable ") +
					                            DomainVariables()[variable].path + " at logical position " +
					                            std::to_string(logical_index));
				} else {
					Require(!actual.IsNull() && actual.GetValue<float>() == expected,
					        "official value differs for variable " + std::string(DomainVariables()[variable].path) +
					            " at logical position " + std::to_string(logical_index));
				}
			}
		}
	}
	Require(logical_index == DOMAIN_ROWS, "domain result ended before all logical positions were compared");
}

} // namespace spatial_reference
} // namespace duckomo_test
