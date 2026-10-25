#include "duckomo/compat/duckdb_api.hpp"
#include "duckdb/main/extension_helper.hpp"

// Standalone 1.5 tests need the no-extension loader. In 2.0 the generated
// capability loader owns this surface; no upstream checkout patch is needed.
#if DUCKOMO_DUCKDB_API_GENERATION < 2
namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &) {
}
vector<string> ExtensionHelper::LoadedExtensionTestPaths() {
	return {};
}
} // namespace duckdb
#endif
