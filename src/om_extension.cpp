#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/common/exception.hpp"
#include "duckomo/read_om.hpp"
#include "duckomo/raw_scan.hpp"
#include "duckomo_extension.hpp"

namespace duckdb {

namespace {

void SetDuckomoMaxThreads(ClientContext &, SetScope, Value &parameter) {
	const auto requested = parameter.GetValue<std::int64_t>();
	if (requested < 0) {
		throw InvalidInputException("duckomo_max_threads must be zero or a positive integer");
	}
}

} // namespace

void DuckomoExtension::Load(ExtensionLoader &loader) {
	loader.RegisterFunction(duckomo::GetReadOmRawFunction());
	loader.RegisterFunction(duckomo::GetReadOmFunction());
	loader.RegisterFunction(duckomo::GetGridInfoFunction());
	loader.RegisterFunction(duckomo::GetLastScanMetricsFunction());
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	config.AddExtensionOption("duckomo_max_threads", "Maximum workers used by read_om (0 uses DuckDB's thread limit)",
	                          LogicalType::BIGINT, Value::BIGINT(0), SetDuckomoMaxThreads);

}

std::string DuckomoExtension::Name() {
	return "duckomo";
}

std::string DuckomoExtension::Version() const {
#ifdef EXT_VERSION_DUCKOMO
	return EXT_VERSION_DUCKOMO;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(duckomo, loader) {
	duckdb::DuckomoExtension extension;
	extension.Load(loader);
}

} // extern "C"
