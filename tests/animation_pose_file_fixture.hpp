#pragma once
#include "package_fixture.hpp"
namespace cyuediff::test {
inline Fixture animation_skeleton_file_fixture(int variant = 0) {
    Bytes class_name, engine_name, payload;
    auto f = fixture(1013, 1, false, 0, {"Skeleton", "/Script/Engine", "root", "arm~/track", "other", "tip"},
                     [&](Bytes &, const NameWriter &name) {
                         name(class_name, "Skeleton");
                         name(engine_name, "/Script/Engine");
                         const std::vector<std::string> bones =
                             variant == 3
                                 ? std::vector<std::string>{"root", "arm~/track", "tip"}
                                 : std::vector<std::string>{"root", variant == 1 ? "other" : "arm~/track"};
                         payload.i(0);
                         payload.i(static_cast<int>(bones.size()));
                         for (std::size_t i = 0; i < bones.size(); ++i) {
                             name(payload, bones[i]);
                             payload.i(i == 0 || variant == 2 ? -1 : 0);
                             payload.string(bones[i]);
                         }
                         payload.i(static_cast<int>(bones.size()));
                         for (std::size_t i = 0; i < bones.size(); ++i) {
                             for (double x : {0., 0., 0., 1.})
                                 payload.put<double>(x);
                             for (double x : {variant == 4 ? 999. : 0., 0., 0., 1., 1., 1.})
                                 payload.put<double>(x);
                         }
                         payload.i(static_cast<int>(bones.size()));
                         for (std::size_t i = 0; i < bones.size(); ++i) {
                             name(payload, bones[i]);
                             payload.i(static_cast<int>(i));
                         }
                     });
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
