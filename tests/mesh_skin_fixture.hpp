#pragma once
#include "mesh_description_fixture.hpp"
#include "package_fixture.hpp"
#include <cyuediff/content_hash.hpp>
#include <cyuediff/virtualized.hpp>
namespace cyuediff::test::skin_fixture {
using Bytes = mesh_fixture::Bytes;
struct Fixture {
    Bytes bytes;
    std::size_t weight = 0, parent = 0, pose = 0, ranges = 0;
};
inline Fixture fixture(bool changed = false, bool wide = true, unsigned vertex_count = 5) {
    auto base = mesh_fixture::fixture();
    Fixture out;
    Bytes weights;
    auto attribute = [&](const char *name) {
        weights.string(name);
        weights.i(4);
        weights.i(0);
        weights.i(vertex_count);
        weights.i(1);
        const auto chunks = (vertex_count + 255) / 256;
        weights.i(chunks);
        for (unsigned chunk = 0; chunk < chunks; ++chunk) {
            const auto n = std::min(256u, vertex_count - chunk * 256);
            weights.i(n * 2);
            for (unsigned i = 0; i < n; ++i) {
                if (!chunk && !i && std::string(name) == "SkinWeights")
                    out.weight = weights.data.size();
                weights.put<std::uint32_t>(changed && !chunk && !i ? 32767 : 32768);
                weights.put<std::uint32_t>((2u << 16) | (changed && !chunk && !i ? 32768 : 32767));
            }
            weights.i(n);
            for (unsigned i = 0; i < n; ++i)
                weights.i(i * 2);
            if (!chunk && std::string(name) == "SkinWeights")
                out.ranges = weights.data.size();
            for (unsigned i = 0; i < n; ++i)
                weights.i(2);
            for (unsigned i = 0; i < n; ++i)
                weights.i(2);
        }
        weights.i(vertex_count);
        weights.i(0);
        weights.i(0);
        weights.i(0);
    };
    attribute("SkinWeights");
    attribute("SkinWeights-Cloth");
    base.bytes.patch<int>(base.vertex_attributes, 3);
    if (vertex_count != 5) {
        if (vertex_count != 257)
            throw std::runtime_error("Fixture supports 5 or 257 vertex slots");
        // Place a live vertex in the second storage chunk, with sparse slots between it and the triangle.
        base.bytes.patch<int>(base.vertex_attributes - 16, 257);
        base.bytes.data.insert(base.bytes.data.begin() + base.vertex_attributes - 8, 32, 0);
        base.bytes.patch<std::uint32_t>(base.vertex_attributes - 12 + 32, 1);
        base.vertex_attributes += 32;
        base.type += 32;
        base.vertex += 32;
        base.after_vertices += 32;
        base.bytes.patch<int>(base.vertex_attributes - 8, 253);
        const auto at = base.vertex_attributes - 4;
        base.bytes.patch<int>(at, static_cast<int>(vertex_count));
        // Position's element/count fields and its storage must match the expanded attribute space.
        const auto position_type = base.type;
        base.bytes.patch<int>(position_type + 8, vertex_count);
        base.bytes.patch<int>(position_type + 24, vertex_count);
        const auto extra = (vertex_count - 5) * 12;
        base.bytes.data.insert(base.bytes.data.begin() + base.vertex + 5 * 12, extra, 0);
        base.after_vertices += extra;
    }
    out.weight += base.after_vertices;
    out.ranges += base.after_vertices;
    base.bytes.data.insert(base.bytes.data.begin() + base.after_vertices, weights.data.begin(),
                           weights.data.end());
    base.bytes.patch<int>(0, 8);
    out.bytes = std::move(base.bytes);
    auto &b = out.bytes;
    b.string("BonesElementName");
    b.i(1);
    b.i(3);
    b.put<std::uint32_t>(5);
    b.i(1);
    b.i(3);
    b.i(3);
    b.string("Name");
    b.i(6);
    b.i(1);
    b.i(3);
    b.i(1);
    b.i(1);
    b.i(3);
    b.string("root");
    b.string("None");
    b.string("arm");
    b.string("None");
    b.i(32);
    b.string("ParentIndex");
    b.i(4);
    b.i(1);
    b.i(3);
    b.i(1);
    b.i(1);
    b.i(4);
    b.i(3);
    out.parent = b.data.size();
    b.i(-1);
    b.i(-1);
    b.i(0);
    b.i(-1);
    b.i(32);
    b.string("Pose");
    b.i(7);
    b.i(1);
    b.i(3);
    b.i(1);
    b.i(1);
    b.i(3);
    out.pose = b.data.size();
    for (unsigned bone = 0; bone < 4; ++bone)
        for (const double v : {0., 0., 0., 1., bone == 2 ? 10. : 0., 0., 0., 1., 1., 1.}) {
            if (wide)
                b.put<double>(v);
            else
                b.put<float>(static_cast<float>(v));
        }
    b.i(32);
    return out;
}
inline MeshSource profile(bool wide = true) {
    auto source = mesh_fixture::profile();
    source.ue5_version = wide ? 1013 : 1003;
    source.custom_versions["601d1886ac644f84aa16d3de0deac7d6"] = 170;
    source.custom_versions["697dd581e64f41abaa4a51ecbeb7b628"] = 119;
    source.custom_versions["9c54d522a8264fbe9421074661b482d0"] = 51;
    return source;
}
inline Bytes buffer(const std::vector<std::uint8_t> &raw) {
    Bytes b;
    auto big = [&](std::uint64_t v, unsigned n) {
        for (unsigned i = n; i > 0; --i)
            b.data.push_back(std::uint8_t(v >> ((i - 1) * 8)));
    };
    big(0xb7756362, 4);
    b.zeros(4);
    b.zeros(4);
    big(1, 4);
    big(raw.size(), 8);
    big(raw.size() + 64, 8);
    const auto hash = blake3_bytes(raw);
    for (unsigned i = 0; i < 32; ++i)
        b.data.push_back(std::uint8_t(std::stoul(hash.substr(i * 2, 2), nullptr, 16)));
    std::uint32_t crc = ~0u;
    for (std::size_t i = 8; i < 64; ++i) {
        crc ^= b.data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i)
        b.data[4 + i] = std::uint8_t(crc >> (24 - i * 8));
    b.data.insert(b.data.end(), raw.begin(), raw.end());
    return b;
}
inline fs::path package(const fs::path &root, const std::string &name, bool changed = false,
                        bool virtualized = false, const Fixture *fixture_override = nullptr) {
    const auto data = fixture_override ? *fixture_override : fixture(changed);
    const auto hash = blake3_bytes(data.bytes.data);
    auto f = cyuediff::test::fixture(1013, 1, false, 0, {}, {}, 40, -1, 4, 45, -1, -1, -1,
                                     "SkeletalMeshDescriptionBulkData", "/Script/Engine",
                                     {{{0x697dd581, 0xe64f41ab, 0xaa4a51ec, 0xbeb7b628}, 119},
                                      {{0x9c54d522, 0xa8264fbe, 0x94210746, 0x61b482d0}, 51},
                                      {{0x9dffbcd6, 0x494f0158, 0xe2211282, 0x3c92a888}, 10},
                                      {{0x601d1886, 0xac644f84, 0xaa16d3de, 0x0deac7d6}, 170}});
    f.script.data.resize(f.script.data.size() - 4);
    f.script.i(0);
    f.script.i(virtualized ? 1 : 0);
    f.script.zeros(16);
    for (unsigned i = 0; i < 20; ++i)
        f.script.put<std::uint8_t>(std::uint8_t(std::stoul(hash.substr(i * 2, 2), nullptr, 16)));
    f.script.put<std::int64_t>(data.bytes.data.size());
    const auto offset_field = f.script.data.size();
    if (!virtualized)
        f.script.put<std::int64_t>(0);
    f.script.zeros(16);
    f.script.i(1);
    const auto size = std::int64_t(f.script.data.size());
    std::memcpy(f.header.data.data() + f.export_offset + 28, &size, 8);
    if (!virtualized) {
        const auto offset = std::uint64_t(f.header.data.size() + f.script.data.size());
        std::memcpy(f.script.data.data() + offset_field, &offset, 8);
    }
    const auto encoded = buffer(data.bytes.data);
    if (virtualized) {
        const auto file = root / "payloads" / virtualized_payload_relative_path(hash.substr(0, 40));
        fs::create_directories(file.parent_path());
        encoded.save(file);
    } else
        f.script.data.insert(f.script.data.end(), encoded.data.begin(), encoded.data.end());
    fs::create_directories(root);
    return save(root, name, std::move(f));
}
} // namespace cyuediff::test::skin_fixture
