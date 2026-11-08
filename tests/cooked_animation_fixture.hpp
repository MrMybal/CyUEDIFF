#pragma once
#include "cooked_texture_fixture.hpp"
namespace cyuediff::test {
inline Bytes animation_scripts() {
    const std::vector<std::string> names = {"/Script/Engine",
                                            "AnimSequence",
                                            "AnimBoneCompressionSettings",
                                            "AnimCompress_PerTrackCompression",
                                            "Skeleton",
                                            "AnimCompress_BitwiseCompressOnly",
                                            "CustomCodec",
                                            "AnimCurveCompressionSettings",
                                            "AnimCurveCompressionCodec_CompressedRichCurve",
                                            "AnimCurveCompressionCodec_UniformIndexable",
                                            "AnimCurveCompressionCodec_UniformlySampled"};
    Bytes b;
    name_batch(b, names);
    b.i(static_cast<int>(names.size()));
    for (std::size_t i = 0; i < names.size(); ++i) {
        b.put<std::uint32_t>(0x80000000u | std::uint32_t(i));
        b.i(0);
        b.put(script_root + i);
        b.put(i ? script_root : ~0ull);
        b.put<std::uint64_t>(~0ull);
    }
    return b;
}
inline Bytes animation_schema(bool curves = false) {
    const std::vector<std::string> names = {"/Script/Engine",
                                            "AnimationAsset",
                                            "AnimSequence",
                                            "Skeleton",
                                            "BoneCompressionSettings",
                                            "Interpolation",
                                            "AnimBoneCompressionSettings",
                                            "Codecs",
                                            "CurveCompressionSettings",
                                            "AnimCurveCompressionSettings",
                                            "Codec"};
    Bytes p;
    p.i(static_cast<int>(names.size()));
    for (const auto &s : names) {
        p.put<std::uint8_t>(std::uint8_t(s.size()));
        for (auto c : s)
            p.put(c);
    }
    p.i(0);
    p.i(5);
    auto schema = [&](int name, int super, unsigned n) {
        p.i(name);
        p.i(super);
        p.put<std::uint16_t>(std::uint16_t(n));
        p.put<std::uint16_t>(std::uint16_t(n));
    };
    auto field = [&](unsigned index, int name, unsigned kind) {
        p.put<std::uint16_t>(std::uint16_t(index));
        p.put<std::uint8_t>(1);
        p.i(name);
        p.put<std::uint8_t>(std::uint8_t(kind));
    };
    schema(1, -1, 1);
    field(0, 3, 4);
    schema(2, 1, curves ? 3 : 2);
    field(0, 4, 4);
    field(1, 5, 0);
    if (curves)
        field(2, 8, 4);
    schema(3, -1, 0);
    schema(6, -1, 1);
    field(0, 7, 8);
    p.put<std::uint8_t>(4);
    schema(9, -1, 1);
    field(0, 10, 4);
    Bytes paths;
    paths.put<std::uint8_t>(0);
    paths.i(0);
    paths.i(5);
    for (unsigned i = 0; i < 5; ++i)
        paths.i(0);
    p.i(0x54584543);
    p.put<std::uint8_t>(0);
    p.i(1);
    for (auto c : std::string("PPTH"))
        p.put(c);
    p.i(static_cast<int>(paths.data.size()));
    p.append(paths);
    Bytes b;
    b.put<std::uint16_t>(0x30c4);
    b.put<std::uint8_t>(0);
    b.put<std::uint8_t>(0);
    b.i(static_cast<int>(p.data.size()));
    b.i(static_cast<int>(p.data.size()));
    b.append(p);
    return b;
}
inline ZenReadOptions animation_context(bool curves = false) {
    ZenReadOptions z;
    z.profile = ZenProfile::unreal_5_3;
    z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(animation_scripts().data, z));
    auto c = std::make_shared<ZenSchemaContext>();
    c->profile = z.profile;
    c->schemas =
        std::make_shared<UnversionedSchemas>(UnversionedSchemas::parse(animation_schema(curves).data));
    for (const auto &name :
         {"AnimSequence", "Skeleton", "AnimBoneCompressionSettings", "AnimCurveCompressionSettings"}) {
        auto s = "/Script/Engine." + std::string(name);
        c->bindings[s] = {s, 0};
    }
    z.schema_context = c;
    return z;
}
struct AnimationExport {
    std::uint32_t name = 2, class_number = 1, flags = 3;
    std::uint64_t outer = ~0ull, public_hash = 1;
    Bytes body;
};
struct AnimationPackage {
    TextureFixture storage;
    std::size_t exports = 0;
};
inline AnimationPackage animation_package(std::vector<std::string> names,
                                          std::vector<AnimationExport> exports,
                                          const std::vector<std::uint64_t> &imports = {},
                                          const std::vector<std::uint64_t> &hashes = {},
                                          const std::vector<std::string> &imported = {},
                                          std::uint64_t bulk_size = 0, unsigned bulk_flags = 72) {
    AnimationPackage out;
    auto &b = out.storage.package;
    b.zeros(52);
    name_batch(b, names);
    b.put<std::uint64_t>(32);
    out.storage.bulk_row = b.data.size();
    b.put<std::int64_t>(bulk_flags & 1 ? 3 : 0);
    b.put<std::int64_t>(-1);
    b.put<std::int64_t>(std::int64_t(bulk_size));
    b.i(bulk_flags);
    b.i(0);
    auto offset = [&](unsigned n) { b.patch(24 + n * 4, static_cast<int>(b.data.size())); };
    offset(0);
    for (auto v : hashes)
        b.put(v);
    offset(1);
    for (auto v : imports)
        b.put(v);
    offset(2);
    out.exports = b.data.size();
    std::uint64_t at = 0;
    for (const auto &e : exports) {
        b.put(at);
        b.put<std::uint64_t>(e.body.data.size());
        b.i(int(e.name));
        b.i(0);
        b.put(e.outer);
        b.put(script_root + e.class_number);
        b.put<std::uint64_t>(~0ull);
        b.put<std::uint64_t>(~0ull);
        b.put(e.public_hash);
        b.i(int(e.flags));
        b.i(0);
        at += e.body.data.size();
    }
    offset(3);
    for (unsigned i = 0; i < exports.size(); ++i) {
        b.i(int(i));
        b.i(0);
        b.i(int(i));
        b.i(1);
    }
    offset(4);
    for (std::size_t i = 0; i < exports.size(); ++i) {
        b.i(-1);
        b.zeros(16);
    }
    offset(5);
    offset(6);
    name_batch(b, imported);
    for (std::size_t i = 0; i < imported.size(); ++i)
        b.i(0);
    out.storage.body = b.data.size();
    b.patch(4, int(b.data.size()));
    b.patch(8, 1);
    b.patch(16, 0x80002200u);
    b.patch(20, 1000);
    for (const auto &e : exports)
        b.append(e.body);
    return out;
}
struct AnimationFixture {
    AnimationPackage animation, settings, skeleton;
    std::size_t native = 0, buffer = 0, metadata = 0, track_map = 0, handle = 0, guid = 0,
                skeleton_native = 0;
};
inline AnimationFixture cooked_animation_fixture(bool bulk = false, bool external = false,
                                                 bool stored_interpolation = true, bool nested = false,
                                                 bool curve = false, bool bone_data = true,
                                                 bool no_tracks = false) {
    AnimationFixture f;
    Bytes keys;
    keys.i(0);
    keys.i(-1);
    keys.put<std::uint32_t>(0x11000002);
    keys.put<float>(2);
    keys.put<float>(10);
    if (!bone_data || no_tracks)
        keys.data.clear();
    Bytes body;
    if (stored_interpolation) {
        body.put<std::uint16_t>(0x0700);
        body.i(-1);
        body.put<std::uint8_t>(0);
        body.i(-2);
    } else {
        body.put<std::uint16_t>(0x0200);
        body.put<std::uint16_t>(0x0301);
        body.i(-1);
        body.i(-2);
    }
    const auto begin = body.data.size();
    body.i(0);
    for (unsigned i = 0; i < 16; ++i)
        body.put<std::uint8_t>(std::uint8_t(i + 1));
    body.put<std::uint8_t>(1);
    body.put<std::uint8_t>(0);
    body.i(1);
    body.i(100);
    body.i(bone_data && !no_tracks ? 1 : 0);
    const auto map = body.data.size();
    if (bone_data && !no_tracks)
        body.i(1);
    body.i(curve ? 1 : 0);
    if (curve) {
        body.i(3);
        body.i(0);
    }
    body.i(int(keys.data.size()));
    body.i(bulk ? 1 : 0);
    const auto buffer = body.data.size();
    if (bulk) {
        body.i(0);
        if (!external)
            body.append(keys);
    } else
        body.append(keys);
    const auto handle = body.data.size();
    body.string(!bone_data ? "" : nested ? "Instance_7.Parent" : "Instance_7");
    body.string("/Game/CurveSettings.Settings:Codec");
    body.i(curve ? 3 : 0);
    if (curve)
        for (unsigned v : {4u, 5u, 6u})
            body.put<std::uint8_t>(std::uint8_t(v));
    const auto metadata = body.data.size();
    if (bone_data) {
        body.i(5);
        for (unsigned v : {2u, 6u, 6u, 6u})
            body.put<std::uint8_t>(std::uint8_t(v));
        body.i(no_tracks ? 0 : 12);
        body.i(no_tracks ? 0 : 2);
        body.i(0);
        body.i(2);
    }
    body.i(1);
    f.animation =
        animation_package({"None", "/Game/Animation", "Animation", "CurveOne"}, {{2, 1, 3, ~0ull, 1, body}},
                          {0x8000000000000000ull, 0x8000000100000001ull}, {0xabc, 0xdef},
                          {"/Game/Settings", "/Game/Skeleton"}, keys.data.size(), external ? 0x10501 : 72);
    if (external) {
        f.animation.storage.bulk.zeros(3);
        f.animation.storage.bulk.append(keys);
    }
    f.native = f.animation.storage.body + begin;
    f.buffer = f.animation.storage.body + buffer;
    f.metadata = f.animation.storage.body + metadata;
    f.track_map = f.animation.storage.body + map;
    f.handle = f.animation.storage.body + handle;
    Bytes settings;
    settings.put<std::uint16_t>(0x0300);
    settings.i(1);
    settings.i(nested ? 3 : 2); // registered codec local export
    AnimationExport root{2, 2, 3, ~0ull, 0xabc, settings};
    AnimationExport instance{3, 3, 8, nested ? 1ull : 0ull, 0x100, {}};
    if (nested)
        f.settings = animation_package({"None", "/Game/Settings", "Settings", "Instance_7", "Parent"},
                                       {root, {4, 5, 8, 0, 0x101, {}}, instance});
    else
        f.settings =
            animation_package({"None", "/Game/Settings", "Settings", "Instance_7"}, {root, instance});
    Bytes skel;
    skel.put<std::uint16_t>(0x0100);
    const auto skel_start = skel.data.size();
    skel.i(0);
    skel.i(2);
    skel.i(3);
    skel.i(0);
    skel.i(-1);
    skel.i(4);
    skel.i(0);
    skel.i(0);
    skel.i(2);
    for (unsigned bone = 0; bone < 2; ++bone)
        for (double v : {0., 0., 0., 1., 0., 0., bone ? 2. : 0., 1., 1., 1.})
            skel.put(v);
    skel.i(2);
    for (unsigned bone = 0; bone < 2; ++bone) {
        skel.i(int(bone + 3));
        skel.i(0);
        skel.i(int(bone));
    }
    skel.i(0);
    const auto guid = skel.data.size();
    for (unsigned i = 0; i < 16; ++i)
        skel.put<std::uint8_t>(std::uint8_t(i + 1));
    f.skeleton = animation_package({"None", "/Game/Skeleton", "Skeleton", "Root", "Child"},
                                   {{2, 4, 3, ~0ull, 0xdef, skel}});
    f.guid = f.skeleton.storage.body + guid;
    f.skeleton_native = f.skeleton.storage.body + skel_start;
    return f;
}
inline void save_animation(const fs::path &root, const AnimationFixture &f, unsigned bulk_type = 2,
                           bool corrupt = false) {
    texture_container(root / "animation", f.animation.storage, bulk_type, corrupt);
    texture_container(root / "settings", f.settings.storage);
    texture_container(root / "skeleton", f.skeleton.storage);
}
struct AnimationCurveFixture {
    AnimationFixture animation;
    AnimationPackage curve_settings;
    std::size_t curve_start = 0, curve_count_at = 0, curve_ref = 0;
};
inline AnimationCurveFixture cooked_animation_curve_fixture(unsigned codec = 0, bool curve_only = false) {
    AnimationCurveFixture out;
    out.animation = cooked_animation_fixture(false, false, true, false, true, !curve_only);
    auto &f = out.animation;
    const auto &raw = f.animation.storage.package.data;
    Bytes body;
    const auto native = f.native - f.animation.storage.body;
    body.data.assign(raw.begin() + f.animation.storage.body, raw.begin() + f.native - 4);
    body.data[0] = 0;
    body.data[1] = 9; // three child fields, then the inherited Skeleton field
    body.i(-3);
    body.data.insert(body.data.end(), raw.begin() + f.native - 4, raw.begin() + f.native);
    const auto curve_count_at = f.metadata - 7;
    body.data.insert(body.data.end(), raw.begin() + f.native, raw.begin() + curve_count_at);
    Bytes curves;
    if (codec == 0) {
        for (unsigned v : {2u, 1u, 4u, 4u})
            curves.put<std::uint8_t>(std::uint8_t(v));
        curves.i(2);
        curves.i(12);
        curves.put<float>(0);
        curves.put<float>(1);
        curves.put<float>(1);
        curves.put<float>(5);
    } else {
        if (codec == 2)
            curves.i(0);
        curves.i(2);
        curves.put<float>(1);
        if (codec == 2)
            curves.i(0);
        curves.put<float>(1);
        curves.put<float>(5);
    }
    const auto count_position = body.data.size();
    body.i(int(curves.data.size()));
    const auto data_position = body.data.size();
    body.append(curves);
    body.data.insert(body.data.end(), raw.begin() + f.metadata, raw.end());
    f.animation =
        animation_package({"None", "/Game/Animation", "Animation", "CurveOne"}, {{2, 1, 3, ~0ull, 1, body}},
                          {0x8000000000000000ull, 0x8000000100000001ull, 0x8000000200000002ull},
                          {0xabc, 0xdef, 0xaaa}, {"/Game/Settings", "/Game/Skeleton", "/Game/CurveSettings"});
    f.native = f.animation.storage.body + native + 4;
    out.curve_ref = f.animation.storage.body + native - 4;
    out.curve_start = f.animation.storage.body + data_position;
    out.curve_count_at = f.animation.storage.body + count_position;
    Bytes settings;
    settings.put<std::uint16_t>(0x0300);
    settings.i(3);
    out.curve_settings =
        animation_package({"None", "/Game/CurveSettings", "CurveSettings", "Unused", "ArbitraryInstance"},
                          {{2, 7, 3, ~0ull, 0xaaa, settings},
                           {3, codec == 0 ? 9u : 8u, 8, 0, 0, {}},
                           {4, 8 + codec, 8, 0, 0, {}}});
    return out;
}
inline void save_animation_curves(const fs::path &root, const AnimationCurveFixture &f) {
    save_animation(root, f.animation);
    texture_container(root / "curve-settings", f.curve_settings.storage);
}
} // namespace cyuediff::test
