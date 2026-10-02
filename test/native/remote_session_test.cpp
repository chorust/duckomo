#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"
#include "httpfs_extension.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &) {
}
} // namespace duckdb

namespace {
using namespace duckdb;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

std::string Lower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string HeaderValue(const std::string &headers, const std::string &name) {
	std::istringstream input(headers);
	std::string line;
	std::getline(input, line);
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto colon = line.find(':');
		if (colon == std::string::npos || Lower(line.substr(0, colon)) != Lower(name)) continue;
		auto value = line.substr(colon + 1);
		const auto first = value.find_first_not_of(" \t");
		return first == std::string::npos ? std::string() : value.substr(first);
	}
	return {};
}

enum class Fault { None, NoHead, Forbidden, IgnoreRange, WrongRange, ShortBody, ChangeVersion, DelayRange };

class LoopbackOmServer final {
public:
	LoopbackOmServer(std::string bytes, Fault fault) : bytes_(std::move(bytes)), fault_(fault) {
		listener_ = socket(AF_INET, SOCK_STREAM, 0);
		Require(listener_ >= 0, "could not create loopback listener");
		int reuse = 1;
		setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		Require(bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
		        "could not bind loopback listener");
		Require(listen(listener_, 32) == 0, "could not listen on loopback socket");
		socklen_t address_size = sizeof(address);
		Require(getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &address_size) == 0,
		        "could not discover loopback port");
		port_ = ntohs(address.sin_port);
		thread_ = std::thread([this] { Serve(); });
	}

	~LoopbackOmServer() {
		running_.store(false, std::memory_order_release);
		shutdown(listener_, SHUT_RDWR);
		close(listener_);
		if (thread_.joinable()) thread_.join();
	}

	std::string Uri(const std::string &suffix = "/raw.om") const {
		return "http://127.0.0.1:" + std::to_string(port_) + suffix;
	}

	std::uint64_t SentBodyBytes() const noexcept { return sent_body_bytes_.load(std::memory_order_relaxed); }

	std::vector<std::string> Requests() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return requests_;
	}

	bool WaitForDataRange(std::chrono::milliseconds timeout) {
		std::unique_lock<std::mutex> guard(mutex_);
		return condition_.wait_for(guard, timeout, [this] { return data_range_entered_; });
	}

