#pragma once
#include "cooked_mesh_fixture.hpp"
#include "nanite_geometry_fixture.hpp"
namespace cyuediff::test {
struct CookedNaniteFixture {
    MeshFixture mesh;
    std::size_t native = 0, root = 0, states = 0, hierarchy = 0, roots = 0, dependencies = 0, facts = 0,
                end = 0;
};
inline std::vector<std::uint8_t> nanite_fixed_page(bool streamed) {
    auto page = nanite_page(streamed ? 1 : 0).bytes;
    page.insert(page.begin() + 8, 16, 0);
    page[4] = 1;
    nanite_patch(page, 8, streamed ? 1 : 0);
    nanite_patch(page, 12, streamed ? 1 : 0);
    nanite_patch(page, 20, streamed ? 9 : 0);
    return page;
}
inline void nanite_export_size(CookedNaniteFixture &f) {
    auto &b = f.mesh.storage.package;
    std::uint32_t export_map;
    std::memcpy(&export_map, b.data.data() + 32, 4);
    const std::uint64_t size = b.data.size() - f.mesh.storage.body;
    std::memcpy(b.data.data() + export_map + 8, &size, 8);
}
inline CookedNaniteFixture cooked_nanite_fixture(bool streaming = false, bool external = false,
                                                 bool empty = false, bool unused = false) {
    CookedNaniteFixture f;
    f.mesh = cooked_mesh_fixture();
    auto &storage = f.mesh.storage;
    auto &b = storage.package;
    b.data.resize(f.mesh.inline_count + 1);
    f.native = b.data.size();
    const auto root = empty ? std::vector<std::uint8_t>{} : nanite_fixed_page(false);
    const auto stream = streaming ? nanite_fixed_page(true) : std::vector<std::uint8_t>{};
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(0);
    b.i(0);
    b.i(0);
    storage.bulk.data.clear();
    if (external) {
        storage.bulk.zeros(3);
        storage.bulk.data.insert(storage.bulk.data.end(), stream.begin(), stream.end());
    } else if (!unused)
        b.data.insert(b.data.end(), stream.begin(), stream.end());
    const std::int64_t bulk_size = stream.size(), offset = external ? 3 : 0;
    std::memcpy(b.data.data() + storage.bulk_row, &offset, 8);
    std::memcpy(b.data.data() + storage.bulk_row + 16, &bulk_size, 8);
    b.patch(static_cast<int>(storage.bulk_row + 24), unused ? 32 : external ? 0x10501 : 72);
    b.i(int(root.size()));
    f.root = b.data.size();
    b.data.insert(b.data.end(), root.begin(), root.end());
    b.i(empty ? 0 : streaming ? 2 : 1);
    f.states = b.data.size();
    if (!empty) {
        for (unsigned pi = 0; pi < (streaming ? 2u : 1u); ++pi) {
            const auto size = pi ? stream.size() : root.size();
            b.i(0);
            b.i(int(size));
            b.i(int(size - 24));
            b.i(0);
            b.put<std::uint16_t>(pi ? 1 : 0);
            b.put<std::uint8_t>(pi ? 2 : 1);
            b.put<std::uint8_t>(pi ? 1 : 0);
        }
    }
    b.i(empty ? 0 : 1);
    f.hierarchy = b.data.size();
    if (!empty)
        for (unsigned child = 0; child < 4; ++child) {
            for (float v : {0.f, 0.f, 0.f, 3.f, -1.f, -1.f, 0.f})
                b.put(v);
            b.i(0);
            for (float v : {1.f, 1.f, 0.f})
                b.put(v);
            b.put<std::uint32_t>(0xffffffffu);
            b.put<std::uint32_t>(child == 0 ? 1 : streaming && child == 1 ? 4609 : 0);
        }
    b.i(empty ? 0 : 1);
    f.roots = b.data.size();
    if (!empty)
        b.i(0);
    b.i(streaming ? 1 : 0);
    f.dependencies = b.data.size();
    if (streaming)
        b.i(0);
    b.i(0);
    f.facts = b.data.size();
    b.i(empty ? 0 : 1);
    b.i(0);
    b.i(empty ? 0 : 1);
    b.i(empty ? 0 : streaming ? 2 : 1);
    b.i(empty ? 0 : 3);
    b.put<std::uint16_t>(empty ? 0 : 1);
    b.put<std::uint16_t>(0);
    b.i(empty ? 0 : streaming ? 2 : 1);
    f.end = b.data.size();
    b.put<std::uint8_t>(0xca);
    nanite_export_size(f);
    return f;
}
inline CookedNaniteFixture cooked_nanite_shared_fixture() {
    auto source = cooked_nanite_fixture(true, true);
    auto result = source;
    result.mesh = cooked_mesh_fixture(true, true);
    auto &storage = result.mesh.storage;
    auto &b = storage.package;
    const auto insert = storage.bulk_row + 32;
    b.data.insert(b.data.begin() + static_cast<std::ptrdiff_t>(insert), 32, 0);
    const std::uint64_t table_size = 64;
    std::memcpy(b.data.data() + storage.bulk_row - 8, &table_size, 8);
    for (unsigned at = 24; at <= 48; at += 4) {
        std::uint32_t value;
        std::memcpy(&value, b.data.data() + at, 4);
        value += 32;
        std::memcpy(b.data.data() + at, &value, 4);
    }
    storage.body += 32;
    b.patch(4, static_cast<int>(storage.body));
    result.mesh.inline_count += 32;
    const std::int64_t offset = static_cast<std::int64_t>(storage.bulk.data.size()), duplicate = -1,
                       size = static_cast<std::int64_t>(source.mesh.storage.bulk.data.size() - 3);
    std::memcpy(b.data.data() + insert, &offset, 8);
    std::memcpy(b.data.data() + insert + 8, &duplicate, 8);
    std::memcpy(b.data.data() + insert + 16, &size, 8);
    b.patch(static_cast<int>(insert + 24), 0x10501);
    storage.bulk.data.insert(storage.bulk.data.end(), source.mesh.storage.bulk.data.begin() + 3,
                             source.mesh.storage.bulk.data.end());
    b.data.resize(result.mesh.inline_count + 1);
    const auto start = b.data.size();
    const auto delta = static_cast<std::int64_t>(start) - source.native;
    b.data.insert(b.data.end(),
                  source.mesh.storage.package.data.begin() + static_cast<std::ptrdiff_t>(source.native),
                  source.mesh.storage.package.data.begin() + static_cast<std::ptrdiff_t>(source.end));
    result.native = start;
    result.root = source.root + delta;
    result.states = source.states + delta;
    result.hierarchy = source.hierarchy + delta;
    result.roots = source.roots + delta;
    result.dependencies = source.dependencies + delta;
    result.facts = source.facts + delta;
    result.end = source.end + delta;
    b.patch(static_cast<int>(start + 6), 1);
    b.put<std::uint8_t>(0xca);
    nanite_export_size(result);
    return result;
}
} // namespace cyuediff::test
