#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct JsonValue {
	enum class Kind { Null, Boolean, Number, String, Array, Object } kind = Kind::Null;
	bool boolean = false;
	std::string scalar;
	std::vector<JsonValue> array;
	std::map<std::string, JsonValue> object;

	static JsonValue Bool(bool value) {
		JsonValue result;
		result.kind = Kind::Boolean;
		result.boolean = value;
		return result;
	}
	static JsonValue Number(std::string value) {
		JsonValue result;
		result.kind = Kind::Number;
		result.scalar = std::move(value);
		return result;
	}
	static JsonValue String(std::string value) {
		JsonValue result;
		result.kind = Kind::String;
		result.scalar = std::move(value);
		return result;
	}
	static JsonValue Object() {
		JsonValue result;
		result.kind = Kind::Object;
		return result;
	}
	static JsonValue Array() {
		JsonValue result;
		result.kind = Kind::Array;
		return result;
	}

	const JsonValue &At(const std::string &key) const {
		if (kind != Kind::Object) {
			throw std::runtime_error("expected JSON object while reading '" + key + "'");
		}
		auto entry = object.find(key);
		if (entry == object.end()) {
			throw std::runtime_error("missing required JSON field '" + key + "'");
		}
		return entry->second;
	}
	JsonValue &operator[](const std::string &key) {
		if (kind == Kind::Null) {
			kind = Kind::Object;
		}
		if (kind != Kind::Object) {
			throw std::runtime_error("cannot assign a field to non-object JSON value");
		}
		return object[key];
	}
	std::string AsString(const std::string &field = "value") const {
		if (kind != Kind::String) {
			throw std::runtime_error("JSON field '" + field + "' is not a string");
		}
		return scalar;
	}
	std::uint64_t AsUint64(const std::string &field = "value") const {
		if (kind != Kind::Number || scalar.empty() || scalar[0] == '-') {
			throw std::runtime_error("JSON field '" + field + "' is not a non-negative integer");
		}
		std::size_t used = 0;
		const auto result = std::stoull(scalar, &used);
		if (used != scalar.size()) {
			throw std::runtime_error("JSON field '" + field + "' is not an integer");
		}
		return result;
	}
	bool AsBool(const std::string &field = "value") const {
		if (kind != Kind::Boolean) {
			throw std::runtime_error("JSON field '" + field + "' is not a boolean");
		}
		return boolean;
	}
	std::string Serialize() const;
};

std::string JsonEscape(const std::string &value) {
	static constexpr char HEX[] = "0123456789abcdef";
	std::string output = "\"";
	for (unsigned char character : value) {
		switch (character) {
		case '"': output += "\\\""; break;
		case '\\': output += "\\\\"; break;
		case '\b': output += "\\b"; break;
		case '\f': output += "\\f"; break;
		case '\n': output += "\\n"; break;
		case '\r': output += "\\r"; break;
		case '\t': output += "\\t"; break;
		default:
			if (character < 0x20) {
				output += "\\u00";
				output.push_back(HEX[character >> 4]);
				output.push_back(HEX[character & 0x0f]);
			} else {
				output.push_back(static_cast<char>(character));
			}
		}
	}
	output.push_back('"');
	return output;
}

std::string JsonValue::Serialize() const {
	switch (kind) {
	case Kind::Null: return "null";
	case Kind::Boolean: return boolean ? "true" : "false";
	case Kind::Number: return scalar;
	case Kind::String: return JsonEscape(scalar);
	case Kind::Array: {
		std::string result = "[";
		for (std::size_t i = 0; i < array.size(); ++i) {
			if (i) result.push_back(',');
			result += array[i].Serialize();
		}
		result.push_back(']');
		return result;
	}
	case Kind::Object: {
		std::string result = "{";
		bool first = true;
		for (const auto &entry : object) {
			if (!first) result.push_back(',');
			first = false;
			result += JsonEscape(entry.first) + ":" + entry.second.Serialize();
		}
		result.push_back('}');
		return result;
	}
	}
	throw std::runtime_error("unknown JSON value kind");
}

class JsonParser {
public:
	explicit JsonParser(const std::string &source_p) : source(source_p) {}
	JsonValue Parse() {
		SkipSpace();
		auto result = ParseValue();
		SkipSpace();
		if (position != source.size()) Fail("trailing characters");
		return result;
	}

private:
	const std::string &source;
	std::size_t position = 0;

