#pragma once
#include <cyuediff/core.hpp>
#include <optional>
namespace cyuediff {
// Virtual names are never used as extraction destinations.
struct ContainerEntry {
    std::string id, path;
    // IoStore BLAKE3-160 over the complete decoded chunk; empty for Pak.
    std::string source_hash;
    std::uint8_t storage_flags = 0;
    std::uint64_t offset = 0, size = 0, stored_bytes = 0;
    std::uint32_t blocks = 0;
    std::uint8_t chunk_type = 0;
    bool encrypted = false, deleted = false;
    Json to_json() const;
};
struct ContainerOptions {
    std::uint64_t max_index_bytes = 128ull * 1024 * 1024;
    std::uint32_t max_entries = 1'000'000, max_blocks = 4'000'000;
    std::uint32_t max_string_bytes = 65536, max_depth = 128;
    OperationControl control;
};
// Container method names are independent of the bulk-payload numeric codec ABI.
using ContainerBlockDecoder = std::function<std::size_t(const std::string &, const std::uint8_t *,
                                                        std::size_t, std::uint8_t *, std::size_t)>;
struct ContainerReadOptions {
    std::uint64_t max_payload_bytes = 256ull * 1024 * 1024;
    std::uint32_t max_block_bytes = 16 * 1024 * 1024;
    ContainerBlockDecoder decoder;
    OperationControl control;
    // Aggregate decoded bytes read by compare_containers, including both sides.
    std::uint64_t max_comparison_bytes = 1024ull * 1024 * 1024;
};
// Adapt the explicit bulk codec ABI for Oodle and LZ4; other names fail closed.
ContainerBlockDecoder container_block_decoder(BlockDecoder decoder);
class Container {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Container(std::shared_ptr<const Impl> impl);

  public:
    // Unencrypted Pak 10/11 (full directory index) and IoStore TOC 3 through 8.
    // UCAS partitions are resolved beside the TOC; no engine executable is used.
    static Container open(const fs::path &, const ContainerOptions & = {});
    const std::vector<ContainerEntry> &entries() const;
    const std::vector<fs::path> &files() const;
    std::vector<std::uint8_t> read(std::size_t entry, const ContainerReadOptions & = {}) const;
    // Summary without serializing every entry; suitable for catalogue interfaces.
    Json metadata() const;
    Json to_json() const;
};
struct ContainerPair {
    std::string key;
    std::optional<std::size_t> before, after;
};
// Live members only, sorted by exact virtual path or unnamed chunk ID.
// Duplicate keys are rejected rather than silently selecting an arbitrary member.
std::vector<ContainerPair> pair_container_entries(const Container &, const Container &,
                                                  const OperationControl & = {});
// Decoded-byte comparison paired by virtual path or chunk ID if unnamed.
// Does not interpret cooked UObject/Zen data.
Json compare_containers(const Container &, const Container &, const ContainerReadOptions & = {});
} // namespace cyuediff
