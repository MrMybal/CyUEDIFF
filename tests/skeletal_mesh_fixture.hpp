#pragma once
#include "package_fixture.hpp"
namespace cyuediff::test {
inline Fixture skeletal_mesh_fixture(int changed = 0, int invalid = 0, bool imported = true,
                                     bool reorder = false, bool guid = false, int editor = 40, int core = 4,
                                     int rendering = 45, int version = 1013) {
    Bytes class_name, engine_name, payload;
    auto f = fixture(
        version, 1, false, 0,
        {"SkeletalMesh", "/Script/Engine", "root", "arm", "tip", "Body", "ImportedBody"},
        [&](Bytes &, const NameWriter &name) {
            name(class_name, "SkeletalMesh");
            name(engine_name, "/Script/Engine");
            payload.i(guid ? 1 : 0);
            if (guid)
                for (unsigned word : {1u, 2u, 3u, 4u})
                    payload.put(word);
            payload.put<std::uint8_t>(invalid == 1 ? 4 : 0);
            payload.put<std::uint8_t>(0);
            for (double x : {0., 0., 0., 50., 50., 50., 86.6})
                payload.put(x);
            payload.i(1);
            payload.i(invalid == 2 ? 99 : 0);
            name(payload, "Body");
            payload.i(invalid == 3 ? 2 : imported ? 1 : 0);
            if (imported)
                name(payload, "ImportedBody");
            payload.i(1);
            payload.i(0);
            for (float x : {1.f, 2.f, 3.f, 4.f})
                payload.put(x);
            const std::vector<std::string> bones = reorder ? std::vector<std::string>{"root", "tip", "arm"}
                                                           : std::vector<std::string>{"root", "arm", "tip"};
            payload.i(3);
            for (unsigned i = 0; i < 3; ++i) {
                name(payload, bones[i]);
                payload.i(i ? 0 : -1);
                payload.string(bones[i]);
            }
            payload.i(3);
            for (unsigned i = 0; i < 3; ++i) {
                for (double x : {0., 0., 0., 1.})
                    payload.put(x);
                const double t = bones[i] == "arm" ? 10. + changed : bones[i] == "tip" ? 20. : 0.;
                for (double x : {t, 0., 0., 1., 1., 1.})
                    payload.put(x);
            }
            payload.i(3);
            for (unsigned i = 0; i < 3; ++i) {
                name(payload, bones[i]);
                payload.i(i);
            }
        },
        editor, -1, core, rendering);
    f.script.data.resize(f.script.data.size() - 4);
    f.script.append(payload);
    f.script.put<std::uint32_t>(0x12345678);
    const auto import = f.export_offset - 40;
    std::copy(class_name.data.begin(), class_name.data.end(), f.header.data.begin() + import + 20);
    std::copy(engine_name.data.begin(), engine_name.data.end(), f.header.data.begin() + import + 28);
    const auto size = std::int64_t(f.script.data.size());
    std::memcpy(f.header.data.data() + f.export_offset + 28, &size, 8);
    return f;
}
} // namespace cyuediff::test
