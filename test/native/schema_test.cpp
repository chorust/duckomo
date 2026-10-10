#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckomo/dimensions.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include "duckomo/local_file.hpp"
#include "duckomo/metadata.hpp"
#include "duckomo/reader.hpp"
#include "duckomo/schema.hpp"
#include "duckomo_extension.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

extern "C" {
#include "om_file.h"
#include "om_variable.h"
}

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<DuckomoExtension>();
}
} // namespace duckdb

namespace {

namespace fs = std::filesystem;
using duckdb::duckomo::BoundSchema;
using duckdb::duckomo::BuildBoundSchema;
using duckdb::duckomo::EncodeMetadataName;
using duckdb::duckomo::LocalFile;
using duckdb::duckomo::MetadataVariable;
using duckdb::duckomo::OmMetadataTree;
using duckdb::duckomo::OmV3Reader;
using duckdb::duckomo::ReadMetadataTree;
using duckdb::duckomo::ReaderError;
using duckdb::duckomo::ReaderErrorCode;
using duckdb::duckomo::ValidateAxisDeclarations;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <class Action>
void RequireReaderError(ReaderErrorCode expected_code, Action action, const std::string &message) {
	try {
		action();
	} catch (const ReaderError &error) {
		Require(error.Code() == expected_code, message + ": unexpected ReaderErrorCode");
		return;
	}
	throw std::runtime_error(message + ": expected ReaderError");
}

template <class Action>
void RequireRejected(Action action, const std::string &message) {
	try {
		action();
	} catch (const std::exception &) {
		return;
	}
	throw std::runtime_error(message + ": malformed OM metadata was accepted");
}

std::string SqlLiteral(const fs::path &path) {
	std::string escaped;
	for (const auto character : path.string()) {
		escaped.push_back(character);
		if (character == '\'') {
			escaped.push_back('\'');
		}
	}
	return "'" + escaped + "'";
}

BoundSchema ReadSchema(duckdb::ClientContext &context, const fs::path &path) {
	OmV3Reader reader(LocalFile::Open(context, path.string()));
	return BuildBoundSchema(ReadMetadataTree(reader));
}

struct FixtureBytes final {
	std::vector<std::uint8_t> bytes;
	std::uint64_t root_offset = 0;
	std::uint64_t root_size = 0;
};

FixtureBytes ReadFixtureBytes(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "cannot open OM fixture: " + path.string());
	input.seekg(0, std::ios::end);
	const auto end = input.tellg();
	Require(end >= 0, "cannot get OM fixture size: " + path.string());
	FixtureBytes result;
	result.bytes.resize(static_cast<std::size_t>(end));
	input.seekg(0, std::ios::beg);
	if (!result.bytes.empty()) {
		input.read(reinterpret_cast<char *>(result.bytes.data()), static_cast<std::streamsize>(result.bytes.size()));
		Require(static_cast<std::size_t>(input.gcount()) == result.bytes.size(), "short read from OM fixture");
	}
	Require(result.bytes.size() >= om_header_write_size() + om_trailer_size(), "OM fixture is too short");
	const auto trailer_offset = result.bytes.size() - om_trailer_size();
	Require(om_trailer_read(result.bytes.data() + trailer_offset, &result.root_offset, &result.root_size),
	        "cannot parse OM fixture trailer");
	Require(result.root_offset <= trailer_offset && result.root_size <= trailer_offset - result.root_offset,
	        "OM fixture root metadata is outside the file");
	return result;
}

struct ChildReference final {
	std::uint64_t offset = 0;
	std::uint64_t size = 0;
};

std::vector<ChildReference> RootChildren(const FixtureBytes &fixture) {
	const auto *root = om_variable_init(fixture.bytes.data() + fixture.root_offset);
	Require(om_variable_validate(root, fixture.root_size) == ERROR_OK, "fixture root metadata is invalid");
	const auto count = om_variable_get_children_count(root);
	std::vector<std::uint64_t> offsets(count);
	std::vector<std::uint64_t> sizes(count);
	Require(count == 0 || om_variable_get_children(root, 0, count, offsets.data(), sizes.data()),
	        "cannot read fixture child references");
	std::vector<ChildReference> result;
	result.reserve(count);
	for (std::uint32_t index = 0; index < count; index++) {
		Require(offsets[index] <= fixture.bytes.size() && sizes[index] <= fixture.bytes.size() - offsets[index],
		        "fixture child metadata is outside the file");
		result.push_back({offsets[index], sizes[index]});
	}
	return result;
}

