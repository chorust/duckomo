#pragma once

#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <vector>

namespace duckdb {
namespace duckomo {

enum class ObjectVersionStrength : std::uint8_t { None, Weak, StrongETag, S3VersionId };

// Identity includes the endpoint and salted access partition so credentials,
// signed URLs, endpoints, or object versions cannot alias cache entries.
struct ObjectIdentity final {
	std::string canonical_uri;
	std::string endpoint;
	std::string redacted_display_id;
	std::string access_partition;
	std::string version_token;
	std::uint64_t size = 0;
	ObjectVersionStrength version_strength = ObjectVersionStrength::None;
	bool local = false;

	bool Cacheable() const noexcept;
	bool operator==(const ObjectIdentity &other) const noexcept;
	std::uint64_t AccountedBytes() const;
};

struct RangeCacheStats final {
	std::uint64_t entries = 0;
	std::uint64_t accounted_bytes = 0;
	std::uint64_t control_bytes = 0;
	std::uint64_t capacity_bytes = 0;
	std::uint64_t peak_charged_bytes = 0;
	std::uint64_t hits = 0;
	std::uint64_t misses = 0;
	std::uint64_t hit_bytes = 0;
	std::uint64_t evictions = 0;
	bool enabled = true;
};

struct RangeCacheClearResult final {
	std::uint64_t entries = 0;
	std::uint64_t accounted_bytes = 0;
};

// A bounded, session-owned LRU. It never expands a requested range: lookups
// use exact or containing entries, and inserts copy only the successful body
// that the caller actually requested.
class RangeCache final {
public:
	explicit RangeCache(std::uint64_t capacity_bytes = 64ULL * 1024 * 1024);

	bool Read(const ObjectIdentity &identity, std::uint64_t offset, std::uint64_t size, void *destination);
	bool Insert(const ObjectIdentity &identity, std::uint64_t offset, const void *source, std::uint64_t size);
	void SetEnabled(bool enabled);
	void SetCapacity(std::uint64_t capacity_bytes);
	RangeCacheClearResult Clear();
	RangeCacheStats Stats() const;

private:
	struct Entry final {
		ObjectIdentity identity;
		std::uint64_t offset = 0;
		std::vector<std::uint8_t> bytes;
		std::uint64_t accounted_bytes = 0;
	};

	static std::uint64_t CheckedEnd(std::uint64_t offset, std::uint64_t size);
	static std::uint64_t EntryCost(const ObjectIdentity &identity, std::uint64_t size);
	void EvictToFit(std::uint64_t cost);

	mutable std::mutex mutex_;
	std::list<Entry> entries_; // front is most recently used
	std::uint64_t capacity_bytes_;
	std::uint64_t accounted_bytes_ = 0;
	std::uint64_t hits_ = 0;
	std::uint64_t misses_ = 0;
	std::uint64_t hit_bytes_ = 0;
	std::uint64_t peak_charged_bytes_ = 0;
	std::uint64_t evictions_ = 0;
	bool enabled_ = true;
};

} // namespace duckomo
} // namespace duckdb
