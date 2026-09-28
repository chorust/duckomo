#include "duckomo/batch.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb::duckomo::BuildBatchSegments;
using duckdb::duckomo::CheckedShapeProduct;
using duckdb::duckomo::LinearIndexToCoordinates;
using duckdb::duckomo::ReaderError;
using duckdb::duckomo::ReaderErrorCode;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void RequireThrows(ReaderErrorCode expected, const std::function<void()> &action, const std::string &message) {
	try {
		action();
	} catch (const ReaderError &error) {
		Require(error.Code() == expected, message + ": wrong ReaderErrorCode");
		return;
	}
	throw std::runtime_error(message + ": expected ReaderError");
}

void RequireVector(const std::vector<std::uint64_t> &actual, const std::vector<std::uint64_t> &expected,
	               const std::string &message) {
	Require(actual == expected, message);
}

void TestRowMajorCoordinates() {
	RequireVector(LinearIndexToCoordinates({2, 3}, 0), {0, 0}, "rank-two first coordinate");
	RequireVector(LinearIndexToCoordinates({2, 3}, 4), {1, 1}, "rank-two row-major coordinate");
	RequireVector(LinearIndexToCoordinates({2, 3}, 5), {1, 2}, "rank-two final coordinate");
	RequireVector(LinearIndexToCoordinates({7}, 6), {6}, "rank-one coordinate");
	RequireVector(LinearIndexToCoordinates({1, 1, 1, 1, 1, 1, 1, 1}, 0),
	               {0, 0, 0, 0, 0, 0, 0, 0}, "rank-eight coordinate");
	RequireThrows(ReaderErrorCode::InvalidSelection, [] { LinearIndexToCoordinates({2, 3}, 6); },
	               "index past end");
}

void TestLastAxisSegments() {
	auto segments = BuildBatchSegments({2, 3}, 2, 4);
	Require(segments.size() == 2, "non-square row crossing has two contiguous segments");
	const auto &first = segments[0];
	Require(first.batch_offset == 0 && first.linear_index == 2 && first.count == 1,
	        "first row segment range");
	RequireVector(first.read_offset, {0, 2}, "first row segment offset");
	RequireVector(first.read_count, {1, 1}, "first row segment count");
	const auto &second = segments[1];
	Require(second.batch_offset == 1 && second.linear_index == 3 && second.count == 3,
	        "second row segment range");
	RequireVector(second.read_offset, {1, 0}, "second row segment offset");
	RequireVector(second.read_count, {1, 3}, "second row segment count");

	auto rank_one = BuildBatchSegments({5}, 1, 4);
	Require(rank_one.size() == 1, "rank-one range is one contiguous segment");
	Require(rank_one[0].count == 4 && rank_one[0].batch_offset == 0, "rank-one segment size");
	RequireVector(rank_one[0].read_offset, {1}, "rank-one segment offset");
	RequireVector(rank_one[0].read_count, {4}, "rank-one segment count");

	auto at_end = BuildBatchSegments({5}, 5, 0);
	Require(at_end.empty(), "empty range at end is valid");
	auto exact_end = BuildBatchSegments({5}, 1, 4);
	Require(exact_end.size() == 1 && exact_end[0].count == 4, "range ending at row_count is valid");

	const auto vector_size = static_cast<std::uint64_t>(STANDARD_VECTOR_SIZE);
	auto max_batch = BuildBatchSegments({vector_size + 1}, 0, vector_size);
	Require(max_batch.size() == 1 && max_batch[0].count == vector_size,
	        "STANDARD_VECTOR_SIZE batch is accepted");
}

void TestInvalidRangesAndShapes() {
	RequireThrows(ReaderErrorCode::InvalidShape, [] { CheckedShapeProduct({2, 0, 3}); }, "zero axis");
	RequireThrows(ReaderErrorCode::ShapeOverflow,
	               [] { CheckedShapeProduct({std::numeric_limits<std::uint64_t>::max(), 2}); }, "shape overflow");
	RequireThrows(ReaderErrorCode::InvalidSelection, [] { BuildBatchSegments({5}, 6, 0); }, "start past end");
	RequireThrows(ReaderErrorCode::InvalidSelection, [] { BuildBatchSegments({5}, 4, 2); }, "range past end");
	RequireThrows(ReaderErrorCode::InvalidSelection,
	               [] { BuildBatchSegments({static_cast<std::uint64_t>(STANDARD_VECTOR_SIZE) + 1}, 0,
	                                       static_cast<std::uint64_t>(STANDARD_VECTOR_SIZE) + 1); },
	               "batch larger than STANDARD_VECTOR_SIZE");
}

} // namespace

int main() {
	try {
		TestRowMajorCoordinates();
		TestLastAxisSegments();
		TestInvalidRangesAndShapes();
		std::cout << "batch checks passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "batch checks failed: " << exception.what() << '\n';
		return 1;
	}
}
