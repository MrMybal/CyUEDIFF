#pragma once
#include "archive.hpp"
namespace cyuediff::detail {
struct NativeDecodeResult {
    Json data;
    std::uint64_t end = 0;
};
bool is_edgraph_node(const ObjectExport &object);
NativeDecodeResult decode_edgraph_node(Archive &, const Package &, std::size_t export_index,
                                       std::uint64_t end, const ReadOptions &);
} // namespace cyuediff::detail
