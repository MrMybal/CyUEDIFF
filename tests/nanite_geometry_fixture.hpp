#pragma once
#include <cyuediff/nanite_geometry.hpp>
namespace cyuediff::test {
inline void nanite_patch(std::vector<std::uint8_t> &b, std::size_t offset, std::uint32_t n) {
    for (unsigned k = 0; k < 4; ++k)
        b.at(offset + k) = std::uint8_t(n >> (8 * k));
}
inline NanitePageSource nanite_page(unsigned reference_mode = 0) {
    // Original one-triangle cluster. mode 1: all vertices reference a parent; mode 2: local coded vertex.
    NanitePageSource out;
    const unsigned refs = reference_mode == 1 ? 3 : reference_mode == 2 ? 1 : 0;
    const unsigned map = 240, mask = map + (refs ? 4 : 0), ref = mask + 32, pos = (ref + 2 * refs + 3) & ~3u;
    const unsigned attr = pos + (refs == 3 ? 0 : 4), end = attr + (refs == 3 ? 0 : 4);
    out.bytes.resize(8 + end);
    out.bytes[0] = 0x4e;
    out.bytes[1] = 0x46;
    out.bytes[2] = 1;
    auto put = [&](unsigned at, std::uint32_t v) { nanite_patch(out.bytes, 8 + at, v); };
    const std::uint32_t header[] = {140, 1, 8, 0, refs, 192, 192, mask};
    for (unsigned i = 0; i < 8; ++i)
        put(4 * i, header[i]);
    const std::uint32_t disk[] = {192, map, ref, pos, attr, refs, 0, 0};
    for (unsigned i = 0; i < 8; ++i)
        put(32 + 4 * i, disk[i]);
    put(64, 1);
    const unsigned cluster = 80;
    put(cluster, 3 | (132u << 9));
    put(cluster + 4, 1 | (128u << 8));
    put(cluster + 16, 0xfffffffe);
    put(cluster + 20, 0xfffffffe);
    put(cluster + 28, 2 | (8u << 4) | (2u << 9) | (2u << 14) | (1u << 24));
    put(cluster + 76, 4);
    put(cluster + 80, 136 | (2u << 22));
    put(cluster + 84, 128);
    put(cluster + 92, 5);
    put(192, 1);
    if (refs) {
        put(map, reference_mode == 1 ? 256 : 0);
        put(mask, reference_mode == 1 ? 7 : 1);
        for (unsigned i = 0; i < refs; ++i)
            out.bytes[8 + ref + refs + i] = std::uint8_t(reference_mode == 1 ? i : 0);
    }
    if (refs != 3) {
        out.bytes[8 + pos] = reference_mode == 2 ? 2 : 0;
        out.bytes[8 + pos + 1] = reference_mode == 2 ? 8 : 2;
        if (!refs)
            out.bytes[8 + pos + 2] = 8;
    }
    if (reference_mode == 1) {
        out.dependencies = {0};
        out.flags = 1;
    }
    return out;
}
inline NaniteGeometrySource nanite_source() {
    NaniteGeometrySource s;
    s.root_pages = 1;
    s.pages.push_back(nanite_page());
    return s;
}
} // namespace cyuediff::test
