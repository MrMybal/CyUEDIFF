#pragma once
#include "cooked_collision_fixture.hpp"
#include <map>
namespace cyuediff::test {
struct PhysicsFixture {
    Bytes blob;
    std::size_t vertex = 0, face = 0, twin = 0, alias = 0, bvh = 0, uv = 0;
};
inline PhysicsFixture physics_fixture(unsigned index_type = 1, bool wide = false) {
    PhysicsFixture f;
    auto &b = f.blob;
    auto values = [&](std::initializer_list<float> a) {
        for (auto v : a)
            b.put(v);
    };
    b.i(4);
    b.i(3);
    b.i(1);
    b.i(5);
    b.put<std::uint8_t>(8);
    b.i(1);
    b.i(1);
    b.put<std::uint8_t>(8);
    b.i(4);
    values({0, 0, 0, 0, 0, -1});
    values({0, 0, 0, 0, -1, 0});
    values({0, 0, 0, -1, 0, 0});
    values({1, 0, 0, 1, 1, 1});
    b.i(4);
    f.vertex = b.data.size();
    values({0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1});
    values({0, 0, 0, 1, 1, 1, 1.f / 6, .25f, .25f, .25f, 0});
    b.put<std::uint8_t>(static_cast<std::uint8_t>(index_type));
    auto put_index = [&](int v) {
        if (index_type == 1)
            b.put<std::uint8_t>(static_cast<std::uint8_t>(v));
        else if (index_type == 2)
            b.put<std::int16_t>(static_cast<std::int16_t>(v));
        else
            b.i(v);
    };
    b.i(4);
    f.face = b.data.size();
    for (int i = 0; i < 4; ++i) {
        put_index(i * 3);
        put_index(3);
    }
    const std::array<std::array<int, 3>, 4> faces = {{{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}}};
    std::map<std::pair<int, int>, int> edges;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j)
            edges[{faces[i][j], faces[i][(j + 1) % 3]}] = i * 3 + j;
    std::vector<int> unique;
    std::array<int, 4> origins{};
    b.i(12);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j) {
            auto from = faces[i][j], to = faces[i][(j + 1) % 3], twin = edges.at({to, from});
            put_index(i);
            put_index(from);
            if (i == 0 && j == 0)
                f.twin = b.data.size();
            put_index(twin);
            origins[from] = i * 3 + j;
            if (i * 3 + j < twin)
                unique.push_back(i * 3 + j);
        }
    b.i(4);
    for (auto e : origins)
        put_index(e);
    b.i(static_cast<int>(unique.size()));
    for (auto e : unique)
        put_index(e);
    values({1, 1, 1});
    for (double v : {0., 0., 0., 1.})
        b.put(v);
    b.i(1);
    f.alias = b.data.size();
    b.i(5);
    b.i(0);
    b.i(1);
    b.i(1);
    b.i(9);
    b.put<std::uint8_t>(11);
    b.i(0);
    b.i(1);
    b.put<std::uint8_t>(11);
    b.i(1);
    b.i(3);
    values({0, 0, 0, 1, 0, 0, 0, 1, 0});
    b.i(wide ? 1 : 0);
    b.i(1);
    for (int i : {0, 1, 2}) {
        if (wide)
            b.i(i);
        else
            b.put<std::uint16_t>(static_cast<std::uint16_t>(i));
    }
    values({0, 0, 0, 1, 1, 0});
    b.i(1);
    values({0, 0, 0, 1, 1, 0});
    f.bvh = b.data.size();
    b.i(0);
    b.i(1);
    values({0, 0, 0, 0, 0, 0});
    b.i(-1);
    b.i(0);
    b.i(1);
    values({0, 0, 0, 1, 1, 0});
    b.i(1);
    b.put<std::uint16_t>(2);
    b.i(1);
    b.i(7);
    b.i(3);
    for (int i : {0, 1, 2})
        b.i(i);
    b.i(3);
    for (int i : {0, 1, 2})
        b.i(i);
    b.i(3);
    for (double v : {0., 0., 0., 1., 0., 0., 0., 1., 0.})
        b.put(v);
    b.i(1);
    b.i(3);
    f.uv = b.data.size();
    for (double v : {0., 0., 1., 0., 0., 1.})
        b.put(v);
    b.i(1);
    b.i(7);
    return f;
}
inline CookedCollisionFixture physics_container_fixture(bool external = false, unsigned flags = 0) {
    auto f = cooked_collision_fixture();
    auto &b = f.storage.package;
    b.data.resize(f.radius + 4);
    b.i(0);
    b.zeros(16);
    b.i(1);
    b.i(1);
    b.i(1);
    b.i(3);
    b.i(0);
    b.i(0);
    const auto physics = physics_fixture().blob;
    if (external) {
        f.storage.bulk.data.clear();
        f.storage.bulk.zeros(3);
        f.storage.bulk.append(physics);
    } else
        b.append(physics);
    std::int64_t size = static_cast<std::int64_t>(physics.data.size());
    std::memcpy(b.data.data() + f.storage.bulk_row + 16, &size, 8);
    std::int64_t offset = external ? 3 : 0;
    std::memcpy(b.data.data() + f.storage.bulk_row, &offset, 8);
    b.patch(static_cast<int>(f.storage.bulk_row + 24), flags      ? static_cast<int>(flags)
                                                       : external ? 0x101
                                                                  : 72);
    std::uint32_t exp;
    std::memcpy(&exp, b.data.data() + 32, 4);
    const auto bytes = std::uint64_t(b.data.size() - f.storage.body);
    std::memcpy(b.data.data() + exp + 8, &bytes, 8);
    return f;
}
} // namespace cyuediff::test
