#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "spatial_reference.hpp"

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	(void)database;
}
} // namespace duckdb

namespace {
namespace fs = std::filesystem;
using duckomo_test::spatial_reference::DomainVariables;
using duckomo_test::spatial_reference::ReadOfficialReference;
using duckomo_test::spatial_reference::Require;
using duckomo_test::spatial_reference::Sha256;
using duckomo_test::spatial_reference::VerifyPinnedDomainManifest;

void RequireSuccess(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result != nullptr && !result->HasError(), "query failed: " + sql + "\n" +
	                                                     (result ? result->GetError() : "no result"));
}

std::string FindCoreFunctionsExtension() {
	if (const auto *override_path = std::getenv("DUCKOMO_CORE_FUNCTIONS_EXTENSION")) {
		return fs::absolute(override_path).string();
	}
	const fs::path repository("build/release/repository/v1.5.4");
	if (!fs::exists(repository)) {
		throw std::runtime_error("release extension repository is missing: " + repository.string());
	}
	for (const auto &entry : fs::recursive_directory_iterator(repository)) {
		if (entry.is_regular_file() && entry.path().filename() == "core_functions.duckdb_extension") {
			return fs::absolute(entry.path()).string();
		}
	}
	throw std::runtime_error("cannot find core_functions extension in the release repository");
}

void Run(const fs::path &sample_path, const fs::path &manifest_path, const fs::path &reference_directory) {
	using namespace duckomo_test::spatial_reference;
	Require(fs::file_size(sample_path) == 5812040, "domain sample byte size differs from the pinned object");
	VerifyPinnedDomainManifest(fs::absolute(manifest_path).string());

	duckdb::DBConfig config;
	config.SetOptionByName("allow_unsigned_extensions", true);
	duckdb::DuckDB database(nullptr, &config);
	duckdb::Connection connection(database);
	RequireSuccess(connection, "LOAD " + SqlLiteral(FindCoreFunctionsExtension()));
	const auto *duckomo_override = std::getenv("DUCKOMO_TEST_EXTENSION");
	const auto duckomo_extension = duckomo_override == nullptr
	                                   ? fs::absolute("build/release/extension/duckomo/duckomo.duckdb_extension").string()
	                                   : fs::absolute(duckomo_override).string();
	RequireSuccess(connection, "LOAD " + SqlLiteral(duckomo_extension));
	RequireSuccess(connection, "SET threads=1");
	Require(Sha256(connection, fs::absolute(sample_path).string()) == DOMAIN_SAMPLE_SHA256,
	        "domain sample SHA-256 differs from the pinned source object");

	std::array<std::vector<float>, 15> references;
	for (std::size_t index = 0; index < DomainVariables().size(); index++) {
		const auto &variable = DomainVariables()[index];
		const auto path = fs::absolute(reference_directory / variable.csv).string();
		Require(Sha256(connection, path) == variable.sha256,
		        "official reference SHA-256 differs from the manifest for " + std::string(variable.path));
		references[index] = ReadOfficialReference(path, DOMAIN_ROWS);
	}

	const auto sample = fs::absolute(sample_path).string();
	const auto query = "SELECT * FROM read_om(" + SqlLiteral(sample) +
	                   ", grid := {'nx':1440, 'ny':721, 'lat0':-90.0, 'lon0':-180.0, 'dlat':0.25, 'dlon':0.25, "
	                   "'order':'separate'}, spatial_axes := ['lat','lon']) ORDER BY latitude, longitude";
	auto result = connection.Query(query);
	Require(result != nullptr && !result->HasError(), "explicit-domain-grid query failed: " +
	                                                     (result ? result->GetError() : "no result"));
	auto &materialized = result->Cast<duckdb::MaterializedQueryResult>();
	CompareDomainResult(materialized, references);

	const auto domain_query = "SELECT * FROM read_om(" + SqlLiteral(sample) +
	                          ", domain := 'ncep_gfswave025') ORDER BY latitude, longitude";
	auto domain_result = connection.Query(domain_query);
	Require(domain_result != nullptr && !domain_result->HasError(), "registered-domain query failed: " +
	                                                                 (domain_result ? domain_result->GetError() : "no result"));
	CompareDomainResult(domain_result->Cast<duckdb::MaterializedQueryResult>(), references);
}

} // namespace

int main(int argc, char **argv) {
	try {
		if (argc != 4) {
			throw std::runtime_error("usage: domain_reference_test DOMAIN_FILE DOMAIN_MANIFEST REFERENCE_DIRECTORY");
		}
		Run(fs::path(argv[1]), fs::path(argv[2]), fs::path(argv[3]));
		std::cout << "ncep_gfswave025 explicit and registered grids match all 15 official references (1,038,240 positions each)\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "domain reference checks failed: " << error.what() << '\n';
		return 1;
	}
}
