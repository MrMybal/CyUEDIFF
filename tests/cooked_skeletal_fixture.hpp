#pragma once
#include "cooked_texture_fixture.hpp"
namespace cyuediff::test {
inline Bytes skeletal_scripts() {
    Bytes b;
    name_batch(b, {"/Script/Engine", "SkeletalMesh", "Default__SkeletalMesh"});
    b.i(3);
    for (unsigned i = 0; i < 3; ++i) {
        b.put<std::uint32_t>(0x80000000u | i);
        b.i(0);
        b.put(script_root + i);
        b.put(i ? script_root : ~0ull);
        b.put<std::uint64_t>(~0ull);
    }
    return b;
}
inline ZenReadOptions skeletal_context() {
    auto z = texture_context();
    z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(skeletal_scripts().data, z));
    auto c = std::make_shared<ZenSchemaContext>(*z.schema_context);
    c->bindings.clear();
    c->bindings["/Script/Engine.SkeletalMesh"] = {"Collections", 0};
    z.schema_context = c;
    return z;
}
inline Bytes skeletal_color_schema() {
    Bytes payload;
    const std::vector<std::string> names = {"/Script/Engine", "SkeletalMesh", "bHasVertexColors"};
    payload.i(3);
    for (const auto &name : names) {
        payload.put<std::uint8_t>(static_cast<std::uint8_t>(name.size()));
        for (auto c : name)
            payload.put(c);
    }
    payload.i(0);
    payload.i(1);
    payload.i(1);
    payload.i(-1);
    payload.put<std::uint16_t>(1);
    payload.put<std::uint16_t>(1);
    payload.put<std::uint16_t>(0);
    payload.put<std::uint8_t>(1);
    payload.i(2);
    payload.put<std::uint8_t>(1);
    Bytes paths;
    paths.put<std::uint8_t>(0);
    paths.i(0);
    paths.i(1);
    paths.i(0);
    payload.i(0x54584543);
    payload.put<std::uint8_t>(0);
    payload.i(1);
    for (char c : std::string("PPTH"))
        payload.put(c);
    payload.i(static_cast<int>(paths.data.size()));
    payload.append(paths);
    Bytes file;
    file.put<std::uint16_t>(0x30c4);
    file.put<std::uint8_t>(0);
    file.put<std::uint8_t>(0);
    file.i(static_cast<int>(payload.data.size()));
    file.i(static_cast<int>(payload.data.size()));
    file.append(payload);
    return file;
}
struct SkeletalFixture {
    TextureFixture storage;
    std::size_t availability = 0, bulk_index = 0, flags = 0, second_lod = 0;
    std::size_t native = 0, bone_parent = 0, pose = 0, map = 0, section = 0, section_bones = 0, positions = 0,
                indices = 0, weights = 0, lookup = 0;
    std::size_t profile = 0, morph = 0, attributes = 0, size = 0, colors = 0, streaming = 0, inline_count = 0,
                buffer_start = 0, weight_meta = 0;
};
inline SkeletalFixture cooked_skeletal_fixture(bool variable = false, bool wide_bone = false,
                                               bool wide_weight = false, bool wide_indices = false,
                                               bool high = false, bool full = false, bool colors = false,
                                               bool mobile = false) {
    SkeletalFixture f;
    f.storage = cooked_texture_fixture();
    auto &b = f.storage.package;
    b.data.resize(f.storage.body);
    auto strip = [&](unsigned c = 0) {
        b.put<std::uint8_t>(1);
        b.put<std::uint8_t>(static_cast<std::uint8_t>(c));
    };
    auto name = [&](unsigned i) {
        b.i(i);
        b.i(0);
    };
    auto bone_array = [&](std::initializer_list<std::uint16_t> a) {
        b.i(static_cast<int>(a.size()));
        for (auto v : a)
            b.put(v);
    };
    b.put<std::uint16_t>(0x0100);
    f.native = b.data.size();
    b.i(0);
    strip();
    for (double v : {0., 0., 0., 1., 1., 1., 2.})
        b.put(v);
    b.i(1);
    b.i(0);
    name(2);
    b.i(0);
    b.i(1);
    b.i(0);
    for (unsigned i = 0; i < 4; ++i)
        b.put<float>(1);
    b.i(2);
    name(2);
    f.bone_parent = b.data.size();
    b.i(-1);
    name(3);
    b.i(0);
    b.i(2);
    f.pose = b.data.size();
    for (unsigned i = 0; i < 2; ++i)
        for (double v : {0., 0., 0., 1., 0., 0., i ? 2. : 0., 1., 1., 1.})
            b.put(v);
    b.i(2);
    name(2);
    f.map = b.data.size();
    b.i(0);
    name(3);
    b.i(1);
    b.i(1);
    if (mobile)
        b.i(2);
    b.i(1);
    strip();
    b.i(0);
    f.streaming = b.data.size();
    b.i(1);
    bone_array({0, 1});
    b.i(1);
    f.section = b.data.size();
    strip(1);
    b.put<std::uint16_t>(0);
    b.i(0);
    b.i(1);
    b.i(0);
    b.put<std::uint8_t>(3);
    b.i(1);
    b.i(1);
    b.i(0);
    b.i(1);
    b.i(0);
    b.i(2);
    f.section_bones = b.data.size();
    b.put<std::uint16_t>(1);
    b.put<std::uint16_t>(0);
    b.i(3);
    b.i(2);
    b.put<std::int16_t>(-1);
    b.zeros(16);
    b.i(-1);
    b.i(0);
    bone_array({0, 1});
    f.size = b.data.size();
    b.i(0);
    f.buffer_start = b.data.size();
    strip();
    b.put<std::uint8_t>(wide_indices ? 4 : 2);
    b.i(wide_indices ? 4 : 2);
    b.i(3);
    f.indices = b.data.size();
    for (unsigned i = 0; i < 3; ++i)
        if (wide_indices)
            b.put<std::uint32_t>(i);
        else
            b.put<std::uint16_t>(static_cast<std::uint16_t>(i));
    b.i(12);
    b.i(3);
    b.i(12);
    b.i(3);
    f.positions = b.data.size();
    for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
        b.put(v);
    strip();
    b.i(1);
    b.i(3);
    b.i(full);
    b.i(high);
    b.i(high ? 16 : 8);
    b.i(3);
    for (unsigned i = 0; i < 3; ++i)
        for (int v : {1, 0, 0, 0, 0, 0, 1, 1})
            if (high)
                b.put<std::int16_t>(static_cast<std::int16_t>(v * 32767));
            else
                b.put<std::int8_t>(static_cast<std::int8_t>(v * 127));
    b.i(full ? 8 : 4);
    b.i(3);
    for (unsigned v : {0, 0, 1, 0, 0, 1})
        if (full)
            b.put<float>(static_cast<float>(v));
        else
            b.put<std::uint16_t>(v ? 0x3c00 : 0);
    strip();
    f.weight_meta = b.data.size();
    b.i(variable);
    b.i(2);
    b.i(variable ? 4 : 6);
    b.i(3);
    b.i(wide_bone);
    b.i(wide_weight);
    Bytes raw;
    std::vector<std::uint32_t> lookup;
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned n = variable && i != 1 ? 1 : 2;
        lookup.push_back((static_cast<std::uint32_t>(raw.data.size()) << 8) | n);
        for (unsigned j = 0; j < n; ++j) {
            const auto bone = i == 1 ? j : 0;
            if (wide_bone)
                raw.put<std::uint16_t>(static_cast<std::uint16_t>(bone));
            else
                raw.put<std::uint8_t>(static_cast<std::uint8_t>(bone));
        }
        for (unsigned j = 0; j < n; ++j) {
            const auto weight = i == 1 ? (j ? 127 : 128) : (j ? 0 : 255);
            if (wide_weight)
                raw.put<std::uint16_t>(static_cast<std::uint16_t>(weight * 257));
            else
                raw.put<std::uint8_t>(static_cast<std::uint8_t>(weight));
        }
    }
    b.i(1);
    b.i(static_cast<int>(raw.data.size()));
    f.weights = b.data.size();
    b.append(raw);
    strip();
    b.i(variable ? 3 : 0);
    b.i(4);
    b.i(variable ? 3 : 0);
    f.lookup = b.data.size();
    if (variable)
        for (auto v : lookup)
            b.put(v);
    f.colors = b.data.size();
    if (colors) {
        strip();
        b.i(4);
        b.i(3);
        b.i(4);
        b.i(3);
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned v : {12, 34, 56, 255})
                b.put<std::uint8_t>(static_cast<std::uint8_t>(v));
    }
    f.profile = b.data.size();
    b.i(0);
    b.i(3);
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(2);
    b.put<std::uint8_t>(3);
    f.morph = b.data.size();
    b.i(0);
    f.attributes = b.data.size();
    b.i(0);
    b.patch(static_cast<int>(f.size), static_cast<int>(b.data.size() - f.buffer_start));
    f.inline_count = b.data.size();
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(0xa5);
    std::uint32_t exp;
    std::memcpy(&exp, b.data.data() + 32, 4);
    const std::uint64_t export_size = b.data.size() - f.storage.body;
    std::memcpy(b.data.data() + exp + 8, &export_size, 8);
    return f;
}
inline void skeletal_export_size(SkeletalFixture &f) {
    std::uint32_t exp;
    auto &b = f.storage.package;
    std::memcpy(&exp, b.data.data() + 32, 4);
    const std::uint64_t size = b.data.size() - f.storage.body;
    std::memcpy(b.data.data() + exp + 8, &size, 8);
}
// Two LODs: streamed LOD 0 and a separate inline LOD 1, built from original data.
inline SkeletalFixture cooked_skeletal_streaming_fixture(bool external = true, bool variable = false,
                                                         bool wide_bone = false, bool wide_weight = false,
                                                         bool wide_indices = false, bool high = false,
                                                         bool full = false, bool colors = false) {
    auto f = cooked_skeletal_fixture(variable, wide_bone, wide_weight, wide_indices, high, full, colors);
    auto &b = f.storage.package;
    const auto old_start = f.buffer_start;
    const auto lod_start = f.streaming - 6;
    Bytes payload;
    payload.data.assign(b.data.begin() + old_start, b.data.begin() + f.inline_count);
    Bytes inline_lod;
    inline_lod.data.assign(b.data.begin() + lod_start, b.data.begin() + f.inline_count);
    b.data.resize(old_start);
    b.patch(static_cast<int>(f.streaming), 0);
    b.patch(static_cast<int>(f.streaming - 10), 2);
    f.bulk_index = b.data.size();
    b.i(0);
    std::size_t start = 0;
    if (external) {
        f.storage.bulk.zeros(3);
        start = 3;
        f.storage.bulk.append(payload);
    } else {
        start = b.data.size();
        b.append(payload);
    }
    for (auto *field : {&f.positions, &f.indices, &f.weights, &f.lookup, &f.profile, &f.morph, &f.attributes,
                        &f.colors, &f.weight_meta})
        *field = start + (*field - old_start);
    f.buffer_start = start;
    f.availability = b.data.size();
    b.put<std::uint8_t>(wide_indices ? 4 : 2);
    b.i(3);
    b.i(1);
    b.i(3);
    b.i(full);
    b.i(high);
    b.i(12);
    b.i(3);
    b.i(colors ? 4 : 0);
    b.i(colors ? 3 : 0);
    b.i(variable);
    b.i(2);
    b.i(variable ? 4 : 6);
    b.i(3);
    b.i(wide_bone);
    b.i(wide_weight);
    b.i(variable ? 3 : 0);
    b.i(0);
    f.second_lod = b.data.size();
    b.append(inline_lod);
    f.inline_count = b.data.size();
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(2);
    b.put<std::uint8_t>(0xa5);
    const std::int64_t offset = external ? 3 : 0, size = payload.data.size();
    std::memcpy(b.data.data() + f.storage.bulk_row, &offset, 8);
    std::memcpy(b.data.data() + f.storage.bulk_row + 16, &size, 8);
    f.flags = f.storage.bulk_row + 24;
    b.patch(static_cast<int>(f.flags), external ? 0x10501 : 72);
    skeletal_export_size(f);
    return f;
}
} // namespace cyuediff::test