	[[noreturn]] void Fail(const std::string &message) const {
		throw std::runtime_error("invalid JSON at byte " + std::to_string(position) + ": " + message);
	}
	void SkipSpace() {
		while (position < source.size() && (source[position] == ' ' || source[position] == '\n' ||
		       source[position] == '\r' || source[position] == '\t')) position++;
	}
	bool Consume(char expected) {
		if (position < source.size() && source[position] == expected) {
			position++;
			return true;
		}
		return false;
	}
	static void AppendUtf8(std::string &out, std::uint32_t codepoint) {
		if (codepoint <= 0x7f) out.push_back(static_cast<char>(codepoint));
		else if (codepoint <= 0x7ff) {
			out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		} else if (codepoint <= 0xffff) {
			out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		} else {
			out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		}
	}
	std::uint32_t Hex4() {
		if (position + 4 > source.size()) Fail("short unicode escape");
		std::uint32_t result = 0;
		for (unsigned int i = 0; i < 4; ++i) {
			const char c = source[position++];
			result <<= 4;
			if (c >= '0' && c <= '9') result |= static_cast<std::uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f') result |= static_cast<std::uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') result |= static_cast<std::uint32_t>(c - 'A' + 10);
			else Fail("invalid unicode escape");
		}
		return result;
	}
	std::string ParseString() {
		if (!Consume('"')) Fail("expected string");
		std::string result;
		while (position < source.size()) {
			const auto c = static_cast<unsigned char>(source[position++]);
			if (c == '"') return result;
			if (c < 0x20) Fail("unescaped control character");
			if (c != '\\') {
				result.push_back(static_cast<char>(c));
				continue;
			}
			if (position == source.size()) Fail("short escape sequence");
			switch (source[position++]) {
			case '"': result.push_back('"'); break;
			case '\\': result.push_back('\\'); break;
			case '/': result.push_back('/'); break;
			case 'b': result.push_back('\b'); break;
			case 'f': result.push_back('\f'); break;
			case 'n': result.push_back('\n'); break;
			case 'r': result.push_back('\r'); break;
			case 't': result.push_back('\t'); break;
			case 'u': {
				auto codepoint = Hex4();
				if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
					if (position + 2 > source.size() || source[position] != '\\' || source[position + 1] != 'u')
						Fail("unpaired high surrogate");
					position += 2;
					const auto low = Hex4();
					if (low < 0xdc00 || low > 0xdfff) Fail("invalid low surrogate");
					codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
				} else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
					Fail("unpaired low surrogate");
				}
				AppendUtf8(result, codepoint);
				break;
			}
			default: Fail("unknown escape sequence");
			}
		}
		Fail("unterminated string");
	}
	JsonValue ParseValue() {
		SkipSpace();
		if (position == source.size()) Fail("expected value");
		if (source[position] == '"') return JsonValue::String(ParseString());
		if (Consume('{')) {
			JsonValue result = JsonValue::Object();
			SkipSpace();
			if (Consume('}')) return result;
			for (;;) {
				SkipSpace();
				auto key = ParseString();
				SkipSpace();
				if (!Consume(':')) Fail("expected ':'");
				auto value = ParseValue();
				if (!result.object.emplace(std::move(key), std::move(value)).second) Fail("duplicate object key");
				SkipSpace();
				if (Consume('}')) return result;
				if (!Consume(',')) Fail("expected ',' or '}'");
			}
		}
		if (Consume('[')) {
			JsonValue result = JsonValue::Array();
			SkipSpace();
			if (Consume(']')) return result;
			for (;;) {
				result.array.emplace_back(ParseValue());
				SkipSpace();
				if (Consume(']')) return result;
				if (!Consume(',')) Fail("expected ',' or ']'");
			}
		}
		if (source.compare(position, 4, "null") == 0) {
			position += 4;
			return JsonValue {};
		}
		if (source.compare(position, 4, "true") == 0) {
			position += 4;
			return JsonValue::Bool(true);
		}
		if (source.compare(position, 5, "false") == 0) {
			position += 5;
			return JsonValue::Bool(false);
		}
		const auto start = position;
		Consume('-');
		if (Consume('0')) {
		} else {
			if (position == source.size() || source[position] < '1' || source[position] > '9') Fail("invalid number");
			while (position < source.size() && source[position] >= '0' && source[position] <= '9') position++;
		}
		if (Consume('.')) {
			if (position == source.size() || source[position] < '0' || source[position] > '9') Fail("invalid fraction");
			while (position < source.size() && source[position] >= '0' && source[position] <= '9') position++;
		}
		if (Consume('e') || Consume('E')) {
			if (!Consume('+')) Consume('-');
			if (position == source.size() || source[position] < '0' || source[position] > '9') Fail("invalid exponent");
			while (position < source.size() && source[position] >= '0' && source[position] <= '9') position++;
		}
		if (position == start) Fail("unexpected character");
		return JsonValue::Number(source.substr(start, position - start));
	}
};

std::string ReadText(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);
	if (!input) throw std::runtime_error("cannot open " + path.string());
	std::ostringstream buffer;
	buffer << input.rdbuf();
	if (!input.good() && !input.eof()) throw std::runtime_error("failed reading " + path.string());
	return buffer.str();
}

void WriteText(const fs::path &path, const std::string &text) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) throw std::runtime_error("cannot write " + path.string());
	output << text << '\n';
	if (!output) throw std::runtime_error("failed writing " + path.string());
}

std::string Sha256(const fs::path &path) {
	static constexpr std::array<std::uint32_t, 64> K = {
		0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
		0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
		0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
		0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
		0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,
		0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,
		0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,
		0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
		0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
	};
	std::array<std::uint32_t, 8> state = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
	                                      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
	std::ifstream input(path, std::ios::binary);
	if (!input) throw std::runtime_error("cannot open for SHA-256: " + path.string());
	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8;
	bytes.push_back(0x80);
	while ((bytes.size() % 64) != 56) bytes.push_back(0);
	for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<unsigned char>(bit_length >> shift));
	auto rotate_right = [](std::uint32_t value, unsigned int shift) { return (value >> shift) | (value << (32 - shift)); };
	for (std::size_t block = 0; block < bytes.size(); block += 64) {
		std::array<std::uint32_t, 64> words {};
		for (unsigned int i = 0; i < 16; ++i) {
			const auto offset = block + i * 4;
			words[i] = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
			           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
			           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
		}
		for (unsigned int i = 16; i < 64; ++i) {
			const auto s0 = rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
			const auto s1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
			words[i] = words[i - 16] + s0 + words[i - 7] + s1;
		}
		auto a = state[0]; auto b = state[1]; auto c = state[2]; auto d = state[3];
		auto e = state[4]; auto f = state[5]; auto g = state[6]; auto h = state[7];
		for (unsigned int i = 0; i < 64; ++i) {
			const auto s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
			const auto ch = (e & f) ^ (~e & g);
			const auto t1 = h + s1 + ch + K[i] + words[i];
			const auto s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
			const auto maj = (a & b) ^ (a & c) ^ (b & c);
			const auto t2 = s0 + maj;
			h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		state[0] += a; state[1] += b; state[2] += c; state[3] += d;
		state[4] += e; state[5] += f; state[6] += g; state[7] += h;
	}
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (const auto word : state) output << std::setw(8) << word;
	return output.str();
}

