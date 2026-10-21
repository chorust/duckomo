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

void TestRectangularSegmentsCoverExactlyTheRequestedRange() {
	for (const auto &shape : std::vector<std::vector<std::uint64_t>>{
	         {5}, {4, 3}, {2, 3, 4}, {2, 1, 3, 1}, {1, 1, 2, 1, 2, 1, 2, 3}}) {
		const auto total = CheckedShapeProduct(shape);
		for (std::uint64_t start = 0; start < total; ++start) {
			for (std::uint64_t count = 1; count <= total - start; ++count) {
				auto segments = BuildBatchSegments(shape, start, count);
				std::uint64_t covered = 0;
				for (const auto &segment : segments) {
					Require(segment.batch_offset == covered && segment.linear_index == start + covered,
					        "rectangular segments preserve source and output ordering");
					Require(CheckedShapeProduct(segment.read_count) == segment.count,
					        "rectangle shape must match the output element count");
					for (std::uint64_t offset = 0; offset < segment.count; ++offset) {
						auto coordinate = LinearIndexToCoordinates(segment.read_count, offset);
						std::uint64_t source_index = 0;
						for (std::size_t axis = 0; axis < shape.size(); ++axis) {
							coordinate[axis] += segment.read_offset[axis];
							Require(coordinate[axis] < shape[axis], "rectangle stays within source axes");
							source_index = source_index * shape[axis] + coordinate[axis];
						}
						Require(source_index == start + covered + offset,
						        "rectangle must not include gaps, duplicates, or unselected source positions");
					}
					covered += segment.count;
				}
				Require(covered == count, "rectangles cover the whole requested range exactly once");
			}
		}
	}
	const auto merged = BuildBatchSegments({2, 26, 7}, 0, 182);
	Require(merged.size() == 1 && merged[0].read_count == std::vector<std::uint64_t>({1, 26, 7}),
	        "complete adjacent point/time rows use one decoder selection");
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
		TestRectangularSegmentsCoverExactlyTheRequestedRange();
		TestInvalidRangesAndShapes();
		std::cout << "batch checks passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "batch checks failed: " << exception.what() << '\n';
		return 1;
	}
}
