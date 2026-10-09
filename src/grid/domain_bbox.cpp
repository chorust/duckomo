#include "duckomo/domain_bbox.hpp"

#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

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

std::string_view Trim(std::string_view value) {
	while (!value.empty() && IsWktSpace(value.front())) value.remove_prefix(1);
	while (!value.empty() && IsWktSpace(value.back())) value.remove_suffix(1);
	return value;
}

bool EqualInsensitive(std::string_view left, std::string_view right) {
	if (left.size() != right.size()) return false;
	for (std::size_t index = 0; index < left.size(); index++) {
		if (std::toupper(static_cast<unsigned char>(left[index])) !=
		    std::toupper(static_cast<unsigned char>(right[index]))) {
			return false;
		}
	}
	return true;
}

bool SplitWktArguments(std::string_view body, std::vector<std::string_view> &arguments) {
	std::size_t start = 0;
	std::size_t depth = 0;
	bool quoted = false;
	for (std::size_t index = 0; index < body.size(); index++) {
		const auto ch = body[index];
		if (ch == '"') {
			if (quoted && index + 1 < body.size() && body[index + 1] == '"') {
				index++;
				continue;
			}
			quoted = !quoted;
			continue;
		}
		if (quoted) continue;
		if (ch == '[') {
			depth++;
		} else if (ch == ']') {
			if (depth == 0) return false;
			depth--;
		} else if (ch == ',' && depth == 0) {
			const auto part = Trim(body.substr(start, index - start));
			if (part.empty()) return false;
			arguments.emplace_back(part);
			start = index + 1;
		}
	}
	if (quoted || depth != 0) return false;
	const auto last = Trim(body.substr(start));
	if (last.empty()) return false;
	arguments.emplace_back(last);
	return true;
}

bool ParseWktClause(std::string_view text, std::string &keyword, std::vector<std::string_view> &arguments) {
	text = Trim(text);
	std::size_t cursor = 0;
	while (cursor < text.size() && (std::isalpha(static_cast<unsigned char>(text[cursor])) || text[cursor] == '_')) {
		cursor++;
	}
	if (cursor == 0) return false;
	keyword.assign(text.substr(0, cursor));
	cursor = SkipWktSpace(std::string(text), cursor);
	if (cursor >= text.size() || text[cursor] != '[' || text.back() != ']') return false;

	std::size_t depth = 0;
	bool quoted = false;
	std::size_t closing = std::string_view::npos;
	for (std::size_t index = cursor; index < text.size(); index++) {
		const auto ch = text[index];
		if (ch == '"') {
			if (quoted && index + 1 < text.size() && text[index + 1] == '"') {
				index++;
				continue;
			}
			quoted = !quoted;
			continue;
		}
		if (quoted) continue;
		if (ch == '[') {
			depth++;
		} else if (ch == ']') {
			if (depth == 0) return false;
			depth--;
			if (depth == 0) {
				closing = index;
				break;
			}
		}
	}
	if (quoted || depth != 0 || closing != text.size() - 1) return false;
	return SplitWktArguments(text.substr(cursor + 1, closing - cursor - 1), arguments);
}

bool ParseWktString(std::string_view text, std::string &value) {
	text = Trim(text);
	if (text.size() < 2 || text.front() != '"' || text.back() != '"') return false;
	value.clear();
	for (std::size_t index = 1; index + 1 < text.size(); index++) {
		if (text[index] == '"') {
			if (index + 1 >= text.size() - 1 || text[index + 1] != '"') return false;
			value.push_back('"');
			index++;
		} else {
			value.push_back(text[index]);
		}
	}
	return true;
}

bool ParseWktNumber(std::string_view text, double &value) {
	text = Trim(text);
	if (text.empty()) return false;
	try {
		std::size_t consumed = 0;
		value = std::stod(std::string(text), &consumed);
		return consumed == text.size() && std::isfinite(value);
	} catch (const std::exception &) {
		return false;
	}
}