struct CommandResult {
	int exit_code = -1;
	std::string output;
	std::uint64_t peak_rss_bytes = 0;
	double elapsed_ms = 0;
};

CommandResult RunProcess(const std::vector<std::string> &arguments, const fs::path &working_directory,
                         const std::map<std::string, std::string> &environment = {}) {
	int output_pipe[2];
	if (pipe(output_pipe) != 0) throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));
	const auto started = std::chrono::steady_clock::now();
	const pid_t child = fork();
	if (child < 0) {
		close(output_pipe[0]); close(output_pipe[1]);
		throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
	}
	if (child == 0) {
		close(output_pipe[0]);
		if (dup2(output_pipe[1], STDOUT_FILENO) < 0 || dup2(output_pipe[1], STDERR_FILENO) < 0) _exit(126);
		close(output_pipe[1]);
		if (chdir(working_directory.c_str()) != 0) _exit(126);
		for (const auto &entry : environment) {
			if (setenv(entry.first.c_str(), entry.second.c_str(), 1) != 0) _exit(126);
		}
		std::vector<char *> argv;
		argv.reserve(arguments.size() + 1);
		for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
		argv.push_back(nullptr);
		execvp(arguments[0].c_str(), argv.data());
		const std::string error = "exec failed: " + std::string(std::strerror(errno)) + "\n";
		const auto error_written = write(STDERR_FILENO, error.data(), error.size());
		(void)error_written;
		_exit(127);
	}
	close(output_pipe[1]);
	CommandResult result;
	std::array<char, 16384> buffer {};
	for (;;) {
		const auto count = read(output_pipe[0], buffer.data(), buffer.size());
		if (count > 0) {
			result.output.append(buffer.data(), static_cast<std::size_t>(count));
			continue;
		}
		if (count == 0) break;
		if (errno == EINTR) continue;
		close(output_pipe[0]);
		throw std::runtime_error("read from child failed: " + std::string(std::strerror(errno)));
	}
	close(output_pipe[0]);
	int status = 0;
	struct rusage usage {};
	pid_t waited;
	do {
		waited = wait4(child, &status, 0, &usage);
	} while (waited < 0 && errno == EINTR);
	if (waited < 0) throw std::runtime_error("wait4 failed: " + std::string(std::strerror(errno)));
	const auto finished = std::chrono::steady_clock::now();
	result.elapsed_ms = std::chrono::duration<double, std::milli>(finished - started).count();
	if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
	else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
#if defined(__APPLE__)
	result.peak_rss_bytes = static_cast<std::uint64_t>(usage.ru_maxrss);
#else
	result.peak_rss_bytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
	return result;
}

std::string RunGit(const fs::path &root, const fs::path &directory) {
	const auto result = RunProcess({"git", "-C", (root / directory).string(), "rev-parse", "HEAD"}, root);
	if (result.exit_code != 0) throw std::runtime_error("git rev-parse failed for " + directory.string() + ": " + result.output);
	auto hash = result.output;
	while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r' || hash.back() == ' ' || hash.back() == '\t')) hash.pop_back();
	if (hash.size() != 40) throw std::runtime_error("unexpected git commit id for " + directory.string());
	return hash;
}

struct Options {
	fs::path root;
	fs::path fixtures;
	fs::path output;
	fs::path duckdb;
	fs::path extension;
};

Options ParseOptions(int argc, char **argv) {
	fs::path executable = fs::weakly_canonical(fs::absolute(argv[0]));
	Options options;
	options.root = executable.parent_path().parent_path().parent_path().parent_path().parent_path();
	options.fixtures = options.root / "test/data";
	options.output = options.root / "build/evidence";
	options.duckdb = options.root / "build/release/duckdb";
	options.extension = options.root / "build/release/extension/duckomo/duckomo.duckdb_extension";
	for (int i = 1; i < argc; ++i) {
		const std::string argument = argv[i];
		if (argument == "--help" || argument == "-h") {
			std::cout << "Usage: duckomo_validation [--root PATH] [--fixtures PATH] [--output PATH] [--duckdb PATH] [--extension PATH]\n";
			std::exit(0);
		}
		if (i + 1 >= argc) throw std::runtime_error("missing value after " + argument);
		const fs::path value = argv[++i];
		if (argument == "--root") options.root = value;
		else if (argument == "--fixtures") options.fixtures = value;
		else if (argument == "--output") options.output = value;
		else if (argument == "--duckdb") options.duckdb = value;
		else if (argument == "--extension") options.extension = value;
		else throw std::runtime_error("unknown option " + argument);
	}
	options.root = fs::weakly_canonical(fs::absolute(options.root));
	auto resolve = [&](fs::path &path) {
		if (path.is_relative()) path = fs::current_path() / path;
		path = fs::weakly_canonical(path);
	};
	resolve(options.fixtures);
	resolve(options.output);
	resolve(options.duckdb);
	resolve(options.extension);
	return options;
}

