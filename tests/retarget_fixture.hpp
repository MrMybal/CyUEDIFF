#pragma once
#include "package_fixture.hpp"
namespace cyuediff::test {
// Independently encoded native arrays; no Unreal asset bytes or production writers.
inline Fixture retarget_file_fixture(int changed = 0, int invalid = 0, int anim_phys = 17, int version = 1013,
                                     bool reorder = false, bool mismatch = false, bool filtered = false,
                                     bool empty = false) {
    Bytes class_name, engine_name, payload;
    auto f = fixture(
        version, 1, false, 0,
        {"Skeleton", "/Script/Engine", "root", "arm", "tip", "A~/pose", "B", "DisplayPose",
         "/Game/Source.Source", "/Game/Source", "Source"},
        [&](Bytes &, const NameWriter &name) {
            name(class_name, "Skeleton");
            name(engine_name, "/Script/Engine");
            const std::vector<std::string> bones = reorder ? std::vector<std::string>{"root", "tip", "arm"}
                                                           : std::vector<std::string>{"root", "arm", "tip"};
            auto pose = [&](double x) {
                for (double v : {0., 0., 0., 1., x, 0., 0., 1., 1., 1.})
                    payload.put(v);
            };
            payload.i(0);
            payload.i(3);
            for (unsigned i = 0; i < 3; ++i) {
                name(payload, bones[i]);
                payload.i(i ? 0 : -1);
                if (!filtered)
                    payload.string(bones[i]);
            }
            payload.i(3);
            for (const auto &bone : bones)
                pose(bone == "arm" ? 10. : bone == "tip" ? 20. : 0.);
            payload.i(3);
            for (unsigned i = 0; i < 3; ++i) {
                name(payload, bones[i]);
                payload.i(i);
            }
            payload.i(empty ? 0 : 2);
            if (!empty)
                for (unsigned i = 0; i < 2; ++i) {
                    const auto index = reorder ? 1 - i : i;
                    name(payload, invalid == 1 ? "B" : index ? "B" : "A~/pose");
                    name(payload, "DisplayPose");
                    payload.i(mismatch ? 2 : 3);
                    for (unsigned j = 0; j < (mismatch ? 2u : 3u); ++j)
                        pose(bones[j] == "arm" ? 15. + (!index ? changed : 0) : bones[j] == "tip" ? 25. : 0.);
                    if (!filtered) {
                        if (anim_phys < 15)
                            payload.i(invalid == 2 ? 99 : 0);
                        else {
                            name(payload, version >= 1007 ? "/Game/Source" : "/Game/Source.Source");
                            if (version >= 1007)
                                name(payload, "Source");
                            payload.string("Component");
                        }
                    }
                }
            for (unsigned word : {0x12345678u, 2u, 3u, 4u})
                payload.put(word);
        },
        -1, -1, -1, -1, anim_phys);
    f.script.data.resize(f.script.data.size() - 4);
    f.script.append(payload);
    f.script.put<std::uint32_t>(0xdeadbeef); // Deliberately opaque SmartName/marker suffix.
    const auto import = f.export_offset - 40;
    std::copy(class_name.data.begin(), class_name.data.end(), f.header.data.begin() + import + 20);
    std::copy(engine_name.data.begin(), engine_name.data.end(), f.header.data.begin() + import + 28);
    const auto size = std::int64_t(f.script.data.size());
    std::memcpy(f.header.data.data() + f.export_offset + 28, &size, 8);
    return f;
}
} // namespace cyuediff::test
