#pragma once
#include "cooked_texture_fixture.hpp"
namespace cyuediff::test {
inline Bytes mesh_scripts() {
    Bytes b;
    name_batch(b, {"/Script/Engine", "StaticMesh", "Default__StaticMesh"});
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
inline ZenReadOptions mesh_context() {
    auto z = texture_context();
    z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(mesh_scripts().data, z));
    auto c = std::make_shared<ZenSchemaContext>(*z.schema_context);
    c->bindings.clear();
    c->bindings["/Script/Engine.StaticMesh"] = {"Collections", 0};
    z.schema_context = c;
    return z;
}
struct MeshFixture {
    TextureFixture storage;
    std::size_t positions = 0, indices = 0, stride = 0, section = 0, inline_count = 0, flags = 0, tangent = 0,
                uv = 0;
    bool external = false, streaming = false;
};
inline MeshFixture cooked_mesh_fixture(bool streaming = false, bool external = false, bool wide = false,
                                       bool high = false, bool full = false, bool colors = false,
                                       bool mobile = false) {
    MeshFixture f;
    f.streaming = streaming;
    f.external = external;
    f.storage = cooked_texture_fixture();
    auto &b = f.storage.package;
    b.data.resize(f.storage.body);
    b.put<std::uint16_t>(0x0100);
    b.i(0);
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(0);
    b.i(1);
    b.i(0);
    b.i(0);
    b.zeros(16);
    b.i(0);
    if (mobile)
        b.i(2);
    b.i(1);
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(8);
    b.i(1);
    f.section = b.data.size();
    for (unsigned v : {0, 0, 1, 0, 2, 1, 1, 0, 1, 1})
        b.put<std::uint32_t>(v);
    b.put<float>(0);
    b.i(0);
    b.i(streaming ? 0 : 1);
    Bytes payload;
    payload.put<std::uint8_t>(1);
    payload.put<std::uint8_t>(8);
    payload.i(12);
    payload.i(3);
    f.stride = payload.data.size();
    payload.i(12);
    payload.i(3);
    f.positions = payload.data.size();
    for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
        payload.put(v);
    payload.put<std::uint8_t>(1);
    payload.put<std::uint8_t>(0);
    payload.i(1);
    payload.i(3);
    payload.i(full);
    payload.i(high);
    payload.i(high ? 16 : 8);
    payload.i(3);
    f.tangent = payload.data.size();
    for (unsigned i = 0; i < 3; ++i)
        for (int v : {1, 0, 0, 0, 0, 0, 1, 1}) {
            if (high)
                payload.put<std::int16_t>(static_cast<std::int16_t>(v * 32767));
            else
                payload.put<std::int8_t>(static_cast<std::int8_t>(v * 127));
        }
    payload.i(full ? 8 : 4);
    payload.i(3);
    f.uv = payload.data.size();
    for (unsigned v : {0, 0, 1, 0, 0, 1}) {
        if (full)
            payload.put<float>(static_cast<float>(v));
        else
            payload.put<std::uint16_t>(v ? 0x3c00 : 0);
    }
    payload.put<std::uint8_t>(1);
    payload.put<std::uint8_t>(0);
    payload.i(colors ? 4 : 0);
    payload.i(colors ? 3 : 0);
    if (colors) {
        payload.i(4);
        payload.i(3);
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned v : {12, 34, 56, 255})
                payload.put<std::uint8_t>(static_cast<std::uint8_t>(v));
    }
    for (unsigned i = 0; i < 4; ++i) {
        payload.i(wide);
        payload.i(1);
        payload.i(wide ? 12 : 6);
        if (!i)
            f.indices = payload.data.size();
        for (unsigned v : {0, 1, 2}) {
            if (wide)
                payload.put<std::uint32_t>(v);
            else
                payload.put<std::uint16_t>(static_cast<std::uint16_t>(v));
        }
        payload.i(0);
    }
    for (unsigned i = 0; i < 2; ++i) {
        payload.i(0);
        payload.i(0);
        payload.put<float>(0);
    }
    const auto size = 36 + (high ? 48 : 24) + (full ? 24 : 12) + (colors ? 12 : 0) + (wide ? 48 : 24);
    std::uint64_t shift = 0;
    if (streaming) {
        b.i(0);
        if (external) {
            f.storage.bulk.zeros(3);
            f.storage.bulk.append(payload);
            shift = 3;
        } else {
            shift = b.data.size();
            b.append(payload);
        }
        b.i(1);
        b.i(14 | (colors ? 16 : 0));
        b.i(1);
        b.i(3);
        b.i(full);
        b.i(high);
        b.i(12);
        b.i(3);
        b.i(colors ? 4 : 0);
        b.i(colors ? 3 : 0);
        for (unsigned i = 0; i < 5; ++i) {
            b.i(i == 4 ? 0 : 3);
            b.i(i == 4 ? 0 : wide);
        }
    } else {
        shift = b.data.size();
        b.append(payload);
    }
    f.positions += shift;
    f.indices += shift;
    f.stride += shift;
    f.tangent += shift;
    f.uv += shift;
    b.i(size);
    b.i(wide ? 12 : 6);
    b.i(wide ? 24 : 12);
    f.inline_count = b.data.size();
    b.put<std::uint8_t>(streaming ? 0 : 1);
    b.put<std::uint8_t>(0xa5); // Deliberately opaque later native data.
    const std::int64_t bulk_size = payload.data.size(), offset = external ? 3 : 0;
    std::memcpy(b.data.data() + f.storage.bulk_row, &offset, 8);
    std::memcpy(b.data.data() + f.storage.bulk_row + 16, &bulk_size, 8);
    f.flags = f.storage.bulk_row + 24;
    b.patch(static_cast<int>(f.flags), external ? 0x10501 : 72);
    std::uint32_t exp;
    std::memcpy(&exp, b.data.data() + 32, 4);
    const std::uint64_t export_size = b.data.size() - f.storage.body;
    std::memcpy(b.data.data() + exp + 8, &export_size, 8);
    return f;
}
} // namespace cyuediff::test