std::vector<std::string> SplitCsvLine(const std::string &line) {
	std::vector<std::string> fields;
	std::string field;
	bool quoted = false;
	for (std::size_t i = 0; i < line.size(); ++i) {
		const char c = line[i];
		if (quoted) {
			if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
				field.push_back('"'); ++i;
			} else if (c == '"') quoted = false;
			else field.push_back(c);
		} else if (c == '"' && field.empty()) quoted = true;
		else if (c == ',') { fields.emplace_back(std::move(field)); field.clear(); }
		else field.push_back(c);
	}
	if (quoted) throw std::runtime_error("unterminated CSV quote");
	fields.emplace_back(std::move(field));
	return fields;
}

double ParseDouble(const std::string &text, const std::string &context) {
	char *end = nullptr;
	errno = 0;
	const auto value = std::strtod(text.c_str(), &end);
	if (errno || end == text.c_str() || *end != '\0' || !std::isfinite(value))
		throw std::runtime_error("invalid finite number '" + text + "' in " + context);
	return value;
}

using ReferenceRows = std::map<std::string, std::map<std::uint64_t, double>>;

void ReadReferenceCsv(const fs::path &fixtures, const std::string &filename, const std::string &variable,
                      ReferenceRows &references) {
	const auto content = ReadText(fixtures / filename);
	std::istringstream lines(content);
	std::string line;
	if (!std::getline(lines, line) || line != "index,value") throw std::runtime_error("unexpected reference CSV header for " + filename);
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		const auto fields = SplitCsvLine(line);
		if (fields.size() != 2) throw std::runtime_error("malformed reference CSV row for " + filename);
		std::size_t used = 0;
		const auto index = std::stoull(fields[0], &used);
		if (used != fields[0].size()) throw std::runtime_error("invalid row index in reference CSV " + filename);
		if (index != references[variable].size())
			throw std::runtime_error("reference CSV row indexes are not contiguous from zero: " + filename);
		if (!references[variable].emplace(index, ParseDouble(fields[1], filename)).second)
			throw std::runtime_error("duplicate row index in reference CSV " + filename);
	}
}

ReferenceRows ReadReferences(const JsonValue &manifest, const JsonValue &scenario, const fs::path &fixtures) {
	ReferenceRows references;
	if (scenario.object.count("expected_values_reference_csvs")) {
		for (const auto &entry : scenario.At("expected_values_reference_csvs").object)
			ReadReferenceCsv(fixtures, entry.second.AsString(), entry.first, references);
	} else if (scenario.object.count("expected_values_reference_csv")) {
		ReadReferenceCsv(fixtures, scenario.At("expected_values_reference_csv").AsString(), "/temperature", references);
	} else if (scenario.At("scenario_id").AsString() == "output_plus_filter") {
		bool found = false;
		for (const auto &fixture : manifest.At("fixtures").array) {
			if (fixture.At("fixture_id").AsString() != "projection") continue;
			for (const auto &variable : fixture.At("variables").array) {
				if (variable.At("variable_path").AsString() != "/temperature") continue;
				ReadReferenceCsv(fixtures, variable.At("reference_csv").AsString(), "/temperature", references);
				found = true;
			}
		}
		if (!found) throw std::runtime_error("projection temperature reference missing from fixture manifest");
	}
	return references;
}

std::vector<std::vector<std::optional<double>>> ParseQueryRows(const std::string &output, std::size_t columns) {
	std::vector<std::vector<std::optional<double>>> rows;
	std::istringstream lines(output);
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		const auto fields = SplitCsvLine(line);
		if (fields.size() != columns) {
			throw std::runtime_error("child produced " + std::to_string(fields.size()) + " CSV columns; expected " +
			                         std::to_string(columns) + "; output begins: " + output.substr(0, 300));
		}
		std::vector<std::optional<double>> row;
		for (const auto &field : fields) {
			if (field == "NULL") row.emplace_back(std::nullopt);
			else row.emplace_back(ParseDouble(field, "DuckDB query output"));
		}
		rows.emplace_back(std::move(row));
	}
	return rows;
}

bool EqualNumber(double actual, double expected) {
	// projection.om uses quarter-step Float32 values, which DuckDB's six-decimal
	// CLI CSV output preserves exactly. This implements the manifest's 0 tolerance.
	return actual == expected;
}

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

struct ComparisonResult {
	bool passed = false;
	std::uint64_t expected_rows = 0;
	std::uint64_t actual_rows = 0;
	std::string detail;
};