private:
	static bool SendAll(int fd, const char *data, std::size_t size) {
		std::size_t offset = 0;
		while (offset < size) {
			const auto sent = send(fd, data + offset, size - offset, MSG_NOSIGNAL);
			if (sent <= 0) return false;
			offset += static_cast<std::size_t>(sent);
		}
		return true;
	}

	void Serve() {
		while (running_.load(std::memory_order_acquire)) {
			pollfd descriptor{listener_, POLLIN, 0};
			if (poll(&descriptor, 1, 100) <= 0 || !(descriptor.revents & POLLIN)) continue;
			const auto client = accept(listener_, nullptr, nullptr);
			if (client < 0) continue;
			Handle(client);
			shutdown(client, SHUT_RDWR);
			close(client);
		}
	}

	void Handle(int client) {
		std::string request;
		char buffer[4096];
		while (request.find("\r\n\r\n") == std::string::npos && request.size() < 32768) {
			const auto count = recv(client, buffer, sizeof(buffer), 0);
			if (count <= 0) return;
			request.append(buffer, static_cast<std::size_t>(count));
		}
		{
			std::lock_guard<std::mutex> guard(mutex_);
			requests_.push_back(request);
		}
		std::istringstream request_stream(request);
		std::string method;
		std::string target;
		request_stream >> method >> target;
		if (fault_ == Fault::Forbidden) {
			const std::string response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			SendAll(client, response.data(), response.size());
			return;
		}
		if (method == "HEAD") {
			const auto status = fault_ == Fault::NoHead ? "405 Method Not Allowed" : "200 OK";
			const auto response = std::string("HTTP/1.1 ") + status + "\r\nContent-Length: " +
			                      std::to_string(bytes_.size()) + "\r\nETag: \"v1\"\r\nConnection: close\r\n\r\n";
			SendAll(client, response.data(), response.size());
			return;
		}
		if (method != "GET") {
			const std::string response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			SendAll(client, response.data(), response.size());
			return;
		}
		const auto range = HeaderValue(request, "Range");
		if (range.rfind("bytes=", 0) != 0) {
			const std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			SendAll(client, response.data(), response.size());
			return;
		}
		const auto dash = range.find('-', 6);
		if (dash == std::string::npos) return;
		auto begin = static_cast<std::uint64_t>(std::stoull(range.substr(6, dash - 6)));
		auto end = static_cast<std::uint64_t>(std::stoull(range.substr(dash + 1)));
		const bool probe = begin == 0 && end == 0;
		if (fault_ == Fault::DelayRange && !probe) {
			{
				std::lock_guard<std::mutex> guard(mutex_);
				data_range_entered_ = true;
			}
			condition_.notify_all();
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
		if (fault_ == Fault::IgnoreRange && !probe) {
			const auto response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(bytes_.size()) +
			                      "\r\nETag: \"v1\"\r\nConnection: close\r\n\r\n";
			if (SendAll(client, response.data(), response.size()) &&
			    SendAll(client, bytes_.data(), bytes_.size())) {
				sent_body_bytes_.fetch_add(bytes_.size(), std::memory_order_relaxed);
			}
			return;
		}
		if (end >= bytes_.size() || begin > end) {
			const std::string response = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			SendAll(client, response.data(), response.size());
			return;
		}
		if (fault_ == Fault::WrongRange && !probe && end + 1 < bytes_.size()) ++end;
		const auto expected = static_cast<std::size_t>(end - begin + 1);
		const auto version = fault_ == Fault::ChangeVersion && !probe ? "\"v2\"" : "\"v1\"";
		const auto content_range = "Content-Range: bytes " + std::to_string(begin) + "-" + std::to_string(end) +
		                           "/" + std::to_string(bytes_.size()) + "\r\n";
		const auto response = "HTTP/1.1 206 Partial Content\r\n" + content_range + "Content-Length: " +
		                      std::to_string(expected) + "\r\nETag: " + version +
		                      "\r\nContent-Encoding: identity\r\nConnection: close\r\n\r\n";
		if (!SendAll(client, response.data(), response.size())) return;
		auto body_size = expected;
		if (fault_ == Fault::ShortBody && !probe && body_size > 1) --body_size;
		if (body_size && SendAll(client, bytes_.data() + begin, body_size)) {
			sent_body_bytes_.fetch_add(body_size, std::memory_order_relaxed);
		}
	}

	std::string bytes_;
	Fault fault_;
	int listener_ = -1;
	std::uint16_t port_ = 0;
	std::atomic<bool> running_{true};
	std::atomic<std::uint64_t> sent_body_bytes_{0};
	mutable std::mutex mutex_;
	std::condition_variable condition_;
	bool data_range_entered_ = false;
	std::vector<std::string> requests_;
	std::thread thread_;
};

std::string ReadFixture() {
	std::ifstream file(DUCKOMO_REMOTE_FIXTURE_PATH, std::ios::binary);
	Require(file.good(), "could not open pinned raw OM fixture");
	return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<std::vector<std::string>> QueryRows(Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result && !result->HasError(), sql + ": " + (result ? result->GetError() : "no result"));
	std::vector<std::vector<std::string>> rows;
	while (true) {
		auto chunk = result->Fetch();
		if (!chunk || chunk->size() == 0) break;
		for (idx_t row = 0; row < chunk->size(); row++) {
			std::vector<std::string> values;
			for (idx_t column = 0; column < chunk->ColumnCount(); column++) {
				values.emplace_back(chunk->GetValue(column, row).ToString());
			}
			rows.emplace_back(std::move(values));
		}
	}
	std::sort(rows.begin(), rows.end());
	return rows;
}

