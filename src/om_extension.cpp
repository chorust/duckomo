#include "duckdb/main/extension/extension_loader.hpp"
#include "duckomo/read_om.hpp"
#include "duckomo/raw_scan.hpp"
#include "duckomo_extension.hpp"

namespace duckdb {

void DuckomoExtension::Load(ExtensionLoader &loader) {
	loader.RegisterFunction(duckomo::GetReadOmRawFunction());
	loader.RegisterFunction(duckomo::GetReadOmFunction());
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
