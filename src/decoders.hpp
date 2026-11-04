#pragma once
#include "archive.hpp"
#include <cyuediff/decoders.hpp>
namespace cyuediff::detail {
Json decode_extension(Archive &, const Package &, const ObjectExport &, const ReadOptions &,
                      const AssetDecoder &, std::uint64_t end, bool exact_size);
}