ComparisonResult CompareScenario(const std::string &scenario_id, const JsonValue &scenario,
	                                const ReferenceRows &references,
	                                const std::vector<std::vector<std::optional<double>>> &actual_rows) {
	ComparisonResult comparison;
	comparison.actual_rows = actual_rows.size();
	if (scenario_id == "full_scan") {
		const auto &columns = scenario.At("output_variables").array;
		Require(columns.size() == 3, "full_scan must compare all three fixture variables");
		Require(references.size() == 3, "full_scan must have three independent reference CSVs");
		const auto row_count = references.begin()->second.size();
		for (const auto &reference : references) Require(reference.second.size() == row_count, "reference row counts differ");
		comparison.expected_rows = row_count;
		Require(actual_rows.size() == row_count, "full_scan row count differs from references");
		std::vector<std::vector<double>> expected;
		expected.reserve(row_count);
		for (std::uint64_t index = 0; index < row_count; ++index) {
			std::vector<double> row;
			for (const auto &column : columns) {
				const auto key = column.AsString();
				auto ref = references.find(key);
				Require(ref != references.end(), "full_scan missing reference for " + key);
				auto value = ref->second.find(index);
				Require(value != ref->second.end(), "full_scan missing reference row " + std::to_string(index));
				row.push_back(value->second);
			}
			expected.emplace_back(std::move(row));
		}
		const auto order_column = std::find_if(columns.begin(), columns.end(), [](const JsonValue &value) {
			return value.AsString() == "/temperature";
		});
		Require(order_column != columns.end(), "full_scan ORDER BY column missing from output");
		const auto order_index = static_cast<std::size_t>(std::distance(columns.begin(), order_column));
		std::stable_sort(expected.begin(), expected.end(), [&](const auto &a, const auto &b) { return a[order_index] < b[order_index]; });
		for (std::size_t row = 0; row < expected.size(); ++row) {
			Require(actual_rows[row].size() == expected[row].size(), "full_scan output width mismatch");
			for (std::size_t column = 0; column < expected[row].size(); ++column) {
				Require(actual_rows[row][column].has_value(), "full_scan unexpectedly returned NULL");
				Require(EqualNumber(*actual_rows[row][column], expected[row][column]),
				        "full_scan value mismatch at sorted row " + std::to_string(row) + ", column " + std::to_string(column));
			}
		}
		comparison.detail = "all three variables matched independent reference CSVs in SQL sort order";
	} else if (scenario_id == "single_variable") {
		const auto reference = references.find("/temperature");
		Require(reference != references.end(), "single_variable temperature reference missing");
		comparison.expected_rows = reference->second.size();
		Require(actual_rows.size() == comparison.expected_rows, "single_variable row count differs from reference");
		std::vector<double> expected;
		for (const auto &entry : reference->second) expected.push_back(entry.second);
		std::sort(expected.begin(), expected.end());
		for (std::size_t row = 0; row < expected.size(); ++row) {
			Require(actual_rows[row].size() == 1 && actual_rows[row][0].has_value(), "single_variable must return one non-NULL column");
			Require(EqualNumber(*actual_rows[row][0], expected[row]), "single_variable value mismatch at row " + std::to_string(row));
		}
		comparison.detail = "temperature matched its independent reference CSV";
	} else if (scenario_id == "output_plus_filter") {
		const auto reference = references.find("/temperature");
		Require(reference != references.end(), "output_plus_filter temperature reference missing");
		const auto &positions = scenario.At("expected_row_positions").array;
		const auto &manifest_values = scenario.At("expected_output_values").array;
		Require(positions.size() == manifest_values.size(), "filter expected positions/value lengths differ");
		comparison.expected_rows = positions.size();
		Require(actual_rows.size() == positions.size(), "output_plus_filter row count differs from manifest");
		std::vector<double> expected;
		for (std::size_t i = 0; i < positions.size(); ++i) {
			const auto row_index = positions[i].AsUint64("expected_row_positions");
			const auto ref = reference->second.find(row_index);
			Require(ref != reference->second.end(), "filter position missing from reference CSV");
			const auto manifest_value = ParseDouble(manifest_values[i].kind == JsonValue::Kind::Number ?
			                                        manifest_values[i].scalar : manifest_values[i].AsString(),
			                                        "manifest filter output value");
			Require(EqualNumber(ref->second, manifest_value), "manifest filter output differs from independent reference CSV");
			expected.push_back(ref->second);
		}
		std::sort(expected.begin(), expected.end());
		for (std::size_t row = 0; row < expected.size(); ++row) {
			Require(actual_rows[row].size() == 1 && actual_rows[row][0].has_value(), "filter query must return one non-NULL column");
			Require(EqualNumber(*actual_rows[row][0], expected[row]), "output_plus_filter value mismatch at row " + std::to_string(row));
		}
		comparison.detail = "humidity filter result matched positions and values from the independent temperature reference";
	} else if (scenario_id == "count") {
		comparison.expected_rows = 1;
		Require(actual_rows.size() == 1 && actual_rows[0].size() == 1 && actual_rows[0][0].has_value(),
		        "count query must return one scalar row");
		const auto expected = scenario.At("count_value").AsUint64("count_value");
		Require(EqualNumber(*actual_rows[0][0], static_cast<double>(expected)), "count value differs from manifest row count");
		comparison.detail = "COUNT(*) equals the manifest logical row count";
	} else {
		throw std::runtime_error("unknown required projection scenario " + scenario_id);
	}
	comparison.passed = true;
	return comparison;
}

const JsonValue *FindVariableMetrics(const JsonValue &metrics, const std::string &path) {
	const auto variables = metrics.object.find("variables");
	if (variables == metrics.object.end() || variables->second.kind != JsonValue::Kind::Object) return nullptr;
	const auto variable = variables->second.object.find(path);
	return variable == variables->second.object.end() ? nullptr : &variable->second;
}

std::uint64_t MetricUint(const JsonValue *metrics, const std::string &key) {
	return metrics ? metrics->At(key).AsUint64(key) : 0;
}

std::uint64_t SumMetric(const JsonValue &metrics, const std::string &key) {
	std::uint64_t result = 0;
	for (const auto &entry : metrics.At("variables").object) {
		const auto value = entry.second.At(key).AsUint64(key);
		if (result > UINT64_MAX - value) throw std::runtime_error("metric sum overflow: " + key);
		result += value;
	}
	return result;
}

