#include "duckomo/range_cache.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace duckdb::duckomo;

namespace {
void Require(bool condition, const std::string &message) {
	if (!condition) throw std::runtime_error(message);
}

ObjectIdentity Identity(std::string version = "\"etag-1\"", std::string partition = "partition-a") {
	ObjectIdentity result;
	result.canonical_uri = "https://example.invalid/object.om";
	result.endpoint = "https://example.invalid";
	result.access_partition = std::move(partition);
	result.version_token = std::move(version);
	result.size = 64;
	result.version_strength = ObjectVersionStrength::StrongETag;
	return result;
}
} // namespace

int main() {
	try {
		RangeCache cache(4096);
		const auto identity = Identity();
		const std::array<std::uint8_t, 8> bytes {{0, 1, 2, 3, 4, 5, 6, 7}};
		Require(cache.Insert(identity, 10, bytes.data(), bytes.size()), "strong-version entry was not cached");
		std::array<std::uint8_t, 3> copied {};
		Require(cache.Read(identity, 12, copied.size(), copied.data()), "containing range should hit");
		Require(copied[0] == 2 && copied[1] == 3 && copied[2] == 4, "cache must copy out exact requested bytes");
		Require(!cache.Read(identity, 18, 1, copied.data()), "range past cached end must miss");
		Require(!cache.Read(Identity("W/\\\"weak\\\""), 10, 1, copied.data()), "different version must not alias");
		Require(!cache.Read(Identity("\"etag-1\"", "partition-b"), 10, 1, copied.data()),
		        "different access partition must not alias");

		auto weak = identity;
		weak.version_strength = ObjectVersionStrength::Weak;
		weak.version_token = "W/\"weak\"";
		Require(!cache.Insert(weak, 0, bytes.data(), bytes.size()), "weak versions must bypass cache writes");
		weak.version_strength = ObjectVersionStrength::StrongETag;
		Require(!cache.Insert(weak, 0, bytes.data(), bytes.size()), "weak ETag text must not be cached as strong");
		auto s3_null_version = identity;
		s3_null_version.version_strength = ObjectVersionStrength::S3VersionId;
		s3_null_version.version_token = "null";
		Require(!cache.Insert(s3_null_version, 0, bytes.data(), bytes.size()),
		        "the literal null S3 version id must not be treated as a strong validator");
		auto local = identity;
		local.local = true;
		Require(!cache.Insert(local, 0, bytes.data(), bytes.size()), "local files must bypass cross-query cache writes");

		cache.SetCapacity(1);
		Require(cache.Stats().entries == 0 && cache.Stats().accounted_bytes == 0,
		        "capacity reduction must evict entries immediately");
		Require(!cache.Insert(identity, 0, bytes.data(), bytes.size()), "oversized entry must bypass the cache");
		cache.SetCapacity(4096);
		Require(cache.Insert(identity, 0, bytes.data(), bytes.size()), "entry should fit after capacity increase");
		cache.SetEnabled(false);
		Require(cache.Stats().entries == 0, "disabling the cache must clear entries");
		Require(!cache.Insert(identity, 8, bytes.data(), bytes.size()), "disabled cache must not store entries");
		const auto cleared = cache.Clear();
		Require(cleared.entries == 0 && cleared.accounted_bytes == 0, "clear must report current occupancy");

		cache.SetEnabled(true);
		Require(cache.Insert(identity, 0, bytes.data(), bytes.size()), "first LRU entry should fit");
		const auto entry_cost = cache.Stats().accounted_bytes;
		cache.SetCapacity(entry_cost * 2);
		Require(cache.Insert(identity, 8, bytes.data(), bytes.size()), "second LRU entry should fit");
		Require(cache.Read(identity, 0, 1, copied.data()), "reading the older entry should make it most-recently used");
		Require(cache.Insert(identity, 16, bytes.data(), bytes.size()), "third LRU entry should evict the least-recently used");
		Require(cache.Read(identity, 0, 1, copied.data()), "recently used entry should survive eviction");
		Require(!cache.Read(identity, 8, 1, copied.data()), "least-recently used entry should be evicted");

		std::atomic<bool> concurrent_ok {true};
		std::vector<std::thread> readers;
		for (std::size_t worker = 0; worker < 8; worker++) {
			readers.emplace_back([&] {
				std::array<std::uint8_t, 1> value {};
				for (std::size_t iteration = 0; iteration < 100; iteration++) {
					if (!cache.Read(identity, 16, value.size(), value.data()) || value[0] != bytes[0]) concurrent_ok = false;
				}
			});
		}
		for (auto &reader : readers) reader.join();
		Require(concurrent_ok, "concurrent cache hits and misses must remain race-free");
		readers.clear();
		for (std::size_t worker = 0; worker < 8; worker++) {
			readers.emplace_back([&] {
				std::array<std::uint8_t, 1> value {};
				if (cache.Read(identity, 50, value.size(), value.data())) concurrent_ok = false;
			});
		}
		for (auto &reader : readers) reader.join();
		Require(concurrent_ok, "concurrent misses must remain race-free");
		Require(cache.Stats().misses >= 8, "concurrent missing ranges must be counted");

		std::cout << "range_cache_test: identity, containment, bounds, eviction, and disable checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "range_cache_test: " << error.what() << '\n';
		return 1;
	}
}
