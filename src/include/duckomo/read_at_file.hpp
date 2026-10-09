#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "duckomo/metrics.hpp"

namespace duckdb { namespace duckomo {

// Checked positional-read boundary shared by local and query-scoped remote
// OM sessions. Implementations own the handle and report only completed reads.
class ReadAtFile {
public:
	virtual ~ReadAtFile() = default;
	virtual std::uint64_t Size() const noexcept = 0;
	virtual const std::string &Path() const noexcept = 0;
	virtual void ReadRange(std::uint64_t offset, std::uint64_t size, void *destination,
	                       ScanReadPhase phase, const std::string &variable_path = std::string()) const = 0;
	virtual std::vector<std::uint8_t> ReadRange(std::uint64_t offset, std::uint64_t size,
	                                            ScanReadPhase phase,
	                                            const std::string &variable_path = std::string()) const = 0;
	virtual const std::shared_ptr<ScanMetrics> &Metrics() const noexcept = 0;
};

} } // namespace duckdb::duckomo
