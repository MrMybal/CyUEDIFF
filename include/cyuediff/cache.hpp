#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct PackageCacheOptions {
    std::size_t max_entries = 8;
    std::uint64_t max_source_bytes = 128ull * 1024 * 1024;
};
struct PackageCacheStats {
    std::size_t entries = 0;
    std::uint64_t source_bytes = 0, hits = 0, misses = 0;
};
// Thread-safe in-memory LRU. Every lookup re-hashes the package and all known
// companions; timestamps alone never establish equality. No persistent files.
class PackageCache {
    struct State;
    std::unique_ptr<State> state_;

  public:
    explicit PackageCache(const PackageCacheOptions &options = {});
    ~PackageCache();
    PackageCache(const PackageCache &) = delete;
    PackageCache &operator=(const PackageCache &) = delete;
    std::shared_ptr<const Package> open(const fs::path &, const ReadOptions &options = {});
    void clear();
    PackageCacheStats stats() const;
};
} // namespace cyuediff