std::uint64_t FindNamedChild(const FixtureBytes &fixture, const std::string &wanted_name) {
	for (const auto &child : RootChildren(fixture)) {
		const auto *variable = om_variable_init(fixture.bytes.data() + child.offset);
		Require(om_variable_validate(variable, child.size) == ERROR_OK, "fixture child metadata is invalid");
		std::uint16_t name_size = 0;
		const auto *name = om_variable_get_name(variable, &name_size);
		if (name != nullptr && std::string(name, name_size) == wanted_name) {
			return child.offset;
		}
	}
	throw std::runtime_error("could not find named root child '" + wanted_name + "'");
}

void WriteFixture(const fs::path &path, const FixtureBytes &fixture) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	Require(output.good(), "cannot create fixture mutation: " + path.string());
	output.write(reinterpret_cast<const char *>(fixture.bytes.data()),
	              static_cast<std::streamsize>(fixture.bytes.size()));
	Require(output.good(), "cannot write fixture mutation: " + path.string());
}

class TemporaryDirectory final {
public:
	TemporaryDirectory() {
		static std::uint64_t next_id = 0;
		path_ = fs::temp_directory_path() /
		        ("duckomo-schema-" + std::to_string(static_cast<unsigned long long>(getpid())) + "-" +
		         std::to_string(next_id++));
		std::error_code error;
		fs::remove_all(path_, error);
		fs::create_directories(path_);
	}

	~TemporaryDirectory() {
		std::error_code error;
		fs::remove_all(path_, error);
	}

	fs::path File(const std::string &name) const {
		return path_ / name;
	}

private:
	fs::path path_;
};

void CheckExpectedSchema(const BoundSchema &schema, const std::vector<std::string> &expected_paths,
                         const std::string &description) {
	Require(schema.variables.size() == expected_paths.size(), description + ": wrong array count");
	for (std::size_t index = 0; index < expected_paths.size(); index++) {
		Require(schema.variables[index].canonical_path == expected_paths[index],
		        description + ": paths are missing or not sorted by UTF-8 bytes");
		const auto expected_column = expected_paths[index] == "/" ? "value" : expected_paths[index].substr(1);
		Require(schema.variables[index].column_name == expected_column,
		        description + ": unexpected DuckDB column name");
		Require(schema.variables[index].type == duckdb::LogicalType::FLOAT,
		        description + ": array column is not FLOAT");
	}
}

void TestFixtureTraversal(duckdb::ClientContext &context, const fs::path &fixture_directory) {
	const auto multi = ReadSchema(context, fixture_directory / "multi.om");
	CheckExpectedSchema(multi, {"/humidity", "/temperature"}, "multi-level root traversal");
	Require(multi.row_count == 6 && multi.shape == std::vector<std::uint64_t>({2, 3}),
	        "multi.om schema lost shared shape information");

	const auto nested = ReadSchema(context, fixture_directory / "nested.om");
	// Kept aligned with the independently generated fixture manifest: sibling
	// leaves with the same basename remain distinct, and delimiter bytes in one
	// node name are escaped before segments are joined into a canonical path.
	const std::vector<std::string> nested_paths = {"/layer_a/value", "/layer_b/temperature", "/layer_b/value",
	                                               "/temperature", "/weird%2Fname%25/value"};
	CheckExpectedSchema(nested, nested_paths, "nested hierarchy traversal");
	Require(EncodeMetadataName("slash/%") == "slash%2F%25", "metadata path segments must escape '/' and '%' bytes");
	Require(nested.row_count == 6 && nested.shape == std::vector<std::uint64_t>({2, 3}),
	        "nested.om schema lost shared shape information");

	const auto pfor = ReadSchema(context, fixture_directory / "pfor_attributes.om");
	CheckExpectedSchema(pfor, {"/humidity", "/temperature"}, "PFOR arrays with metadata attributes");
	Require(pfor.variables[0].inferred_axes == std::vector<std::string>({"row", "column"}) &&
	            pfor.variables[1].inferred_axes == pfor.variables[0].inferred_axes,
	        "matching coordinates metadata must provide ordered axes");
	Require(ValidateAxisDeclarations(nullptr, pfor).size() == 2,
	        "matching coordinates metadata must allow implicit dimension alignment");
}

