#pragma once
#include "archive.hpp"
#include <cyuediff/decoders.hpp>
namespace cyuediff::detail {
void validate_extension_value(const Json &, const ReadOptions &,
                              const std::function<void(std::uint32_t, std::uint64_t)> &charge,
                              unsigned depth = 0);
Json decode_extension(Archive &, const Package &, const ObjectExport &, const ReadOptions &,
                      const AssetDecoder &, std::uint64_t end, bool exact_size,
                      const std::function<void(std::uint32_t, std::uint64_t)> &charge = {},
                      unsigned depth = 0, bool prefix_consumed = false,
                      const Json *tagged_properties = nullptr, const TaggedType *effective_type = nullptr);
}