bool ParseWktNameAndNumber(const std::string &text, std::string_view expected_name, double expected_value) {
	std::string keyword;
	std::vector<std::string_view> args;
	if (!ParseWktClause(text, keyword, args) || !EqualInsensitive(keyword, "ANGLEUNIT") || args.size() != 2) return false;
	std::string name;
	double value = 0;
	return ParseWktString(args[0], name) && EqualInsensitive(name, expected_name) &&
	       ParseWktNumber(args[1], value) && value == expected_value;
}

bool ParseGaussianWgs84Datum(std::string_view text) {
	std::string datum_keyword;
	std::vector<std::string_view> datum_args;
	if (!ParseWktClause(text, datum_keyword, datum_args) || !EqualInsensitive(datum_keyword, "DATUM") ||
	    datum_args.size() != 2) {
		return false;
	}
	std::string datum_name;
	if (!ParseWktString(datum_args[0], datum_name) || !EqualInsensitive(datum_name, "World Geodetic System 1984")) {
		return false;
	}
	std::string ellipsoid_keyword;
	std::vector<std::string_view> ellipsoid_args;
	if (!ParseWktClause(datum_args[1], ellipsoid_keyword, ellipsoid_args) ||
	    !EqualInsensitive(ellipsoid_keyword, "ELLIPSOID") || ellipsoid_args.size() != 3) {
		return false;
	}
	std::string ellipsoid_name;
	double semi_major = 0;
	double inverse_flattening = 0;
	return ParseWktString(ellipsoid_args[0], ellipsoid_name) && EqualInsensitive(ellipsoid_name, "WGS 84") &&
	       ParseWktNumber(ellipsoid_args[1], semi_major) && semi_major == 6378137.0 &&
	       ParseWktNumber(ellipsoid_args[2], inverse_flattening) && inverse_flattening == 298.257223563;
}

bool ParseGaussianAxis(std::string_view text, std::string_view expected_name, std::string_view expected_direction) {
	std::string keyword;
	std::vector<std::string_view> args;
	if (!ParseWktClause(text, keyword, args) || !EqualInsensitive(keyword, "AXIS") || args.size() != 2) return false;
	std::string name;
	return ParseWktString(args[0], name) && EqualInsensitive(name, expected_name) &&
	       EqualInsensitive(Trim(args[1]), expected_direction);
}