void VerifyMetricTotals(const JsonValue &metrics) {
	const auto expected_bytes = metrics.At("metadata_bytes").AsUint64("metadata_bytes") +
	                            SumMetric(metrics, "index_bytes") + SumMetric(metrics, "data_bytes");
	const auto expected_requests = metrics.At("metadata_requests").AsUint64("metadata_requests") +
	                               SumMetric(metrics, "index_requests") + SumMetric(metrics, "data_requests");
	Require(metrics.At("bytes_fetched").AsUint64("bytes_fetched") == expected_bytes,
	        "read metrics bytes_fetched does not reconcile with metadata/index/data classes");
	Require(metrics.At("read_requests").AsUint64("read_requests") == expected_requests,
	        "read metrics read_requests does not reconcile with metadata/index/data classes");
	Require(metrics.At("decode_count_complete").AsBool("decode_count_complete"),
	        "successful scenario has incomplete decode metrics");
	for (const auto &entry : metrics.At("variables").object) {
		Require(entry.second.At("decode_count_complete").AsBool("decode_count_complete"),
		        "successful scenario has incomplete decode metrics for " + entry.first);
	}
}

void VerifyExpectedMetrics(const std::string &scenario_id, const JsonValue &metrics) {
	const std::vector<std::string> variables = {"/humidity", "/pressure", "/temperature"};
	for (const auto &path : variables) {
		const auto *entry = FindVariableMetrics(metrics, path);
		const auto data_bytes = MetricUint(entry, "data_bytes");
		const auto decoded = MetricUint(entry, "decoded_chunks");
		if (scenario_id == "full_scan") {
			Require(data_bytes > 0 && decoded > 0, "full_scan did not read and decode " + path);
		} else if (scenario_id == "single_variable") {
			if (path == "/temperature") Require(data_bytes > 0 && decoded > 0, "single_variable did not read/decode temperature");
			else Require(data_bytes == 0 && decoded == 0, "single_variable read or decoded unused variable " + path);
		} else if (scenario_id == "output_plus_filter") {
			if (path == "/temperature" || path == "/humidity")
				Require(data_bytes > 0 && decoded > 0, "filter scenario did not read/decode required variable " + path);
			else Require(data_bytes == 0 && decoded == 0, "filter scenario read or decoded unused pressure variable");
		} else if (scenario_id == "count") {
			Require(data_bytes == 0 && decoded == 0, "count-only scenario read or decoded data for " + path);
		}
		if (scenario_id == "count") {
			Require(MetricUint(entry, "index_bytes") == 0 && MetricUint(entry, "index_requests") == 0 &&
			        MetricUint(entry, "data_requests") == 0,
			        "count-only scenario performed index/data reads for " + path);
		}
	}
}

std::string SqlLiteral(const std::string &value) {
	std::string result = "'";
	for (char c : value) {
		if (c == '\'') result.push_back('\'');
		result.push_back(c);
	}
	result.push_back('\'');
	return result;
}

std::string ActualScenarioSql(const std::string &manifest_sql, const fs::path &fixtures, const fs::path &root) {
	const auto fixture = (fixtures / "projection.om").lexically_normal();
	std::error_code error;
	auto relative = fs::relative(fixture, root, error);
	if (error) throw std::runtime_error("cannot form fixture path relative to repo root: " + error.message());
	const std::string replacement = relative.generic_string();
	std::string sql = manifest_sql;
	const std::string manifest_path = "test/data/projection.om";
	const auto at = sql.find(manifest_path);
	if (at == std::string::npos) throw std::runtime_error("manifest scenario SQL does not reference projection fixture as expected");
	sql.replace(at, manifest_path.size(), replacement);
	return sql;
}

JsonValue StringMap(const std::map<std::string, std::string> &values) {
	auto result = JsonValue::Object();
	for (const auto &entry : values) result[entry.first] = JsonValue::String(entry.second);
	return result;
}

JsonValue StringArray(const std::vector<std::string> &values) {
	auto result = JsonValue::Array();
	for (const auto &value : values) result.array.emplace_back(JsonValue::String(value));
	return result;
}

std::string DoubleText(double value) {
	std::ostringstream output;
	output << std::setprecision(17) << value;
	return output.str();
}

struct ScenarioEvidence {
	std::string id;
	fs::path json_path;
	JsonValue evidence;
	std::uint64_t data_bytes = 0;
	CommandResult child;
};

