#include "duckomo/range_cache.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace duckdb {
namespace duckomo {

bool ObjectIdentity::Cacheable() const noexcept {
	return !local && !canonical_uri.empty() && !endpoint.empty() && !access_partition.empty() &&
	       !version_token.empty() &&
	       ((version_strength == ObjectVersionStrength::StrongETag && version_token.rfind("W/", 0) != 0) ||
	                                (version_strength == ObjectVersionStrength::S3VersionId && version_token != "null"));
}

bool ObjectIdentity::operator==(const ObjectIdentity &other) const noexcept {
	return canonical_uri == other.canonical_uri && endpoint == other.endpoint &&
	       access_partition == other.access_partition && version_token == other.version_token &&
	       size == other.size && version_strength == other.version_strength && local == other.local;
}

std::uint64_t ObjectIdentity::AccountedBytes() const {
	std::uint64_t total = sizeof(ObjectIdentity) + 4 * sizeof(void *);
	for (const auto *field : {&canonical_uri, &endpoint, &access_partition, &version_token}) {
		if (field->size() > std::numeric_limits<std::uint64_t>::max() - total) {
			throw std::overflow_error("range cache key size overflow");
		}
		total += field->size();
	}
	return total;
}

RangeCache::RangeCache(std::uint64_t capacity_bytes) : capacity_bytes_(capacity_bytes) {
}

std::uint64_t RangeCache::CheckedEnd(std::uint64_t offset, std::uint64_t size) {
	if (size > std::numeric_limits<std::uint64_t>::max() - offset) {
		throw std::overflow_error("range cache interval overflow");
	}
	return offset + size;
}

std::uint64_t RangeCache::EntryCost(const ObjectIdentity &identity, std::uint64_t size) {
	const auto key_cost = identity.AccountedBytes();
	const auto index_cost = sizeof(Entry) + 4 * sizeof(void *);
	if (size > std::numeric_limits<std::uint64_t>::max() - key_cost ||
	    index_cost > std::numeric_limits<std::uint64_t>::max() - key_cost - size) {
		throw std::overflow_error("range cache entry size overflow");
	}
	return key_cost + size + index_cost;
}

bool RangeCache::Read(const ObjectIdentity &identity, std::uint64_t offset, std::uint64_t size, void *destination) {
	const auto end = CheckedEnd(offset, size);
	if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || !identity.Cacheable() ||
	    offset > identity.size || size > identity.size - offset ||
	    (size != 0 && destination == nullptr)) {
		std::lock_guard<std::mutex> guard(mutex_);
		++misses_;
		return false;
	}
	std::lock_guard<std::mutex> guard(mutex_);
	if (!enabled_ || size == 0) {
		++misses_;
		return false;
	}
	for (auto entry = entries_.begin(); entry != entries_.end(); ++entry) {
		if (!(entry->identity == identity) || offset < entry->offset) continue;
		const auto entry_end = CheckedEnd(entry->offset, entry->bytes.size());
		if (end > entry_end) continue;
		const auto relative = static_cast<std::size_t>(offset - entry->offset);
		std::memcpy(destination, entry->bytes.data() + relative, static_cast<std::size_t>(size));
		hit_bytes_ = size > std::numeric_limits<std::uint64_t>::max() - hit_bytes_
		                 ? std::numeric_limits<std::uint64_t>::max()
		                 : hit_bytes_ + size;
		++hits_;
		entries_.splice(entries_.begin(), entries_, entry);
		return true;
	}
	++misses_;
	return false;
}

void RangeCache::EvictToFit(std::uint64_t cost) {
	while (!entries_.empty() && (cost > capacity_bytes_ || accounted_bytes_ > capacity_bytes_ - cost)) {
		accounted_bytes_ -= entries_.back().accounted_bytes;
		entries_.pop_back();
	}
}

bool RangeCache::Insert(const ObjectIdentity &identity, std::uint64_t offset, const void *source, std::uint64_t size) {
	CheckedEnd(offset, size);
	if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || !identity.Cacheable() ||
	    offset > identity.size || size > identity.size - offset || size == 0 || source == nullptr) {
		return false;
	}
	const auto cost = EntryCost(identity, size);
	std::lock_guard<std::mutex> guard(mutex_);
	if (!enabled_ || cost > capacity_bytes_) return false;
	for (auto entry = entries_.begin(); entry != entries_.end();) {
		if (entry->identity == identity && entry->offset == offset && entry->bytes.size() == size) {
			accounted_bytes_ -= entry->accounted_bytes;
			entry = entries_.erase(entry);
		} else {
			++entry;
		}
	}
	EvictToFit(cost); // free space before allocating/copying the new payload
	Entry incoming;
	incoming.identity = identity;
	incoming.offset = offset;
	incoming.accounted_bytes = cost;
	incoming.bytes.resize(static_cast<std::size_t>(size));
	std::memcpy(incoming.bytes.data(), source, static_cast<std::size_t>(size));
	entries_.push_front(std::move(incoming));
	accounted_bytes_ += cost;
	return true;
}

void RangeCache::SetEnabled(bool enabled) {
	std::lock_guard<std::mutex> guard(mutex_);
	enabled_ = enabled;
	if (!enabled_) {
		entries_.clear();
		accounted_bytes_ = 0;
	}
}

void RangeCache::SetCapacity(std::uint64_t capacity_bytes) {
	std::lock_guard<std::mutex> guard(mutex_);
	capacity_bytes_ = capacity_bytes;
	while (accounted_bytes_ > capacity_bytes_ && !entries_.empty()) {
		accounted_bytes_ -= entries_.back().accounted_bytes;
		entries_.pop_back();
	}
}

RangeCacheClearResult RangeCache::Clear() {
	std::lock_guard<std::mutex> guard(mutex_);
	RangeCacheClearResult result {static_cast<std::uint64_t>(entries_.size()), accounted_bytes_};
	entries_.clear();
	accounted_bytes_ = 0;
	return result;
}

RangeCacheStats RangeCache::Stats() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return {static_cast<std::uint64_t>(entries_.size()), accounted_bytes_, hits_, misses_, hit_bytes_};
}

} // namespace duckomo
} // namespace duckdb
