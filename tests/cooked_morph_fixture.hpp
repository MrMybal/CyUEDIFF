#pragma once
#include "cooked_skeletal_fixture.hpp"
#include "compressed_morph_fixture.hpp"
namespace cyuediff::test {
inline Bytes morph_scripts() {
    Bytes b;
    name_batch(b, {"/Script/Engine", "SkeletalMesh", "Default__SkeletalMesh", "MorphTarget",
                   "Default__MorphTarget"});
    b.i(5);
    for (unsigned i = 0; i < 5; ++i) {
        b.put<std::uint32_t>(0x80000000u | i);
        b.i(0);
        b.put(script_root + i);
        b.put(i ? script_root : ~0ull);
        b.put<std::uint64_t>(~0ull);
    }
    return b;
}
inline Bytes morph_schema() {
    Bytes p;
    const std::vector<std::string> names = {"/Script/Engine", "SkeletalMesh", "MorphTargets"};
    p.i(3);
    for (const auto &n : names) {
        p.put<std::uint8_t>(static_cast<std::uint8_t>(n.size()));
        for (auto c : n)
            p.put(c);
    }
    p.i(0);
    p.i(1);
    p.i(1);
    p.i(-1);
    p.put<std::uint16_t>(1);
    p.put<std::uint16_t>(1);
    p.put<std::uint16_t>(0);
    p.put<std::uint8_t>(1);
    p.i(2);
    p.put<std::uint8_t>(8);
    p.put<std::uint8_t>(4);
    Bytes paths;
    paths.put<std::uint8_t>(0);
    paths.i(0);
    paths.i(1);
    paths.i(0);
    p.i(0x54584543);
    p.put<std::uint8_t>(0);
    p.i(1);
    for (char c : std::string("PPTH"))
        p.put(c);
    p.i(static_cast<int>(paths.data.size()));
    p.append(paths);
    Bytes f;
    f.put<std::uint16_t>(0x30c4);
    f.put<std::uint8_t>(0);
    f.put<std::uint8_t>(0);
    f.i(static_cast<int>(p.data.size()));
    f.i(static_cast<int>(p.data.size()));
    f.append(p);
    return f;
}
inline ZenReadOptions morph_context() {
    auto z = skeletal_context();
    z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(morph_scripts().data, z));
    auto c = std::make_shared<ZenSchemaContext>();
    c->profile = z.profile;
    c->schemas = std::make_shared<UnversionedSchemas>(UnversionedSchemas::parse(morph_schema().data));
    c->bindings["/Script/Engine.SkeletalMesh"] = {"/Script/Engine.SkeletalMesh", 0};
    z.schema_context = c;
    return z;
}
// Original three-vertex cooked skeleton with genuine ObjectProperty-array name
// bindings. Export order deliberately differs from the MorphTargets order.
inline TextureFixture cooked_morph_fixture(const std::vector<std::vector<MorphFixtureDelta>> &targets,
                                           bool bound = true, bool absent = false, bool bad_outer = false,
                                           bool custom = false, bool duplicate = false,
                                           bool streamed = false) {
    auto base = streamed ? cooked_skeletal_streaming_fixture() : cooked_skeletal_fixture();
    auto &payload = streamed ? base.storage.bulk : base.storage.package;
    const auto compressed = compressed_morph_fixture(targets);
    const auto at = base.morph;
    if (!absent) {
        payload.patch(static_cast<int>(at), 1);
        payload.data.insert(payload.data.begin() + at + 4, compressed.data.begin(), compressed.data.end());
        // Streamed fixtures carry two equal LODs; only LOD0 is augmented.
        std::uint32_t size = 0;
        std::memcpy(&size, base.storage.package.data.data() + base.size, 4);
        base.storage.package.patch(static_cast<int>(base.size),
                                   size + static_cast<int>(compressed.data.size()));
        if (streamed) {
            const std::uint64_t bytes = payload.data.size() - 3;
            std::memcpy(base.storage.package.data.data() + base.storage.bulk_row + 16, &bytes, 8);
        }
    }
    Bytes properties;
    properties.put<std::uint16_t>(bound ? 0x0300 : 0x0100);
    if (bound) {
        properties.i(static_cast<int>(targets.size()));
        for (std::size_t i = 0; i < targets.size(); ++i)
            properties.i(static_cast<int>(duplicate ? 2 : targets.size() + 1 - i));
    }
    const auto &old = base.storage.package;
    Bytes body;
    body.append(properties);
    body.data.insert(body.data.end(), old.data.begin() + base.native, old.data.end());
    const auto root_bytes = body.data.size();
    for (std::size_t i = 0; i < targets.size(); ++i)
        body.put<std::uint16_t>(0x0100);
    TextureFixture f;
    f.bulk = base.storage.bulk;
    auto &b = f.package;
    b.zeros(52);
    std::vector<std::string> names = {"None", "/Game/Texture", "Texture", "PF_DXT1"};
    for (std::size_t i = 0; i < targets.size(); ++i)
        names.push_back("Morph/" + std::to_string(i));
    name_batch(b, names);
    b.put<std::uint64_t>(32);
    f.bulk_row = b.data.size();
    b.put<std::int64_t>(streamed ? 3 : 0);
    b.put<std::int64_t>(-1);
    b.put<std::int64_t>(streamed ? f.bulk.data.size() - 3 : 0);
    b.i(streamed ? 0x10501 : 0);
    b.i(0);
    const auto offset = [&](int i) { b.patch(24 + i * 4, static_cast<int>(b.data.size())); };
    offset(0);
    offset(1);
    offset(2);
    for (std::size_t i = 0; i <= targets.size(); ++i) {
        b.put<std::uint64_t>(i ? root_bytes + (i - 1) * 2 : 0);
        b.put<std::uint64_t>(i ? 2 : root_bytes);
        b.i(i ? 4 + static_cast<int>(targets.size() - i) : 2);
        b.i(0);
        b.put<std::uint64_t>(i ? bad_outer ? ~0ull : 0 : ~0ull);
        b.put<std::uint64_t>(i ? custom ? script_class : script_root + 3 : script_class);
        b.put<std::uint64_t>(~0ull);
        b.put<std::uint64_t>(i ? script_root + 4 : script_cdo);
        b.put<std::uint64_t>(i + 1);
        b.i(3);
        b.i(0);
    }
    offset(3);
    for (std::size_t i = 0; i <= targets.size(); ++i) {
        b.i(static_cast<int>(i));
        b.i(0);
    }
    for (std::size_t i = 0; i <= targets.size(); ++i) {
        b.i(static_cast<int>(i));
        b.i(1);
    }
    offset(4);
    for (std::size_t i = 0; i <= targets.size(); ++i) {
        b.i(-1);
        b.zeros(16);
    }
    offset(5);
    offset(6);
    name_batch(b, {});
    f.body = b.data.size();
    b.patch(4, static_cast<int>(f.body));
    b.patch(8, 1);
    b.patch(16, 0x80002200u);
    b.patch(20, 1000);
    b.append(body);
    return f;
}
} // namespace cyuediff::test
