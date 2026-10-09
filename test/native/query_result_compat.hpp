#pragma once

#include "duckdb.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckomo_test {

struct CollectedQueryResult final {
	std::vector<std::vector<duckdb::Value>> rows;
	duckdb::idx_t column_count = 0;

	duckdb::idx_t RowCount() const {
		return static_cast<duckdb::idx_t>(rows.size());
	}

	duckdb::idx_t ColumnCount() const {
		return column_count;
	}

	const duckdb::Value &GetValue(duckdb::idx_t column, duckdb::idx_t row) const {
		return rows.at(static_cast<std::size_t>(row)).at(static_cast<std::size_t>(column));
	}
};

inline std::unique_ptr<CollectedQueryResult> Query(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	if (!result || result->HasError()) {
		throw std::runtime_error(sql + ": " + (result ? result->GetError() : "no result"));
	}
	auto collected = std::make_unique<CollectedQueryResult>();
	collected->column_count = result->ColumnCount();
	for (auto chunk = result->Fetch(); chunk; chunk = result->Fetch()) {
		chunk->Flatten();
		for (duckdb::idx_t row = 0; row < chunk->size(); row++) {
			std::vector<duckdb::Value> values;
			values.reserve(collected->column_count);
			for (duckdb::idx_t column = 0; column < collected->column_count; column++) {
				values.push_back(chunk->GetValue(column, row));
			}
			collected->rows.push_back(std::move(values));
		}
	}
	if (result->HasError()) {
		throw std::runtime_error(sql + ": " + result->GetError());
	}
	return collected;
}

} // namespace duckomo_test
