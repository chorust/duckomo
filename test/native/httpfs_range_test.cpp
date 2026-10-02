#include "duckdb.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_file_opener.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "httpfs_extension.hpp"
#include "httpfs_om_range.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &database) {
	database.LoadStaticExtension<HttpfsExtension>();
}
} // namespace duckdb

namespace {
using namespace duckdb;

void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

enum class FaultMode { None, IgnoreRange, ShortBody, ChangeVersion };

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
		if (colon != std::string::npos && Lower(line.substr(0, colon)) == Lower(name)) {
			auto value = line.substr(colon + 1);
			const auto first = value.find_first_not_of(" \t");
			return first == std::string::npos ? std::string() : value.substr(first);
		}
	}
	return {};
}

class LoopbackRangeServer final {
public:
	explicit LoopbackRangeServer(FaultMode mode) : mode_(mode) {
		listener_ = socket(AF_INET, SOCK_STREAM, 0);
		Require(listener_ >= 0, "could not create loopback HTTP listener");
		int reuse = 1;
		setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		Require(bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
		        "could not bind loopback HTTP listener");
		Require(listen(listener_, 16) == 0, "could not listen on loopback HTTP socket");
		socklen_t size = sizeof(address);
		Require(getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) == 0,
		        "could not read loopback HTTP port");
		port_ = ntohs(address.sin_port);
		thread_ = std::thread([this] { Serve(); });
	}

	LoopbackRangeServer(const LoopbackRangeServer &) = delete;
	LoopbackRangeServer &operator=(const LoopbackRangeServer &) = delete;

	~LoopbackRangeServer() {
		running_.store(false, std::memory_order_release);
		shutdown(listener_, SHUT_RDWR);
		close(listener_);
		if (thread_.joinable()) thread_.join();
	}

	std::string Uri() const {
		return "http://127.0.0.1:" + std::to_string(port_) + "/object.om";
	}

	std::vector<std::string> Requests() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return requests_;
	}

	std::uint64_t SentGetBodyBytes() const noexcept {
		return sent_get_body_bytes_.load(std::memory_order_relaxed);
	}

private:
	static bool SendAll(int socket_fd, const char *data, std::size_t size) {
		std::size_t offset = 0;
		while (offset < size) {
			const auto sent = send(socket_fd, data + offset, size - offset, MSG_NOSIGNAL);
			if (sent <= 0) return false;
			offset += static_cast<std::size_t>(sent);
		}
		return true;
	}

	void Serve() {
		while (running_.load(std::memory_order_acquire)) {
			pollfd descriptor{listener_, POLLIN, 0};
			const auto ready = poll(&descriptor, 1, 100);
			if (ready <= 0 || !(descriptor.revents & POLLIN)) continue;
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
			const auto received = recv(client, buffer, sizeof(buffer), 0);
			if (received <= 0) return;
			request.append(buffer, static_cast<std::size_t>(received));
		}
		{
			std::lock_guard<std::mutex> guard(mutex_);
			requests_.push_back(request);
		}
		std::istringstream input(request);
		std::string method;
		std::string target;
		input >> method >> target;
		const auto content = std::string("abcdefgh");
		if (method == "HEAD") {
			const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nETag: \"v1\"\r\nConnection: close\r\n\r\n";
			SendAll(client, response.data(), response.size());
			return;
		}
		if (method != "GET") {
			const std::string response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			SendAll(client, response.data(), response.size());
			return;
		}
		const auto range = HeaderValue(request, "Range");
		std::uint64_t begin = 0;
		std::uint64_t end = content.size() - 1;
		if (range.rfind("bytes=", 0) == 0) {
			const auto dash = range.find('-', 6);
			if (dash != std::string::npos) {
				begin = std::stoull(range.substr(6, dash - 6));
				end = std::stoull(range.substr(dash + 1));
			}
		}
		if (begin >= content.size() || end >= content.size() || begin > end) {
			const std::string response = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			SendAll(client, response.data(), response.size());
			return;
		}
		const auto expected_length = static_cast<std::size_t>(end - begin + 1);
		const auto is_probe = range == "bytes=0-0";
		if (mode_ == FaultMode::IgnoreRange && !is_probe) {
			const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nETag: \"v1\"\r\nConnection: close\r\n\r\n";
			if (SendAll(client, response.data(), response.size())) {
				SendAll(client, content.data(), content.size());
				sent_get_body_bytes_.fetch_add(content.size(), std::memory_order_relaxed);
			}
			return;
		}
		const auto etag = mode_ == FaultMode::ChangeVersion && !is_probe ? "\"v2\"" : "\"v1\"";
		const auto announced_length = expected_length;
		const auto content_range = "Content-Range: bytes " + std::to_string(begin) + "-" + std::to_string(end) + "/8\r\n";
		const auto response = "HTTP/1.1 206 Partial Content\r\n" + content_range +
		                      "Content-Length: " + std::to_string(announced_length) + "\r\nETag: " + etag +
		                      "\r\nContent-Encoding: identity\r\nConnection: close\r\n\r\n";
		if (!SendAll(client, response.data(), response.size())) return;
		const auto body = content.substr(static_cast<std::size_t>(begin), expected_length);
		const auto send_size = mode_ == FaultMode::ShortBody && !is_probe ? expected_length / 2 : expected_length;
		if (send_size != 0 && SendAll(client, body.data(), send_size)) {
			sent_get_body_bytes_.fetch_add(send_size, std::memory_order_relaxed);
		}
	}

	FaultMode mode_;
	int listener_ = -1;
	std::uint16_t port_ = 0;
	std::atomic<bool> running_{true};
	std::atomic<std::uint64_t> sent_get_body_bytes_{0};
	mutable std::mutex mutex_;
	std::vector<std::string> requests_;
	std::thread thread_;
};

