#pragma once
#include <cyuediff/zen.hpp>
#include "cooked_texture_fixture.hpp"
namespace cyuediff::test {
inline Bytes collision_scripts() {
    Bytes b;
    name_batch(b, {"/Script/Engine", "BodySetup", "Default__BodySetup"});
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
inline Bytes collision_schemas() {
    Bytes payload;
    std::vector<std::string> names = {"BodySetup", "AggGeom", "KAggregateGeom", "SphereElems", "KSphereElem",
                                      "Name",      "Center",  "Radius",         "Vector"};
    payload.i(static_cast<int>(names.size()));
    for (const auto &n : names) {
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(n.size()));
        payload.data.insert(payload.data.end(), n.begin(), n.end());
    }
    payload.i(0);
    payload.i(4);
    auto schema = [&](int name, int slots) {
        payload.i(name);
        payload.i(-1);
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(slots));
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(slots));
    };
    auto field = [&](unsigned index, int name, unsigned type) {
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(index));
        payload.put<std::uint8_t>(1);
        payload.i(name);
        payload.put<std::uint8_t>(static_cast<std::uint8_t>(type));
    };
    schema(0, 1);
    field(0, 1, 9);
    payload.i(2);
    schema(2, 1);
    field(0, 3, 8);
    payload.put<std::uint8_t>(9);
    payload.i(4);
    schema(4, 3);
    field(0, 5, 5);
    field(1, 6, 9);
    payload.i(8);
    field(2, 7, 3);
    schema(8, 0);
    Bytes out;
    out.put<std::uint16_t>(0x30c4);
    out.put<std::uint8_t>(4);
    out.i(0);
    out.put<std::uint8_t>(0);
    out.i(static_cast<int>(payload.data.size()));
    out.i(static_cast<int>(payload.data.size()));
    out.append(payload);
    return out;
}
struct CookedCollisionFixture {
    TextureFixture storage;
    std::size_t center = 0, radius = 0;
};
inline CookedCollisionFixture cooked_collision_fixture(float radius = 4.25f) {
    CookedCollisionFixture f;
    f.storage = cooked_texture_fixture();
    auto &b = f.storage.package;
    b.data.resize(f.storage.body);
    b.put<std::uint16_t>(0x300);
    b.put<std::uint16_t>(0x300);
    b.i(1);
    b.put<std::uint16_t>(0x700);
    b.i(0);
    b.i(0);
    f.center = b.data.size();
    for (double v : {1., 2., 3.})
        b.put(v);
    f.radius = b.data.size();
    b.put(radius);
    b.zeros(24);
    std::uint32_t exp;
    std::memcpy(&exp, b.data.data() + 32, 4);
    const std::uint64_t size = b.data.size() - f.storage.body;
    std::memcpy(b.data.data() + exp + 8, &size, 8);
    return f;
}
inline ZenReadOptions collision_context() {
    ZenReadOptions z;
    z.profile = ZenProfile::unreal_5_3;
    z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(collision_scripts().data, z));
    auto c = std::make_shared<ZenSchemaContext>();
    c->profile = z.profile;
    c->schemas = std::make_shared<UnversionedSchemas>(UnversionedSchemas::parse(collision_schemas().data));
    c->bindings["/Script/Engine.BodySetup"] = {"BodySetup", 0};
    c->reflected_structs = {"KAggregateGeom", "KSphereElem"};
    c->native_structs["Vector"] = NativeStructLayout::vector3_f64;
    z.schema_context = c;
    return z;
}
inline void write_collision_fixtures(const fs::path &root) {
    fs::create_directories(root);
    auto f = cooked_collision_fixture();
    texture_container(root / "collision", f.storage);
    texture_container(root / "changed", cooked_collision_fixture(8.25f).storage);
    texture_container(root / "negative", cooked_collision_fixture(-1).storage);
    texture_container(root / "corrupt", f.storage, 2, true);
    f.storage.package.patch(static_cast<int>(f.radius), 0x7fc00000);
    texture_container(root / "nan", f.storage);
    collision_schemas().save(root / "native.usmap");
    auto global = collision_scripts();
    global.save(root / "global.ucas");
    zen_toc(global, 5).save(root / "global.utoc");
    Json manifest = {{"format", "cyuediff.zen-schema/2"},
                     {"profile", "ue5.3"},
                     {"usmap", "native.usmap"},
                     {"sha256", sha256_bytes(collision_schemas().data)},
                     {"bindings", {{"/Script/Engine.BodySetup", {{"schema", "BodySetup"}, {"offset", 0}}}}},
                     {"reflectedStructs", {"KAggregateGeom", "KSphereElem"}},
                     {"nativeStructs", {{"Vector", "vector3-f64"}}}};
    std::ofstream(root / "context.json") << manifest.dump(2);
}
} // namespace cyuediff::test