void TestInvalidArrayAttributeReference(duckdb::ClientContext &context, const fs::path &pfor_path,
                                        const TemporaryDirectory &temporary) {
	auto fixture = ReadFixtureBytes(pfor_path);
	const auto array_offset = FindNamedChild(fixture, "temperature");
	const auto *metadata = reinterpret_cast<const OmVariableArrayV3_t *>(fixture.bytes.data() + array_offset);
	Require(metadata->children_count > 0, "PFOR fixture must have array attributes");
	const auto reference_offset = array_offset + sizeof(OmVariableArrayV3_t) +
	                              sizeof(std::uint64_t) * metadata->children_count;
	const auto invalid_offset = static_cast<std::uint64_t>(fixture.bytes.size()) + 4096;
	std::memcpy(fixture.bytes.data() + reference_offset, &invalid_offset, sizeof(invalid_offset));
	const auto damaged_path = temporary.File("invalid_array_attribute_reference.om");
	WriteFixture(damaged_path, fixture);
	RequireReaderError(ReaderErrorCode::InvalidMetadata, [&] { (void)ReadSchema(context, damaged_path); },
	                   "array attribute child references must be validated");
}

void TestInvalidNames(duckdb::ClientContext &context, const fs::path &nested_path,
                      const TemporaryDirectory &temporary) {
	RequireReaderError(ReaderErrorCode::InvalidMetadata, [] { EncodeMetadataName(""); }, "empty path segment");
	RequireReaderError(ReaderErrorCode::InvalidMetadata,
	                   [] { EncodeMetadataName(std::string("bad\0name", 8)); }, "NUL in path segment");
	RequireReaderError(ReaderErrorCode::InvalidMetadata,
	                   [] { EncodeMetadataName(std::string("\xFF", 1)); }, "invalid UTF-8 path segment");

	const auto source = ReadFixtureBytes(nested_path);
	const auto layer_a_offset = FindNamedChild(source, "layer_a");
	for (const auto &test_case : {std::string("invalid_utf8"), std::string("nul"), std::string("empty")}) {
		auto mutated = source;
		auto *header = reinterpret_cast<OmVariableV3_t *>(mutated.bytes.data() + layer_a_offset);
		const auto name_offset = layer_a_offset + sizeof(OmVariableV3_t) +
		                         static_cast<std::uint64_t>(header->children_count) * 2 * sizeof(std::uint64_t);
		if (test_case == "invalid_utf8") {
			mutated.bytes[static_cast<std::size_t>(name_offset)] = 0xFF;
		} else if (test_case == "nul") {
			mutated.bytes[static_cast<std::size_t>(name_offset)] = 0;
		} else {
			header->name_size = 0;
		}
		const auto mutated_path = temporary.File(test_case + ".om");
		WriteFixture(mutated_path, mutated);
		RequireReaderError(ReaderErrorCode::InvalidMetadata,
		                   [&] {
			                   OmV3Reader reader(LocalFile::Open(context, mutated_path.string()));
			                   (void)ReadMetadataTree(reader);
		                   },
		                   "metadata traversal must reject " + test_case + " node names");
	}
}

MetadataVariable SyntheticVariable(const std::string &path, std::vector<std::uint64_t> shape,
                                   std::vector<std::uint64_t> chunks) {
	MetadataVariable result;
	result.canonical_path = path;
	result.shape = std::move(shape);
	result.chunk_shape = std::move(chunks);
	result.row_count = duckdb::duckomo::CheckedShapeProduct(result.shape);
	return result;
}

