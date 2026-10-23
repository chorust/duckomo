#pragma once

// Shared compatibility for native tests compiled against both DuckDB 1.5.x and 2.0.

#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
#include "duckdb/main/query_result_stream.hpp"
#endif

#include <memory>
#include <stdexcept>
#include <string>

// 1.5.x declares ExtensionHelper::LoadAllExtensions without defining it and calls it while opening a database, so each
// test supplies the body. 2.0 defines it in the engine and never calls it, so the test body becomes an ordinary
// function the test invokes itself with LoadAllExtensions.
#if DUCKOMO_DUCKDB_API_GENERATION < 2
#define DUCKOMO_TEST_LOAD_ALL_EXTENSIONS(database) \
	void ExtensionHelper::LoadAllExtensions(DuckDB &database)
#define DUCKOMO_TEST_LOAD_EXTENSIONS(database) static_cast<void>(database)
#else
namespace duckdb {
inline void DuckomoTestLoadAllExtensions(DuckDB &database);
inline void LoadAllExtensions(DuckDB &database) {
	DuckomoTestLoadAllExtensions(database);
}
} // namespace duckdb
#define DUCKOMO_TEST_LOAD_ALL_EXTENSIONS(database) \
	inline void DuckomoTestLoadAllExtensions(DuckDB &database)
#define DUCKOMO_TEST_LOAD_EXTENSIONS(database) duckdb::LoadAllExtensions(database)
#endif

namespace duckdb {
namespace duckomo_test {

// DuckDB 2.0 removed QueryResult::GetValue. This wrapper fetches every chunk once and serves later
// reads from them, so code written against the 1.5 MaterializedQueryResult::GetValue keeps working on both.
// Held by pointer so existing `result->GetValue(...)` call sites compile unchanged.
template <typename RESULT>
struct ResultRows {
	RESULT result;
	duckdb::vector<duckdb::unique_ptr<duckdb::DataChunk>> chunks;

	explicit ResultRows(RESULT result_p) : result(std::move(result_p)) {
		if (result && !result->HasError()) {
			for (auto chunk = result->Fetch(); chunk; chunk = result->Fetch()) {
				chunks.push_back(std::move(chunk));
			}
		}
	}

	explicit operator bool() const {
		return static_cast<bool>(result);
	}

	bool HasError() const {
		return result && result->HasError();
	}

	std::string GetError() const {
		return result ? result->GetError() : std::string();
	}

	duckdb::idx_t RowCount() const {
		duckdb::idx_t rows = 0;
		for (const auto &chunk : chunks) rows += chunk->size();
		return rows;
	}

	duckdb::idx_t ColumnCount() const {
		return result ? result->ColumnCount() : 0;
	}

	duckdb::Value GetValue(duckdb::idx_t column, duckdb::idx_t row) const {
		duckdb::idx_t seen = 0;
		for (const auto &chunk : chunks) {
			if (row < seen + chunk->size()) return chunk->GetValue(column, row - seen);
			seen += chunk->size();
		}
		throw std::runtime_error("query result has no value at the requested position");
	}
};

template <typename RESULT>
duckdb::unique_ptr<ResultRows<RESULT>> Rows(RESULT result) {
	return duckdb::make_uniq<ResultRows<RESULT>>(std::move(result));
}

// 2.0 made BaseQueryResult::types private behind GetTypes(); 1.5.x exposes the field directly.
inline const duckdb::vector<duckdb::LogicalType> &ResultTypes(duckdb::BaseQueryResult &result) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	return result.GetTypes();
#else
	return result.types;
#endif
}

// 2.0 takes extension load options where 1.5.x takes the extension path string.
inline void LoadExternalExtension(duckdb::DatabaseInstance &db, duckdb::FileSystem &fs, const std::string &path) {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	duckdb::ExtensionHelper::LoadExternalExtension(db, fs, duckdb::ExtensionLoadOptions(path));
#else
	duckdb::ExtensionHelper::LoadExternalExtension(db, fs, path);
#endif
}

// 2.0 removed MaterializedQueryResult; Connection::Query returns QueryResult, which 1.5.x materialized.
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
using MaterializedQueryResult = duckdb::QueryResult;
#else
using MaterializedQueryResult = duckdb::MaterializedQueryResult;
#endif

// 2.0 replaced SendQuery with Submit, whose handle yields chunks only through a QueryResultStream. This adapter exposes
// the Fetch/HasError/GetError surface the cancellation tests use, so both versions stream chunk by chunk.
struct StreamingQuery {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	duckdb::unique_ptr<duckdb::QueryResult> result;
	duckdb::unique_ptr<duckdb::QueryResultStream<duckdb::ChunkFormat>> stream;
#else
	duckdb::unique_ptr<duckdb::QueryResult> result;
#endif

	explicit operator bool() const {
		return static_cast<bool>(result);
	}

	bool HasError() const {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
		return (result && result->HasError()) || (stream && stream->HasError());
#else
		return result && result->HasError();
#endif
	}

	std::string GetError() const {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
		if (result && result->HasError()) return result->GetError();
		if (stream && stream->HasError()) return stream->GetError();
#else
		if (result && result->HasError()) return result->GetError();
#endif
		return {};
	}

	duckdb::unique_ptr<duckdb::DataChunk> Fetch() {
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
		if (!stream) {
			stream = duckdb::make_uniq<duckdb::QueryResultStream<duckdb::ChunkFormat>>(std::move(result));
		}
		return stream->Fetch();
#else
		return result->Fetch();
#endif
	}
};

inline duckdb::unique_ptr<StreamingQuery> SendQuery(duckdb::Connection &connection, const std::string &sql) {
	auto query = duckdb::make_uniq<StreamingQuery>();
#if DUCKOMO_DUCKDB_API_GENERATION >= 2
	query->result = connection.Submit(sql);
#else
	query->result = connection.SendQuery(sql);
#endif
	return query;
}

} // namespace duckomo_test
} // namespace duckdb