class ObservedSession final : public HTTPFSOmRangeSessionV2 {
public:
	explicit ObservedSession(std::string uri) {
		identity_.canonical_uri = std::move(uri);
		identity_.object_size = 8;
		identity_.version_kind = HTTPFSOmVersionKindV2::StrongETag;
		identity_.version_token = "\"v1\"";
	}

	const HTTPFSOmRangeIdentityV2 &Identity() const noexcept override { return identity_; }
	bool IsCancelled() const noexcept override { return false; }
	HTTPFSOmReadClassV2 CurrentReadClass() const noexcept override { return HTTPFSOmReadClassV2::Metadata; }
	void BeforeRequest(HTTPFSOmRequestV2 &, std::map<std::string, std::string> &headers) override {
		headers.emplace("If-Match", identity_.version_token);
	}
	bool AllowRedirect(const std::string &, const std::string &, bool) override { return false; }
	void OnResponse(const HTTPFSOmResponseV2 &response) override {
		std::lock_guard<std::mutex> guard(mutex_);
		responses_.push_back(response);
	}
	void OnBodyBytes(std::uint64_t request_id, std::uint64_t attempt, std::uint64_t received_bytes) override {
		std::lock_guard<std::mutex> guard(mutex_);
		body_events_.push_back({request_id, attempt, received_bytes});
	}
	void OnAttemptComplete(const HTTPFSOmResponseV2 &response) override {
		std::lock_guard<std::mutex> guard(mutex_);
		completed_.push_back(response);
	}

	std::vector<HTTPFSOmResponseV2> Responses() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return responses_;
	}
	std::vector<HTTPFSOmResponseV2> Completed() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return completed_;
	}
	std::uint64_t ObservedBodyBytes() const {
		std::lock_guard<std::mutex> guard(mutex_);
		std::uint64_t total = 0;
		for (const auto &event : body_events_) total += event.bytes;
		return total;
	}
	std::size_t BodyEventCount() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return body_events_.size();
	}

private:
	struct BodyEvent { std::uint64_t request_id; std::uint64_t attempt; std::uint64_t bytes; };
	HTTPFSOmRangeIdentityV2 identity_;
	mutable std::mutex mutex_;
	std::vector<HTTPFSOmResponseV2> responses_;
	std::vector<HTTPFSOmResponseV2> completed_;
	std::vector<BodyEvent> body_events_;
};

class TestOpener final : public ClientContextFileOpener, public HTTPFSOmRangeProviderV2 {
public:
	TestOpener(ClientContext &context, std::shared_ptr<ObservedSession> session)
	    : ClientContextFileOpener(context), session_(std::move(session)) {
	}
	std::uint32_t OmRangeAbiVersion() const noexcept override { return HTTPFS_OM_RANGE_ABI_VERSION; }
	std::shared_ptr<HTTPFSOmRangeSessionV2> OpenOmRangeSessionV2(const std::string &uri) override {
		if (uri != session_->Identity().canonical_uri) throw std::runtime_error("unexpected range URI");
		return session_;
	}
private:
	std::shared_ptr<ObservedSession> session_;
};

struct ReadResult {
	std::string bytes;
	std::shared_ptr<ObservedSession> session;
};

ReadResult ReadRange(FaultMode mode) {
	LoopbackRangeServer server(mode);
	duckdb::DuckDB database(nullptr);
	auto context = duckdb::make_shared_ptr<duckdb::ClientContext>(database.instance);
	context->transaction.BeginTransaction();
	auto session = std::make_shared<ObservedSession>(server.Uri());
	TestOpener opener(*context, session);
	auto &file_system = *context->db->config.file_system;
	auto handle = file_system.OpenFile(server.Uri(), duckdb::FileOpenFlags(1), &opener);
	Require(handle != nullptr, "httpfs did not open the loopback object");
	Require(file_system.GetFileSize(*handle) == 8, "httpfs HEAD did not return the object size");
	char bytes[4] = {};
	file_system.Read(*handle, bytes, sizeof(bytes), 2);
	return {std::string(bytes, sizeof(bytes)), std::move(session)};
}

