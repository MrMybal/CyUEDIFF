#pragma once
#include "native.hpp"
namespace cyuediff::detail {
NativeDecodeResult decode_reference_skeleton(Archive &, const Package &, std::uint64_t end,
                                             const ReadOptions &);
}
