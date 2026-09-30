#include "duckomo/domain_bbox.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>

#include "duckomo/om_reader.hpp"

namespace duckdb {
namespace duckomo {
namespace {

bool IsWktSpace(char value) {
	return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool IsWordCharacter(char value) {
	return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

bool MatchesBboxKeyword(const std::string &wkt, std::size_t position) {
	static constexpr char KEYWORD[] = "BBOX";
	if (position != 0 && IsWordCharacter(wkt[position - 1])) {
		return false;
	}
	if (position + sizeof(KEYWORD) - 1 > wkt.size()) {
		return false;
	}
	for (std::size_t offset = 0; offset < sizeof(KEYWORD) - 1; offset++) {
		const auto value = static_cast<unsigned char>(wkt[position + offset]);
		if (std::toupper(value) != KEYWORD[offset]) {
			return false;
		}
	}
	const auto end = position + sizeof(KEYWORD) - 1;
	return end == wkt.size() || !IsWordCharacter(wkt[end]);
}

std::size_t SkipWktSpace(const std::string &wkt, std::size_t position) {
	while (position < wkt.size() && IsWktSpace(wkt[position])) {
		position++;
	}
	return position;
}

[[noreturn]] void ThrowMalformedBbox() {
	throw ReaderError(ReaderErrorCode::InvalidMetadata, "crs_wkt contains a malformed BBOX clause");
}

} // namespace

std::optional<std::array<double, 4>> ParseWktBbox(const std::string &wkt) {
	bool in_quoted_string = false;
	for (std::size_t position = 0; position < wkt.size(); position++) {
		if (wkt[position] == '"') {
			if (in_quoted_string && position + 1 < wkt.size() && wkt[position + 1] == '"') {
				position++;
				continue;
			}
			in_quoted_string = !in_quoted_string;
			continue;
		}
		if (in_quoted_string || !MatchesBboxKeyword(wkt, position)) {
			continue;
		}

		auto cursor = SkipWktSpace(wkt, position + 4);
		if (cursor >= wkt.size() || wkt[cursor] != '[') {
			ThrowMalformedBbox();
		}
		cursor = SkipWktSpace(wkt, cursor + 1);

		std::array<double, 4> result{};
		for (std::size_t index = 0; index < result.size(); index++) {
			try {
				std::size_t consumed = 0;
				result[index] = std::stod(wkt.substr(cursor), &consumed);
				if (consumed == 0 || !std::isfinite(result[index])) {
					ThrowMalformedBbox();
				}
				cursor = SkipWktSpace(wkt, cursor + consumed);
			} catch (const std::invalid_argument &) {
				ThrowMalformedBbox();
			} catch (const std::out_of_range &) {
				ThrowMalformedBbox();
			}

			const char expected = index + 1 == result.size() ? ']' : ',';
			if (cursor >= wkt.size() || wkt[cursor] != expected) {
				ThrowMalformedBbox();
			}
			cursor = SkipWktSpace(wkt, cursor + 1);
		}
		return result;
	}
	return std::nullopt;
}

WktBboxStatus ValidateWktBbox(const std::string &wkt, const RegularGrid &grid) {
	const auto bbox = ParseWktBbox(wkt);
	if (!bbox) {
		return WktBboxStatus::Absent;
	}
	const std::array<double, 4> expected = {
	    grid.LatitudeOrigin(), grid.LongitudeOrigin(),
	    grid.LatitudeOrigin() + static_cast<double>(grid.Ny() - 1) * grid.LatitudeStep(),
	    grid.LongitudeOrigin() + static_cast<double>(grid.Nx() - 1) * grid.LongitudeStep()};
	for (std::size_t index = 0; index < bbox->size(); index++) {
		// Open-Meteo writes Float coordinates and prints their WKT decimal form.
		if (std::abs((*bbox)[index] - expected[index]) > 1e-3) {
			return WktBboxStatus::ConflictsWithGrid;
		}
	}
	return WktBboxStatus::MatchesGrid;
}

} // namespace duckomo
} // namespace duckdb