std::string Scalar(Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	if (result->HasError()) {
		throw std::runtime_error("query failed: " + result->GetError());
	}
	if (result->RowCount() != 1 || result->ColumnCount() != 1) {
		throw std::runtime_error("expected a single scalar result");
	}
	return result->GetValue(0, 0).ToString();
}

std::string SqlString(const std::string &value) {
	std::string result = "'";
	for (auto character : value) {
		result.push_back(character);
		if (character == '\'') result.push_back('\'');
	}
	return result + "'";
}

std::uint64_t MetricUint(const std::string &json, const std::string &name) {
	const auto marker = "\"" + name + "\":";
	const auto start = json.find(marker);
	Require(start != std::string::npos, "metrics JSON is missing " + name);
	const auto value_start = start + marker.size();
	const auto value_end = json.find_first_not_of("0123456789", value_start);
	return std::stoull(json.substr(value_start, value_end - value_start));
}

std::string FixturePath() { return DUCKOMO_REMOTE_FIXTURE_PATH; }

void LoadExtensions(DuckDB &database) {
	database.LoadStaticExtension<HttpfsExtension>();
	database.LoadStaticExtension<DuckomoExtension>();
}

void TestLocalAndRemoteResultsAndAccounting() {
	DuckDB database(nullptr);
	LoadExtensions(database);
	Connection connection(database);
	LoopbackOmServer server(ReadFixture(), Fault::None);
	const auto local = QueryRows(connection, "SELECT * FROM read_om(" + SqlString(FixturePath()) + ")");
	const auto remote_uri = server.Uri();
	const auto remote = QueryRows(connection, "SELECT * FROM read_om(" + SqlString(remote_uri) + ")");
	Require(!local.empty() && local == remote, "HTTP read_om rows differ from the local fixture");
	const auto cold_metrics = Scalar(connection, "SELECT metrics FROM duckomo_last_scan_metrics()");
	Require(cold_metrics.find("\"schema_version\":3") != std::string::npos,
	        "remote scan metrics were not published as v3");
	const auto cold_body = MetricUint(cold_metrics, "response_body_bytes");
	const auto cold_sent = server.SentBodyBytes();
	const auto cold_request_count = server.Requests().size();
	Require(cold_body == cold_sent, "cold transport body count differs from server accounting");
	const auto requests = server.Requests();
	Require(requests.size() >= 3 && requests[0].find("HEAD ") != std::string::npos &&
	            HeaderValue(requests[1], "Range") == "bytes=0-0",
	        "remote read did not perform a fresh HEAD and exact one-byte range probe");
	Require(server.SentBodyBytes() > 1, "HTTP fixture did not observe OM body bytes");
	const auto warm = QueryRows(connection, "SELECT * FROM read_om(" + SqlString(remote_uri) + ")");
	Require(warm == local, "warm cached HTTP scan changed the complete local result");
	const auto warm_metrics = Scalar(connection, "SELECT metrics FROM duckomo_last_scan_metrics()");
	const auto warm_body = MetricUint(warm_metrics, "response_body_bytes");
	const auto warm_sent = server.SentBodyBytes() - cold_sent;
	Require(warm_body == warm_sent, "warm transport body count differs from server accounting");
	Require(MetricUint(warm_metrics, "hits") > 0, "warm remote scan did not hit the session range cache");
	Require(warm_body < cold_body, "warm strong-version scan did not reduce HTTP response body bytes");
	const auto warm_requests = server.Requests();
	Require(warm_requests.size() > cold_request_count,
	        "warm query omitted fresh authorization metadata requests");
	bool saw_fresh_head = false;
	bool saw_fresh_range_probe = false;
	for (std::size_t i = cold_request_count; i < warm_requests.size(); i++) {
		saw_fresh_head = saw_fresh_head || warm_requests[i].find("HEAD ") != std::string::npos;
		saw_fresh_range_probe = saw_fresh_range_probe || HeaderValue(warm_requests[i], "Range") == "bytes=0-0";
	}
	Require(saw_fresh_head && saw_fresh_range_probe,
	        "warm cache skipped the fresh HEAD and one-byte authorization probe");
}