ScenarioEvidence RunScenario(const Options &options, const JsonValue &manifest_scenario,
                            const JsonValue &manifest, const std::map<std::string, std::string> &dependencies,
	                            const std::string &fixture_sha) {
	ScenarioEvidence result;
	result.id = manifest_scenario.At("scenario_id").AsString("scenario_id");
	const auto expected_sql = manifest_scenario.At("sql").AsString("sql");
	const auto sql = ActualScenarioSql(expected_sql, options.fixtures, options.root);
	const auto sidecar = options.output / (result.id + ".metrics.json");
	std::error_code ignored;
	fs::remove(sidecar, ignored);
	const std::string execution_sql = "SET threads=1; LOAD " + SqlLiteral(options.extension.string()) + "; " + sql;
	const std::vector<std::string> command = {options.duckdb.string(), "-unsigned", "-bail", "-no-stdin", "-csv", "-noheader",
	                                          "-nullvalue", "NULL", "-c", execution_sql, ":memory:"};
	const std::map<std::string, std::string> environment = {
	    {"DUCKOMO_METRICS_OUTPUT", sidecar.string()}, {"DUCKOMO_SCENARIO", result.id},
	    {"DUCKOMO_FIXTURE_ID", "projection"}, {"DUCKOMO_FIXTURE_SHA256", fixture_sha},
	    {"DUCKOMO_QUERY_ID", "projection_" + result.id}};
	result.child = RunProcess(command, options.root, environment);
	if (result.child.exit_code != 0) {
		throw std::runtime_error("scenario " + result.id + " child exited " + std::to_string(result.child.exit_code) + ": " +
		                         result.child.output.substr(0, 2000));
	}
	Require(fs::exists(sidecar), "scenario " + result.id + " did not write DUCKOMO_METRICS_OUTPUT sidecar");
	auto metrics = JsonParser(ReadText(sidecar)).Parse();
	Require(metrics.kind == JsonValue::Kind::Object, "scenario metrics sidecar must be a JSON object");
	Require(metrics.At("schema_version").AsUint64("schema_version") == 1, "unsupported metrics schema version");
	Require(metrics.At("status").AsString("status") == "success", "successful child did not mark scan status success");
	Require(metrics.At("scenario").AsString("scenario") == result.id, "metrics sidecar scenario does not match requested scenario");
	Require(metrics.At("fixture_id").AsString("fixture_id") == "projection", "metrics sidecar fixture id mismatch");
	Require(metrics.At("fixture_sha256").AsString("fixture_sha256") == fixture_sha, "metrics sidecar fixture hash mismatch");
	VerifyMetricTotals(metrics);
	VerifyExpectedMetrics(result.id, metrics);
	const std::size_t output_columns = result.id == "full_scan" ? 3 : 1;
	auto rows = ParseQueryRows(result.child.output, output_columns);
	auto references = ReadReferences(manifest, manifest_scenario, options.fixtures);
	const auto comparison = CompareScenario(result.id, manifest_scenario, references, rows);
	Require(comparison.passed, "scenario comparison failed");
	if (result.id == "count") {
		const auto expected_count = manifest_scenario.At("count_value").AsUint64("count_value");
		Require(rows.at(0).at(0).has_value() && static_cast<std::uint64_t>(*rows.at(0).at(0)) == expected_count,
		        "COUNT(*) output does not equal the expected fixture row count");
	}
	const auto declared_rows = manifest_scenario.At("result_row_count").AsUint64("result_row_count");
	Require(comparison.expected_rows == declared_rows, "scenario manifest result_row_count disagrees with independent references");
	result.data_bytes = SumMetric(metrics, "data_bytes");
	result.json_path = options.output / (result.id + ".json");
	result.evidence = std::move(metrics);
	result.evidence["query_id"] = JsonValue::String("projection_" + result.id);
	result.evidence["scenario"] = JsonValue::String(result.id);
	result.evidence["sql"] = JsonValue::String(sql);
	result.evidence["fixture_id"] = JsonValue::String("projection");
	result.evidence["fixture_sha256"] = JsonValue::String(fixture_sha);
	result.evidence["dependency_commits"] = StringMap(dependencies);
	result.evidence["result_rows"] = JsonValue::Number(std::to_string(comparison.actual_rows));
	result.evidence["comparison_passed"] = JsonValue::Bool(true);
	result.evidence["elapsed_ms"] = JsonValue::Number(DoubleText(result.child.elapsed_ms));
	result.evidence["peak_rss_bytes"] = JsonValue::Number(std::to_string(result.child.peak_rss_bytes));
	result.evidence["child_exit_code"] = JsonValue::Number(std::to_string(result.child.exit_code));
	result.evidence["command"] = StringArray(command);
	result.evidence["comparison"] = JsonValue::String(comparison.detail);
	auto platform = JsonValue::Object();
	struct utsname host {};
	if (uname(&host) == 0) {
		platform["system"] = JsonValue::String(host.sysname);
		platform["release"] = JsonValue::String(host.release);
		platform["machine"] = JsonValue::String(host.machine);
	}
	platform["build"] = JsonValue::String("release");
	platform["duckdb_cli"] = JsonValue::String(options.duckdb.string());
	platform["extension"] = JsonValue::String(options.extension.string());
	platform["threads"] = JsonValue::String("1");
	result.evidence["environment"] = std::move(platform);
	auto cache_policy = JsonValue::Object();
	cache_policy["application_cache"] = JsonValue::String("disabled; each scenario uses a fresh DuckDB process");
	cache_policy["os_page_cache"] = JsonValue::String("not cleared; comparisons use the same host conditions");
	result.evidence["cache_policy"] = std::move(cache_policy);
	WriteText(result.json_path, result.evidence.Serialize());
	return result;
}

void ValidateManifestHashes(const JsonValue &manifest, const fs::path &fixtures, std::string &projection_sha) {
	const auto &fixture_entries = manifest.At("fixtures").array;
	bool found_projection = false;
	for (const auto &entry : fixture_entries) {
		if (entry.At("fixture_id").AsString() != "projection") continue;
		found_projection = true;
		const auto path = fixtures / entry.At("path").AsString();
		projection_sha = Sha256(path);
		Require(projection_sha == entry.At("sha256").AsString(), "projection.om SHA-256 does not match manifest");
		for (const auto &variable : entry.At("variables").array) {
			const auto reference = variable.At("reference_csv").AsString();
			Require(Sha256(fixtures / reference) == variable.At("reference_csv_sha256").AsString(),
			        "reference CSV SHA-256 does not match manifest: " + reference);
		}
	}
	Require(found_projection, "manifest lacks projection fixture entry");
}

