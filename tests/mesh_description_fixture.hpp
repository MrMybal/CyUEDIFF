#pragma once
#include <cyuediff/core.hpp>
#include <cstring>
#include <fstream>
namespace cyuediff::test::mesh_fixture {
struct Bytes {
    std::vector<std::uint8_t> data;
    template <class T> void put(T v) {
        auto p = reinterpret_cast<const std::uint8_t *>(&v);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void i(int v) {
        put<std::int32_t>(v);
    }
    void f(float v) {
        put(v);
    }
    void zeros(std::size_t n) {
        data.insert(data.end(), n, 0);
    }
    void string(const std::string &s) {
        i(static_cast<int>(s.size() + 1));
        data.insert(data.end(), s.begin(), s.end());
        data.push_back(0);
    }
    void wide(const std::u16string &s) {
        i(-static_cast<int>(s.size() + 1));
        for (auto c : s)
            put(c);
        put<char16_t>(0);
    }
    template <class T> void patch(std::size_t at, T v) {
        std::memcpy(data.data() + at, &v, sizeof(T));
    }
    void save(const fs::path &path) const {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
};
struct Fixture {
    Bytes bytes;
    std::size_t vertex = 0, vertex_extent = 0, reference = 0, triangle = 0, sparse = 0, numeric = 0, type = 0,
                utf = 0, vertex_attributes = 0, after_vertices = 0;
};
inline Fixture fixture() {
    Fixture f;
    auto &b = f.bytes;
    b.i(7);
    auto element = [&](const char *name, int bits, std::uint32_t mask, int holes, int attributes) {
        b.string(name);
        b.i(1);
        b.i(bits);
        if (bits)
            b.put(mask);
        b.i(holes);
        b.i(bits);
        if (std::string(name) == "Vertices")
            f.vertex_attributes = b.data.size();
        b.i(attributes);
    };
    auto fixed = [&](const char *name, int type, int extent, int elements,
                     const std::vector<std::vector<float>> &floats, const std::vector<int> &integers = {}) {
        b.string(name);
        auto type_at = b.data.size();
        b.i(type);
        b.i(extent);
        b.i(elements);
        b.i(integers.empty() ? static_cast<int>(floats.size()) : 1);
        std::size_t offset = 0;
        int components = type < 3 ? 4 - type : 1;
        auto channel = [&]() {
            b.i(extent);
            b.i(type == 5 ? 1 : components * 4);
            b.i(elements * extent);
            offset = b.data.size();
        };
        if (!integers.empty()) {
            channel();
            for (auto i : integers)
                b.i(i);
        } else
            for (const auto &values : floats) {
                channel();
                for (float v : values)
                    b.f(v);
            }
        if (type < 4)
            for (int i = 0; i < components; ++i)
                b.f(0);
        else
            b.i(-1);
        b.i(0);
        if (std::string(name) == "Position ") {
            f.vertex = offset;
            f.vertex_extent = type_at + 4;
            f.type = type_at;
        }
        return offset;
    };
    element("Vertices", 5, 0x15, 2, 1);
    f.sparse = 4 + 4 + 9 + 4 + 4 + 4;
    fixed("Position ", 1, 1, 5, {{0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0}});
    f.after_vertices = b.data.size();
    element("VertexInstances", 3, 7, 0, 3);
    f.reference = fixed("VertexIndex", 4, 1, 3, {}, {0, 2, 4});
    f.numeric = fixed("Normal", 1, 1, 3, {{0, 0, 1, 0, 0, 1, 0, 0, 1}});
    fixed("TextureCoordinate", 2, 1, 3, {{0, 0, 1, 0, 0, 1}, {.5f, .5f, 1, .5f, .5f, 1}});
    element("Triangles", 3, 4, 2, 3);
    f.triangle = fixed("VertexInstanceIndex", 4, 3, 3, {}, {-1, -1, -1, -1, -1, -1, 0, 1, 2});
    fixed("PolygonIndex", 4, 1, 3, {}, {-1, -1, 7});
    fixed("PolygonGroupIndex", 4, 1, 3, {}, {-1, -1, 3});
    element("Polygons", 8, 128, 7, 0);
    element("PolygonGroups", 4, 8, 3, 1);
    b.string("ImportedMaterialSlotName");
    b.i(6);
    b.i(1);
    b.i(4);
    b.i(1);
    b.i(1);
    b.i(4);
    for (int i = 0; i < 3; ++i)
        b.string("None");
    f.utf = b.data.size() + 4;
    b.wide(u"Matériau");
    b.string("None");
    b.i(32);
    element("Edges", 2, 3, 0, 1);
    b.string("CustomConnections");
    b.i(4);
    b.i(0);
    b.i(2);
    b.i(1);
    b.i(1);
    b.i(3);
    b.i(5);
    b.i(6);
    b.i(7);
    b.i(2);
    b.i(0);
    b.i(1);
    b.i(1);
    b.i(2);
    b.i(1);
    b.i(2);
    b.i(2);
    b.i(-1);
    b.i(-1);
    b.i(0);
    element("UVs", 0, 0, 0, 0);
    return f;
}
inline MeshSource profile() {
    MeshSource s;
    s.object_path = "Mesh.Source";
    s.ue5_version = 1009;
    s.custom_versions = {{"697dd581e64f41abaa4a51ecbeb7b628", 118},
                         {"e4b068edf49442e9a231da0b2e46bb41", 40},
                         {"9c54d522a8264fbe9421074661b482d0", 44},
                         {"9dffbcd6494f0158e22112823c92a888", 10}};
    return s;
}
inline Bytes compressed() {
    Bytes b;
    auto big = [&](std::uint64_t v, unsigned n) {
        for (unsigned i = n; i > 0; --i)
            b.data.push_back(static_cast<std::uint8_t>(v >> ((i - 1) * 8)));
    };
    big(0xb7756362, 4);
    b.zeros(4);
    b.data.push_back(3);
    b.zeros(2);
    b.data.push_back(2);
    big(2, 4);
    big(8, 8);
    big(74, 8);
    b.zeros(32);
    big(1, 4);
    big(1, 4);
    b.data.push_back('a');
    b.data.push_back('b');
    return b;
}
inline void checksum(Bytes &b) {
    std::uint32_t crc = ~0u;
    for (std::size_t i = 8; i < 72; ++i) {
        crc ^= b.data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    crc ^= ~0u;
    for (int i = 0; i < 4; ++i)
        b.data[4 + i] = static_cast<std::uint8_t>(crc >> (24 - 8 * i));
}
} // namespace cyuediff::test::mesh_fixture
