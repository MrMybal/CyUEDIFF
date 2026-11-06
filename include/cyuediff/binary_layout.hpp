#pragma once
#include <cyuediff/decoders.hpp>
namespace cyuediff {
struct BinaryLayoutOptions {
    std::uint64_t max_bytes = 2ull * 1024 * 1024, max_text_bytes = 2ull * 1024 * 1024;
    std::uint32_t max_nodes = 100'000, max_depth = 48, max_string_bytes = 65536;
    OperationControl control;
};
struct BinaryLayoutReadOptions {
    std::uint64_t max_read_bytes = 16ull * 1024 * 1024, max_text_bytes = 4ull * 1024 * 1024;
    std::uint32_t max_values = 100'000, max_depth = 48, max_string_bytes = 65536;
    OperationControl control;
    NarrowStringEncoding narrow_string_encoding = NarrowStringEncoding::unspecified;
    // Optional JSON package-reference resolver (used by schema-backed cooked inputs).
    std::function<Json(std::int32_t)> reference;
    // Optional enclosing-operation budget. Charges include failed reads.
    std::function<void(std::uint32_t, std::uint64_t)> charge;
};
// Immutable descriptions of serialized little-endian bytes, not C++ memory layouts.
// No engine/plugin code is discovered or executed.
class BinaryLayouts {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit BinaryLayouts(std::shared_ptr<const Impl>);

  public:
    static BinaryLayouts parse(const Json &, const BinaryLayoutOptions & = {});
    static BinaryLayouts parse_text(const std::string &, const BinaryLayoutOptions & = {});
    static BinaryLayouts read(const fs::path &, const BinaryLayoutOptions & = {});
    bool contains(const std::string &) const;
    const std::string &sha256() const;
    Json to_json() const;
    NarrowStringEncoding narrow_string_encoding() const;
    Json decode(DecoderInput &, const std::string &, const BinaryLayoutReadOptions & = {}) const;
    std::shared_ptr<const DecoderRegistry> decoder_registry(const BinaryLayoutReadOptions & = {}) const;
};
} // namespace cyuediff
