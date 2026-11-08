#pragma once
#include "package_fixture.hpp"
#include <cyuediff/content_hash.hpp>
namespace cyuediff::test {
// Original fixtures use the separately vector-tested BLAKE3 primitive for source hashes.
inline void io_chunk_meta(Bytes &out, const std::vector<std::uint8_t> &source, std::uint8_t flags = 0,
                          unsigned version = 5) {
    const auto hash = blake3_bytes(source);
    auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < 40; i += 2)
        out.put<std::uint8_t>(static_cast<std::uint8_t>((nibble(hash[i]) << 4) | nibble(hash[i + 1])));
    if (version < 8)
        out.zeros(12);
    out.put(flags);
    if (version >= 8)
        out.zeros(3);
}
} // namespace cyuediff::test
