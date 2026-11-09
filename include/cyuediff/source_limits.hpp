#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
// Finite policies used by the CLI/desktop. Direct ReadOptions hosts retain their own policy.
struct SourceReadLimits {
    std::uint32_t max_properties = 100'000;
    std::uint64_t max_property_bytes = 32ull * 1024 * 1024;
    static constexpr std::uint32_t maximum_work = 4'000'000;
    static constexpr std::uint64_t maximum_bytes = 256ull * 1024 * 1024;
    void validate() const;
    bool is_default() const;
    void apply(ReadOptions &) const;
    Json to_json() const;
};
} // namespace cyuediff