void TestRemoteFailuresAreRedactedAndRecoverable() {
	DuckDB database(nullptr);
	LoadExtensions(database);
	Connection connection(database);
	const auto fixture = ReadFixture();
	const std::vector<Fault> failures = {Fault::NoHead, Fault::Forbidden, Fault::IgnoreRange,
	                                     Fault::WrongRange, Fault::ShortBody, Fault::ChangeVersion};
	for (const auto fault : failures) {
		LoopbackOmServer server(fixture, fault);
		const auto result = connection.Query("SELECT count(*) FROM read_om(" + SqlString(server.Uri()) + ")");
		Require(result && result->HasError(), "read_om accepted a remote protocol or authorization failure");
		const auto message = result->GetError();
		Require(message.find("v1") == std::string::npos,
		        "remote failure leaked an object version token: " + message);
	}
	LoopbackOmServer good(fixture, Fault::None);
	const auto recovered = QueryRows(connection, "SELECT * FROM read_om(" + SqlString(good.Uri()) + ")");
	Require(!recovered.empty(), "a valid remote read failed after previous query errors");
}

void TestCredentialAndSignatureRedaction() {
	DuckDB database(nullptr);
	LoadExtensions(database);
	Connection connection(database);
	LoopbackOmServer server(ReadFixture(), Fault::Forbidden);
	auto uri = server.Uri("/raw.om?X-Amz-Credential=access-secret&X-Amz-Signature=signature-secret");
	const auto result = connection.Query("SELECT * FROM read_om(" + SqlString(uri) + ")");
	Require(result && result->HasError(), "signed URL fault fixture unexpectedly succeeded");
	const auto message = result->GetError();
	Require(message.find("access-secret") == std::string::npos && message.find("signature-secret") == std::string::npos,
	        "remote diagnostic leaked signed query parameters");
	const auto metrics = QueryRows(connection, "SELECT metrics FROM duckomo_last_scan_metrics()");
	Require(!metrics.empty() && metrics.back()[0].find("access-secret") == std::string::npos &&
	            metrics.back()[0].find("signature-secret") == std::string::npos,
	        "remote metrics leaked signed query parameters");
}

void TestRemoteCancellationAndRecovery() {
	DuckDB database(nullptr);
	LoadExtensions(database);
	Connection connection(database);
	LoopbackOmServer server(ReadFixture(), Fault::DelayRange);
	std::string query_error;
	std::thread query_thread([&] {
		try {
			auto result = connection.SendQuery("SELECT count(*) FROM read_om(" + SqlString(server.Uri()) + ")");
			if (!result) {
				query_error = "remote cancellation query returned no result";
				return;
			}
			if (result->HasError()) {
				query_error = result->GetError();
				return;
			}
			while (result->Fetch()) {
			}
			if (result->HasError()) query_error = result->GetError();
		} catch (const std::exception &error) {
			query_error = error.what();
		}
	});
	const auto reached_range = server.WaitForDataRange(std::chrono::seconds(5));
	if (reached_range) connection.Interrupt();
	query_thread.join();
	Require(reached_range, "remote query did not reach a data range request");
	std::string lowered = query_error;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
	               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	Require(lowered.find("interrupt") != std::string::npos || lowered.find("cancel") != std::string::npos,
	        "remote read ignored ClientContext cancellation: " + query_error);
	Require(Scalar(connection, "SELECT count(*) FROM read_om(" + SqlString(FixturePath()) + ")") == "6",
	        "local query did not recover after cancelling a remote read");
}

} // namespace

int main() {
	try {
		TestLocalAndRemoteResultsAndAccounting();
		TestRemoteFailuresAreRedactedAndRecoverable();
		TestCredentialAndSignatureRedaction();
		TestRemoteCancellationAndRecovery();
		std::cout << "remote_session_test: identity, cancellation, range authorization, error redaction, and recovery checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "remote_session_test: " << error.what() << '\n';
		return 1;
	}
}