void TestIdentifierCollisionsAndShapeBounds() {
	OmMetadataTree collision;
	collision.arrays.push_back(SyntheticVariable("/Layer", {2}, {1}));
	collision.arrays.push_back(SyntheticVariable("/layer", {2}, {1}));
	RequireReaderError(ReaderErrorCode::InvalidMetadata, [&] { (void)BuildBoundSchema(collision); },
	                   "DuckDB case-insensitive identifier collision");

	OmMetadataTree root_array;
	root_array.arrays.push_back(SyntheticVariable("/", {2, 3}, {1, 2}));
	const auto root_schema = BuildBoundSchema(root_array);
	Require(root_schema.variables.size() == 1 && root_schema.variables[0].column_name == "value",
	        "root array must bind to the stable 'value' identifier");

	OmMetadataTree rank_eight;
	rank_eight.arrays.push_back(SyntheticVariable("/rank8", {1, 1, 1, 1, 1, 1, 1, 1},
	                                               {1, 1, 1, 1, 1, 1, 1, 1}));
	Require(BuildBoundSchema(rank_eight).variables[0].shape.size() == 8, "rank-eight arrays must be schema-valid");

	OmMetadataTree rank_nine;
	MetadataVariable rank_nine_variable;
	rank_nine_variable.canonical_path = "/rank9";
	rank_nine_variable.shape = {1, 1, 1, 1, 1, 1, 1, 1, 1};
	rank_nine_variable.chunk_shape = rank_nine_variable.shape;
	rank_nine_variable.row_count = 1;
	rank_nine.arrays.push_back(rank_nine_variable);
	RequireReaderError(ReaderErrorCode::InvalidMetadata, [&] { (void)BuildBoundSchema(rank_nine); },
	                   "rank greater than eight");
	RequireReaderError(ReaderErrorCode::InvalidShape,
	                   [] { (void)duckdb::duckomo::CheckedShapeProduct({1, 1, 1, 1, 1, 1, 1, 1, 1}); },
	                   "checked row-count helper must reject rank greater than eight");

	OmMetadataTree overflowing;
	MetadataVariable overflow_variable;
	overflow_variable.canonical_path = "/overflow";
	overflow_variable.shape = {static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), 2};
	overflow_variable.chunk_shape = {1, 1};
	overflow_variable.row_count = 1;
	overflowing.arrays.push_back(overflow_variable);
	RequireReaderError(ReaderErrorCode::ShapeOverflow, [&] { (void)BuildBoundSchema(overflowing); },
	                   "shape product above DuckDB's signed row-count limit");
}

void TestSharedAxesAndCoordinateNames() {
	OmMetadataTree tree;
	tree.arrays.push_back(SyntheticVariable("/first", {2, 3}, {1, 2}));
	tree.arrays.push_back(SyntheticVariable("/nested/second", {2, 3}, {1, 2}));
	auto schema = BuildBoundSchema(tree);
	auto shared = duckdb::Value::LIST(duckdb::LogicalType::VARCHAR, {duckdb::Value("lat"), duckdb::Value("lon")});
	auto axes = ValidateAxisDeclarations(&shared, schema);
	Require(axes.size() == 2 && axes[0] == axes[1] && axes[0] == std::vector<std::string>({"lat", "lon"}),
	        "shared list must normalize to a declaration for every array");
	schema.variables[1].inferred_axes = {"lat", "lon"};
	Require(ValidateAxisDeclarations(&shared, schema) == axes, "matching evidence accepts shared axes");
	schema.variables[1].inferred_axes = {"lon", "lat"};
	RequireRejected([&] { (void)ValidateAxisDeclarations(&shared, schema); },
	                "shared declaration must check metadata on later arrays, not only the first");
	schema.variables[1].inferred_axes.clear();
	schema.variables[1].shape = {3, 2};
	RequireRejected([&] { (void)ValidateAxisDeclarations(&shared, schema); },
	                "equal row count does not establish shape/axis alignment");
	schema.variables[1].shape = {1, 2, 3};
	RequireRejected([&] { (void)ValidateAxisDeclarations(&shared, schema); },
	                "shared axes must check rank on every array");

	for (const auto *name : {"lat", "LAT", "lon", "LoN"}) {
		OmMetadataTree collision;
		collision.arrays.push_back(SyntheticVariable(std::string("/") + name, {2, 3}, {1, 2}));
		const auto source = BuildBoundSchema(collision);
		Require(source.variables[0].column_name == name, "ordinary source schema keeps its column names");
		std::vector<duckdb::LogicalType> types;
		duckdb::duckomo::TableFunctionColumnNames names;
		RequireRejected([&] { duckdb::duckomo::AppendSpatialOutputColumns(source, types, names); },
		                "short geographic column conflicts are case-insensitive");
		Require(types.empty() && names.empty(), "name conflict fails before appending either coordinate");
	}
	OmMetadataTree long_names;
	long_names.arrays.push_back(SyntheticVariable("/Latitude", {2, 3}, {1, 2}));
	long_names.arrays.push_back(SyntheticVariable("/Longitude", {2, 3}, {1, 2}));
	std::vector<duckdb::LogicalType> types;
	duckdb::duckomo::TableFunctionColumnNames names;
	duckdb::duckomo::AppendSpatialOutputColumns(BuildBoundSchema(long_names), types, names);
	Require(names.size() == 2 && duckdb::duckomo::IdentifierNameString(names[0]) == "lat" &&
	            duckdb::duckomo::IdentifierNameString(names[1]) == "lon" &&
	            types[0] == duckdb::LogicalType::DOUBLE && types[1] == duckdb::LogicalType::DOUBLE,
	        "only lat/lon are emitted/reserved; long source names remain valid");
}

