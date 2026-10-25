#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/common/exception.hpp"
#include "duckomo/compat/duckdb_api.hpp"
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

void SetRemoteMergeGap(ClientContext &, SetScope, Value &parameter) {
	if (parameter.GetValue<std::uint64_t>() > static_cast<std::uint64_t>(1024) * 1024 * 1024) {
		throw InvalidInputException("duckomo_remote_merge_gap must be at most 1 GiB");
	}
}

void SetRemoteMaxRequest(ClientContext &, SetScope, Value &parameter) {
	const auto requested = parameter.GetValue<std::uint64_t>();
	if (requested < 512 || requested > static_cast<std::uint64_t>(1024) * 1024 * 1024) {
		throw InvalidInputException("duckomo_remote_max_request must be between 512 bytes and 1 GiB");
	}
}

void SetPrefetchBatches(ClientContext &, SetScope, Value &parameter) {
	const auto requested = parameter.GetValue<std::uint64_t>();
	if (requested < 1 || requested > 4) {
		throw InvalidInputException("duckomo_async_prefetch_batches must be between 1 and 4");
	}
}

void SetAsyncInflight(ClientContext &, SetScope, Value &parameter) {
	const auto requested = parameter.GetValue<std::uint64_t>();
	if (requested < 1 || requested > 256) {
		throw InvalidInputException("duckomo_async_max_inflight must be between 1 and 256");
	}
}

} // namespace

void DuckomoExtension::Load(ExtensionLoader &loader) {
	loader.RegisterFunction(duckomo::GetReadOmRawFunction());
	loader.RegisterFunction(duckomo::GetReadOmFunction());
	loader.RegisterFunction(duckomo::GetReadOmRegionsFunction());
	loader.RegisterFunction(duckomo::GetGridInfoFunction());
	loader.RegisterFunction(duckomo::GetLastScanMetricsFunction());
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	config.AddExtensionOption("duckomo_max_threads", "Maximum workers used by read_om (0 uses DuckDB's thread limit)",
	                          LogicalType::BIGINT, Value::BIGINT(0), SetDuckomoMaxThreads);
	config.AddExtensionOption("duckomo_remote_merge",
	                          "Merge nearby remote OM reads (local files ignore this)",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	config.AddExtensionOption("duckomo_remote_merge_gap",
	                          "Maximum gap in bytes merged into one remote OM read",
	                          LogicalType::UBIGINT, Value::UBIGINT(4096), SetRemoteMergeGap);
	config.AddExtensionOption("duckomo_remote_max_request",
	                          "Maximum bytes of one merged remote OM read",
	                          LogicalType::UBIGINT, Value::UBIGINT(64 * 1024), SetRemoteMaxRequest);
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	config.AddExtensionOption("duckomo_async_io",
	                          "Read merged remote OM ranges concurrently (DuckDB 2.0 only)",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
#else
	config.AddExtensionOption("duckomo_async_io",
	                          "Read merged remote OM ranges concurrently (no effect before DuckDB 2.0)",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(false));
#endif
	config.AddExtensionOption("duckomo_async_prefetch_batches",
	                          "Experimental remote full-scan prefetch window (1 disables cross-batch reads)",
	                          LogicalType::UBIGINT, Value::UBIGINT(1), SetPrefetchBatches);
	config.AddExtensionOption("duckomo_async_max_inflight",
	                          "Maximum concurrent remote OM reads per worker",
	                          LogicalType::UBIGINT, Value::UBIGINT(8), SetAsyncInflight);
	config.AddExtensionOption("duckomo_revalidate_metadata",
	                          "Re-read every OM variable before scanning (slower; detects metadata changes after bind)",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(false));

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