void TestObservedRangeBody() {
	LoopbackRangeServer server(FaultMode::None);
	duckdb::DuckDB database(nullptr);
	auto context = duckdb::make_shared_ptr<duckdb::ClientContext>(database.instance);
	context->transaction.BeginTransaction();
	auto session = std::make_shared<ObservedSession>(server.Uri());
	TestOpener opener(*context, session);
	auto &file_system = *context->db->config.file_system;
	auto handle = file_system.OpenFile(server.Uri(), duckdb::FileOpenFlags(1), &opener);
	Require(handle != nullptr, "httpfs did not open the loopback object");
	Require(file_system.GetFileSize(*handle) == 8, "HEAD did not report the expected object length");
	char bytes[4] = {};
	file_system.Read(*handle, bytes, sizeof(bytes), 2);
	Require(std::string(bytes, sizeof(bytes)) == "cdef", "range body bytes were incorrect");
	const auto responses = session->Responses();
	const auto completed = session->Completed();
	Require(responses.size() == 3 && completed.size() == 3,
	        "observer did not receive HEAD, capability probe, and range GET attempts");
	Require(responses[0].method == "HEAD" && responses[1].method == "GET" && responses[2].method == "GET" &&
	            responses[1].status == 206 && responses[2].status == 206,
	        "observer did not preserve response method/status for each attempt");
	Require(completed[1].body_bytes == 1 && completed[2].body_bytes == 4,
	        "completed attempt events did not retain their actual response body lengths");
	Require(session->BodyEventCount() > 0 && session->ObservedBodyBytes() == server.SentGetBodyBytes(),
	        "observer body-byte events disagree with loopback server body bytes");
	Require(session->ObservedBodyBytes() == 5, "observer did not report the actual probe plus range body sizes");
	const auto requests = server.Requests();
	Require(requests.size() == 3 && HeaderValue(requests[1], "Range") == "bytes=0-0" &&
	            HeaderValue(requests[2], "Range") == "bytes=2-5",
	        "httpfs did not send the exact probe and requested byte ranges");
}

void ExpectRangeFailure(FaultMode mode, int expected_status, bool expect_body) {
	LoopbackRangeServer server(mode);
	duckdb::DuckDB database(nullptr);
	auto context = duckdb::make_shared_ptr<duckdb::ClientContext>(database.instance);
	context->transaction.BeginTransaction();
	auto session = std::make_shared<ObservedSession>(server.Uri());
	TestOpener opener(*context, session);
	auto &file_system = *context->db->config.file_system;
	auto handle = file_system.OpenFile(server.Uri(), duckdb::FileOpenFlags(1), &opener);
	Require(handle != nullptr, "httpfs did not open the fault-injection object");
	Require(file_system.GetFileSize(*handle) == 8, "fault fixture HEAD failed");
	char bytes[4] = {};
	bool failed = false;
	try {
		file_system.Read(*handle, bytes, sizeof(bytes), 2);
	} catch (const std::exception &) {
		failed = true;
	}
	Require(failed, "strict range session accepted an invalid HTTP response");
	const auto responses = session->Responses();
	const auto completed = session->Completed();
	Require(responses.size() >= 3 && completed.size() >= 3,
	        "observer did not report the failed GET attempt after the probe");
	const auto &get_response = responses.back();
	const auto &completed_attempt = completed.back();
	Require(get_response.method == "GET" && get_response.status == expected_status &&
	            completed_attempt.method == "GET" && completed_attempt.status == expected_status,
	        "observer lost the failed response status");
	if (expect_body) {
		Require(completed_attempt.body_bytes < 4 &&
		            session->ObservedBodyBytes() == 1 + completed_attempt.body_bytes,
		        "short-read observer count did not match body callbacks (observed=" +
		            std::to_string(session->ObservedBodyBytes()) + ", failed_attempt=" +
		            std::to_string(completed_attempt.body_bytes) + ")");
	} else {
		Require(completed_attempt.body_bytes == 0,
		        "observer counted a range body that was rejected before delivery");
	}
}

} // namespace

int main() {
	try {
		TestObservedRangeBody();
		ExpectRangeFailure(FaultMode::IgnoreRange, 200, false);
		ExpectRangeFailure(FaultMode::ShortBody, 206, true);
		ExpectRangeFailure(FaultMode::ChangeVersion, 206, false);
		std::cout << "httpfs_range_test: exact 206/body observer and strict failure checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "httpfs_range_test: " << error.what() << '\n';
		return 1;
	}
}