void ValidatePinnedDependencies(const std::map<std::string, std::string> &dependencies) {
	const std::map<std::string, std::string> expected = {
	    {"duckdb", "08e34c447bae34eaee3723cac61f2878b6bdf787"},
	    {"om-file-format", "d8855e418e2231ae8439f0c7e840fa3f93b371e3"},
	    {"extension-ci-tools", "b777c70d30942cca5bef62d6d4fa23a13362f398"}};
	for (const auto &entry : expected) {
		auto actual = dependencies.find(entry.first);
		Require(actual != dependencies.end() && actual->second == entry.second,
		        "dependency commit mismatch for " + entry.first + ": expected " + entry.second +
	        (actual == dependencies.end() ? ", found missing" : ", found " + actual->second));
	}
}

void ValidateRequiredScenarios(const JsonValue &projection_scenarios) {
	const std::set<std::string> required = {"full_scan", "single_variable", "output_plus_filter", "count"};
	std::set<std::string> found;
	for (const auto &scenario : projection_scenarios.At("scenarios").array) {
		const auto id = scenario.At("scenario_id").AsString();
		if (!found.insert(id).second) throw std::runtime_error("duplicate projection scenario " + id);
	}
	for (const auto &id : required) Require(found.count(id) != 0, "manifest lacks required projection scenario " + id);
}

} // namespace

int main(int argc, char **argv) {
	try {
		const auto options = ParseOptions(argc, argv);
		Require(fs::is_regular_file(options.duckdb), "release DuckDB CLI missing: " + options.duckdb.string());
		Require(fs::is_regular_file(options.extension), "release extension missing: " + options.extension.string());
		Require(fs::is_regular_file(options.fixtures / "manifest.json"), "fixture manifest missing");
		fs::create_directories(options.output);
		for (const auto &id : {"full_scan", "single_variable", "output_plus_filter", "count"}) {
			std::error_code ignored;
			fs::remove(options.output / (std::string(id) + ".json"), ignored);
			fs::remove(options.output / (std::string(id) + ".metrics.json"), ignored);
		}
		{
			std::error_code ignored;
			fs::remove(options.output / "summary.json", ignored);
		}
		const auto manifest = JsonParser(ReadText(options.fixtures / "manifest.json")).Parse();
		const auto &projection_scenarios = manifest.At("projection_scenarios");
		ValidateRequiredScenarios(projection_scenarios);
		std::string fixture_sha;
		ValidateManifestHashes(manifest, options.fixtures, fixture_sha);
		std::map<std::string, std::string> dependencies = {
		    {"duckdb", RunGit(options.root, "duckdb")},
		    {"om-file-format", RunGit(options.root, "third_party/om-file-format")},
		    {"extension-ci-tools", RunGit(options.root, "extension-ci-tools")}};
		ValidatePinnedDependencies(dependencies);
		std::map<std::string, const JsonValue *> scenarios;
		for (const auto &scenario : projection_scenarios.At("scenarios").array)
			scenarios.emplace(scenario.At("scenario_id").AsString(), &scenario);
		std::vector<ScenarioEvidence> completed;
		for (const auto &id : {"full_scan", "single_variable", "output_plus_filter", "count"}) {
			completed.emplace_back(RunScenario(options, *scenarios.at(id), manifest, dependencies, fixture_sha));
		}
		const auto full_data_bytes = completed.at(0).data_bytes;
		const auto single_data_bytes = completed.at(1).data_bytes;
		Require(single_data_bytes < full_data_bytes, "single-variable data bytes are not strictly below full-scan data bytes");
		JsonValue summary = JsonValue::Object();
		summary["schema_version"] = JsonValue::Number("1");
		summary["status"] = JsonValue::String("success");
		summary["fixture_id"] = JsonValue::String("projection");
		summary["fixture_sha256"] = JsonValue::String(fixture_sha);
		summary["dependency_commits"] = StringMap(dependencies);
		summary["scenario_count"] = JsonValue::Number(std::to_string(completed.size()));
		summary["required_scenarios"] = StringArray({"full_scan", "single_variable", "output_plus_filter", "count"});
		summary["full_scan_variable_data_bytes"] = JsonValue::Number(std::to_string(full_data_bytes));
		summary["single_variable_data_bytes"] = JsonValue::Number(std::to_string(single_data_bytes));
		summary["single_variable_data_bytes_reduced"] = JsonValue::Bool(true);
		auto scenario_files = JsonValue::Object();
		for (const auto &scenario : completed) scenario_files[scenario.id] = JsonValue::String(scenario.json_path.filename().string());
		summary["scenarios"] = std::move(scenario_files);
		auto platform = JsonValue::Object();
		struct utsname host {};
		if (uname(&host) == 0) {
			platform["system"] = JsonValue::String(host.sysname);
			platform["release"] = JsonValue::String(host.release);
			platform["machine"] = JsonValue::String(host.machine);
		}
		platform["build"] = JsonValue::String("release");
		platform["threads"] = JsonValue::String("1");
		summary["environment"] = std::move(platform);
		auto cache_policy = JsonValue::Object();
		cache_policy["application_cache"] = JsonValue::String("disabled; each scenario uses a fresh DuckDB process");
		cache_policy["os_page_cache"] = JsonValue::String("not cleared; comparisons use the same host conditions");
		summary["cache_policy"] = std::move(cache_policy);
		WriteText(options.output / "summary.json", summary.Serialize());
		std::cout << "duckomo_validation: 4 projection scenarios passed; summary written to "
		          << (options.output / "summary.json") << '\n';
		return 0;
	} catch (const std::exception &exception) {
		std::cerr << "duckomo_validation: " << exception.what() << '\n';
		return 1;
	}
}
