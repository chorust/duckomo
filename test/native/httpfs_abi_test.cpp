#include "duckdb.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_file_opener.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"
#include "duckomo/remote_file.hpp"
#include "httpfs_extension.hpp"
#include "httpfs_om_range.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &) {
}
} // namespace duckdb

namespace {
using namespace duckdb;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (const auto character : value) {
		result.push_back(character);
		if (character == '\'') result.push_back('\'');
	}
	return result + "'";
}

std::string Scalar(Connection &connection, const std::string &query) {
	auto result = connection.Query(query);
	Require(result && !result->HasError(), query + ": " + (result ? result->GetError() : "no result"));
	Require(result->RowCount() == 1 && result->ColumnCount() == 1, "expected one scalar row: " + query);
	return result->GetValue(0, 0).ToString();
}

void RequireCapability(Connection &connection) {
	Require(Scalar(connection, "SELECT abi_version FROM httpfs_om_range_capabilities()") == "2",
	        "paired httpfs must expose ABI v2");
	Require(Scalar(connection, "SELECT upstream_commit FROM httpfs_om_range_capabilities()") ==
	            HTTPFS_OM_RANGE_UPSTREAM_COMMIT,
	        "paired httpfs reports the wrong upstream commit");
	Require(Scalar(connection, "SELECT patch_revision FROM httpfs_om_range_capabilities()") ==
	            HTTPFS_OM_RANGE_PATCH_REVISION,
	        "paired httpfs reports the wrong patch revision");
}

void RequireLocalReaderIndependentOfHttpfs() {
	duckdb::DuckDB database(nullptr);
	database.LoadStaticExtension<duckdb::DuckomoExtension>();
	Connection connection(database);
	Require(Scalar(connection, "SELECT count(*) FROM read_om('test/data/raw.om')") == "6",
	        "local read_om must work without loading httpfs");
	Require(Scalar(connection, "SELECT count(*) FROM read_om_raw('test/data/raw.om')") == "6",
	        "local read_om_raw must work without loading httpfs");
	auto remote = connection.Query("SELECT * FROM read_om('http://127.0.0.1/no-httpfs.om')");
	Require(remote && remote->HasError() &&
	            remote->GetError().find("paired httpfs extension") != std::string::npos,
	        "remote read_om must reject a missing paired httpfs capability");
	auto remote_raw = connection.Query("SELECT * FROM read_om_raw('http://127.0.0.1/no-httpfs.om')");
	Require(remote_raw && remote_raw->HasError(), "read_om_raw must remain a local-only entry point");
	Require(!duckdb::duckomo::IsCompatibleHttpfsRangeDescriptor(
	            "duckomo.httpfs.om-range|abi=1|wrong-commit|wrong-revision"),
	        "an incompatible capability descriptor was accepted");
}

void RequireStaticLoadOrder() {
	duckdb::DuckDB database(nullptr);
	database.LoadStaticExtension<duckdb::HttpfsExtension>();
	database.LoadStaticExtension<duckdb::DuckomoExtension>();
	Connection connection(database);
	RequireCapability(connection);
	Require(Scalar(connection, "SELECT count(*) FROM read_om('test/data/raw.om')") == "6",
	        "static companion load changed the local reader");
}

void RequireLoadableLoadOrder() {
	duckdb::DBConfig config;
	config.SetOptionByName("allow_unsigned_extensions", true);
	duckdb::DuckDB database(nullptr, &config);
	Connection connection(database);
	const auto httpfs_path = SqlLiteral(DUCKOMO_HTTPFS_EXTENSION_PATH);
	const auto duckomo_path = SqlLiteral(DUCKOMO_DUCKOMO_EXTENSION_PATH);
	auto load_httpfs = connection.Query("LOAD " + httpfs_path);
	Require(load_httpfs && !load_httpfs->HasError(), "loadable httpfs failed: " +
	        (load_httpfs ? load_httpfs->GetError() : "no result"));
	auto load_duckomo = connection.Query("LOAD " + duckomo_path);
	Require(load_duckomo && !load_duckomo->HasError(), "loadable duckomo failed: " +
	        (load_duckomo ? load_duckomo->GetError() : "no result"));
	RequireCapability(connection);
	Require(Scalar(connection, "SELECT count(*) FROM read_om('test/data/raw.om')") == "6",
	        "loadable companion pair changed the local reader");
}

class WrongAbiOpener final : public ClientContextFileOpener, public HTTPFSOmRangeProviderV2 {
public:
	explicit WrongAbiOpener(ClientContext &context) : ClientContextFileOpener(context) {
	}

	std::uint32_t OmRangeAbiVersion() const noexcept override {
		return HTTPFS_OM_RANGE_ABI_VERSION + 1;
	}

	std::shared_ptr<HTTPFSOmRangeSessionV2> OpenOmRangeSessionV2(const std::string &) override {
		throw std::runtime_error("httpfs must reject the ABI before asking for a session");
	}
};

void RequireWrongAbiRejected() {
	duckdb::DuckDB database(nullptr);
	database.LoadStaticExtension<duckdb::HttpfsExtension>();
	auto context = duckdb::make_shared_ptr<duckdb::ClientContext>(database.instance);
	WrongAbiOpener opener(*context);
	bool rejected = false;
	try {
		auto handle = context->db->config.file_system->OpenFile(
		    "http://127.0.0.1/never-requested", duckdb::FileOpenFlags(1), &opener);
		if (handle) handle->GetFileSize();
	} catch (const std::exception &error) {
		rejected = std::string(error.what()).find("incompatible DuckOMO httpfs range provider ABI") !=
		           std::string::npos;
	}
	Require(rejected, "httpfs did not reject a provider with an incompatible range ABI");
}

} // namespace

int main() {
	try {
		RequireLocalReaderIndependentOfHttpfs();
		RequireStaticLoadOrder();
		RequireLoadableLoadOrder();
		RequireWrongAbiRejected();
		std::cout << "httpfs_abi_test: static/loadable capability and local independence checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "httpfs_abi_test: " << error.what() << '\n';
		return 1;
	}
}
