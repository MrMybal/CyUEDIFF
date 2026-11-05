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
    std::size_t max_composition_fragments = 1000000;
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
    Json to_json(const BinaryDeltaOptions & = {}) const;
    static BinaryDelta from_json(const Json &, const BinaryDeltaOptions & = {});
    static BinaryDelta parse(const std::string &, const BinaryDeltaOptions & = {});
    // Compact wire format: fixed little-endian integers and raw literal bytes.
    std::vector<std::uint8_t> to_binary(const BinaryDeltaOptions & = {}) const;
    static BinaryDelta from_binary(const std::vector<std::uint8_t> &, const BinaryDeltaOptions & = {});
};
// Rolling target windows recover aligned base blocks after insertions/deletions.
// Every candidate copy is compared byte-for-byte; weak hashes are lookup hints only.
// Exact reconstruction is the scope, independently of Unreal serialization semantics.
BinaryDelta make_binary_delta(const std::vector<std::uint8_t> &base, const std::vector<std::uint8_t> &target,
                              const BinaryDeltaOptions & = {});
// No partial result is returned: both identities and every operation are checked.
std::vector<std::uint8_t> apply_binary_delta(const std::vector<std::uint8_t> &base, const BinaryDelta &,
                                             const BinaryDeltaOptions & = {});
// Compose A->B and B->C as A->C without endpoint files. Intermediate identity claims must agree;
// only applying the result verifies endpoint bytes. Fragment work and duplicated literals are bounded.
BinaryDelta compose_binary_deltas(const BinaryDelta &first, const BinaryDelta &second,
                                  const BinaryDeltaOptions & = {});
// Bounded binary file reads; no output file writes or engine/codec discovery.
std::vector<std::uint8_t> read_binary_file(const fs::path &, std::uint64_t max_bytes,
                                           const OperationControl & = {});
BinaryDelta make_binary_delta_files(const fs::path &base, const fs::path &target,
                                    const BinaryDeltaOptions & = {});
BinaryDelta read_binary_delta(const fs::path &, const BinaryDeltaOptions & = {});
} // namespace cyuediff
