#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct BinaryDeltaOptions {
    std::uint32_t block_size = 65536; // 16 bytes through 1 MiB; indexes aligned base blocks.
    std::uint64_t max_input_bytes = 256ull * 1024 * 1024;
    std::uint64_t max_literal_bytes = 64ull * 1024 * 1024;
    std::uint64_t max_match_bytes = 512ull * 1024 * 1024;
    std::uint64_t max_manifest_bytes = 160ull * 1024 * 1024;
    std::size_t max_blocks = 1000000, max_operations = 100000;
    OperationControl control;
};
enum class BinaryDeltaKind { copy, literal };
struct BinaryDeltaOperation {
    BinaryDeltaKind kind = BinaryDeltaKind::copy;
    std::uint64_t offset = 0, bytes = 0;
    std::vector<std::uint8_t> data; // Literal bytes only; copy ranges always reference the original base.
};
struct BinaryDelta {
    std::uint64_t base_bytes = 0, target_bytes = 0;
    std::string base_sha256, target_sha256;
    std::uint32_t block_size = 65536;
    std::vector<BinaryDeltaOperation> operations;
    // Checked derived counters. This report contains no literal payload.
    Json summary(const BinaryDeltaOptions & = {}) const;
};
// Rolling target windows recover aligned base blocks after insertions/deletions.
// Every candidate copy is compared byte-for-byte; weak hashes are lookup hints only.
// Exact reconstruction is the scope, independently of Unreal serialization semantics.
BinaryDelta make_binary_delta(const std::vector<std::uint8_t> &base, const std::vector<std::uint8_t> &target,
                              const BinaryDeltaOptions & = {});
} // namespace cyuediff