void TestNegativeFixtures(duckdb::ClientContext &context, const fs::path &fixture_directory) {
	const auto negative = fixture_directory / "negative";
	for (const auto &case_name : {"unsupported_version", "unsupported_type", "unsupported_compression",
	                              "empty_array_zero_axis", "illegal_reference"}) {
		const auto path = negative / (std::string(case_name) + ".om");
		Require(fs::exists(path), "missing generated negative fixture: " + path.string());
		RequireRejected([&] { (void)ReadSchema(context, path); }, std::string("negative fixture ") + case_name);
	}
	const auto duplicate_path = negative / "duplicate_name.om";
	Require(fs::exists(duplicate_path), "missing generated duplicate-name fixture");
	RequireReaderError(ReaderErrorCode::InvalidMetadata, [&] { (void)ReadSchema(context, duplicate_path); },
	                   "duplicate canonical metadata paths");

	const auto mismatch_path = negative / "shape_mismatch.om";
	Require(fs::exists(mismatch_path), "missing generated shape-mismatch fixture");
	OmV3Reader reader(LocalFile::Open(context, mismatch_path.string()));
	auto mismatch_schema = BuildBoundSchema(ReadMetadataTree(reader));
	duckdb::vector<duckdb::Value> keys;
	duckdb::vector<duckdb::Value> axis_lists;
	for (const auto &variable : mismatch_schema.variables) {
		keys.emplace_back(variable.canonical_path);
		axis_lists.emplace_back(duckdb::Value::LIST(
		    duckdb::LogicalType::VARCHAR,
		    {duckdb::Value("row"), duckdb::Value("column")}));
	}
	auto dimensions = duckdb::Value::MAP(duckdb::LogicalType::VARCHAR,
	                                    duckdb::LogicalType::LIST(duckdb::LogicalType::VARCHAR), std::move(keys),
	                                    std::move(axis_lists));
	RequireRejected([&] { (void)ValidateAxisDeclarations(&dimensions, mismatch_schema); },
	                "shape-mismatch arrays must fail dimension alignment validation");
}

void TestMetadataOnlyDescribe(duckdb::Connection &connection, const fs::path &raw_path,
                              const TemporaryDirectory &temporary) {
	auto damaged = ReadFixtureBytes(raw_path);
	const auto header_size = static_cast<std::uint64_t>(om_header_write_size());
	Require(damaged.root_offset > header_size, "raw fixture has no payload/index region to corrupt");
	std::fill(damaged.bytes.begin() + static_cast<std::ptrdiff_t>(header_size),
	          damaged.bytes.begin() + static_cast<std::ptrdiff_t>(damaged.root_offset), 0xFF);
	const auto damaged_path = temporary.File("metadata_only_corrupt_payload.om");
	WriteFixture(damaged_path, damaged);

	auto result = connection.Query("DESCRIBE SELECT * FROM read_om(" + SqlLiteral(damaged_path) + ")");
	Require(result != nullptr && !result->HasError(),
	        "DESCRIBE read_om must bind from metadata without decoding the damaged payload" +
	            (result && result->HasError() ? ": " + result->GetError() : ""));
	Require(result->RowCount() == 1, "root-array DESCRIBE must expose one column");
	Require(result->GetValue(0, 0).GetValue<std::string>() == "value" &&
	            result->GetValue(1, 0).GetValue<std::string>() == "FLOAT",
	        "root-array DESCRIBE must expose value FLOAT");
}

} // namespace

int main() {
	try {
		const fs::path fixture_directory("test/data");
		duckdb::DuckDB database(nullptr);
		duckdb::Connection connection(database);
		TemporaryDirectory temporary;

		TestFixtureTraversal(*connection.context, fixture_directory);
		TestInvalidArrayAttributeReference(*connection.context, fixture_directory / "pfor_attributes.om", temporary);
		TestInvalidNames(*connection.context, fixture_directory / "nested.om", temporary);
		TestIdentifierCollisionsAndShapeBounds();
		TestSharedAxesAndCoordinateNames();
		TestNegativeFixtures(*connection.context, fixture_directory);
		TestMetadataOnlyDescribe(connection, fixture_directory / "raw.om", temporary);
		std::cout << "metadata/schema checks passed\n";
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "metadata/schema checks failed: " << exception.what() << '\n';
		return 1;
	}
}