bool ParseGaussianOrderRemark(std::string_view text, std::optional<std::uint64_t> &order) {
	std::string remark;
	if (!ParseWktString(text, remark)) return false;
	const std::string_view value = Trim(remark);
	constexpr std::string_view generic = "Reduced Gaussian Grid";
	if (EqualInsensitive(value, generic)) {
		order.reset();
		return true;
	}
	constexpr std::string_view prefix = "Reduced Gaussian Grid O";
	if (value.size() <= prefix.size() || !EqualInsensitive(value.substr(0, prefix.size()), prefix)) return false;
	std::size_t cursor = prefix.size();
	const auto digit_start = cursor;
	std::uint64_t parsed = 0;
	while (cursor < value.size() && std::isdigit(static_cast<unsigned char>(value[cursor]))) {
		const auto digit = static_cast<std::uint64_t>(value[cursor] - '0');
		if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
		parsed = parsed * 10 + digit;
		cursor++;
	}
	if (cursor == digit_start || parsed == 0 || !EqualInsensitive(Trim(value.substr(cursor)), "(ECMWF)")) return false;
	order = parsed;
	return true;
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

SourceCrsProfile ClassifySourceCrsProfile(const std::string &wkt) {
	if (Trim(wkt).empty()) return {SourceCrsProfileKind::NotSpecified, std::nullopt};
	std::string root_keyword;
	std::vector<std::string_view> root_args;
	if (!ParseWktClause(wkt, root_keyword, root_args) || !EqualInsensitive(root_keyword, "GEOGCRS") ||
	    root_args.size() < 2) {
		return {SourceCrsProfileKind::Unrecognized, std::nullopt};
	}
	std::string root_name;
	if (!ParseWktString(root_args[0], root_name) || !EqualInsensitive(root_name, "Reduced Gaussian Grid")) {
		return {SourceCrsProfileKind::Unrecognized, std::nullopt};
	}

	std::size_t datum_count = 0;
	std::size_t cs_count = 0;
	std::size_t angular_unit_count = 0;
	std::size_t remark_count = 0;
	std::size_t usage_count = 0;
	std::optional<std::uint64_t> gaussian_order;
	std::vector<std::string_view> axes;
	for (std::size_t index = 1; index < root_args.size(); index++) {
		std::string clause_keyword;
		std::vector<std::string_view> clause_args;
		if (!ParseWktClause(root_args[index], clause_keyword, clause_args)) {
			return {SourceCrsProfileKind::Unrecognized, std::nullopt};
		}
		if (EqualInsensitive(clause_keyword, "DATUM")) {
			datum_count++;
			if (!ParseGaussianWgs84Datum(root_args[index])) return {SourceCrsProfileKind::Unrecognized, std::nullopt};
		} else if (EqualInsensitive(clause_keyword, "CS")) {
			cs_count++;
			double dimensions = 0;
			if (clause_args.size() != 2 || !EqualInsensitive(Trim(clause_args[0]), "ellipsoidal") ||
			    !ParseWktNumber(clause_args[1], dimensions) || dimensions != 2.0) {
				return {SourceCrsProfileKind::Unrecognized, std::nullopt};
			}
		} else if (EqualInsensitive(clause_keyword, "AXIS")) {
			axes.emplace_back(root_args[index]);
		} else if (EqualInsensitive(clause_keyword, "ANGLEUNIT")) {
			angular_unit_count++;
			if (!ParseWktNameAndNumber(std::string(root_args[index]), "degree", 0.0174532925199433)) {
				return {SourceCrsProfileKind::Unrecognized, std::nullopt};
			}
		} else if (EqualInsensitive(clause_keyword, "REMARK")) {
			remark_count++;
			if (clause_args.size() != 1 || !ParseGaussianOrderRemark(clause_args[0], gaussian_order)) {
				return {SourceCrsProfileKind::Unrecognized, std::nullopt};
			}
		} else if (EqualInsensitive(clause_keyword, "USAGE")) {
			usage_count++;
			if (clause_args.empty()) return {SourceCrsProfileKind::Unrecognized, std::nullopt};
		} else {
			return {SourceCrsProfileKind::Unrecognized, std::nullopt};
		}
	}
	if (datum_count != 1 || cs_count != 1 || angular_unit_count != 1 || remark_count > 1 || usage_count > 1 ||
	    axes.size() != 2 || !ParseGaussianAxis(axes[0], "latitude", "north") ||
	    !ParseGaussianAxis(axes[1], "longitude", "east")) {
		return {SourceCrsProfileKind::Unrecognized, std::nullopt};
	}
	return {SourceCrsProfileKind::ReducedGaussianWgs84V1, gaussian_order};
}

SourceCrsBindingStatus CheckSourceCrsProfile(const SourceCrsProfile &profile, bool reduced_gaussian,
	                                             bool wgs84_source_earth,
	                                             std::optional<std::uint64_t> declared_gaussian_order) {
	if (profile.kind == SourceCrsProfileKind::NotSpecified) return SourceCrsBindingStatus::NotSpecified;
	if (profile.kind == SourceCrsProfileKind::Unrecognized) return SourceCrsBindingStatus::Unrecognized;
	if (profile.kind == SourceCrsProfileKind::ReducedGaussianWgs84V1 && reduced_gaussian && wgs84_source_earth &&
	    (!profile.gaussian_order || profile.gaussian_order == declared_gaussian_order)) {
		return SourceCrsBindingStatus::Compatible;
	}
	return SourceCrsBindingStatus::ConflictsWithGrid;
}

} // namespace duckomo
} // namespace duckdb
