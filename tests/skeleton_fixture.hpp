#pragma once
#include "package_fixture.hpp"
namespace cyuediff::test {
inline Fixture skeleton_file_fixture(bool filtered = false, bool reorder = false, int changed = 0,
                                     int invalid = 0) {
    Bytes class_name, engine_name, payload;
    auto f = fixture(
        1013, 1, false, 0, {"Skeleton", "/Script/Engine", "root", "arm", "tip"},
        [&](Bytes &, const NameWriter &name) {
            name(class_name, "Skeleton");
            name(engine_name, "/Script/Engine");
            const std::vector<std::string> bones = reorder ? std::vector<std::string>{"root", "tip", "arm"}
                                                           : std::vector<std::string>{"root", "arm", "tip"};
            payload.i(3);
            for (unsigned i = 0; i < 3; ++i) {
                name(payload, invalid == 1 && i == 2 ? bones[1] : bones[i]);
                payload.i(invalid == 2 && i == 1 ? 2 : i ? 0 : -1);
                if (!filtered)
                    payload.string(bones[i]);
            }
            payload.i(invalid == 3 ? 2 : 3);
            for (unsigned i = 0; i < 3; ++i) {
                for (double x : {0., 0., 0., 1.})
                    payload.put<double>(x);
                const double offset = bones[i] == "arm" ? 10. + changed : bones[i] == "tip" ? 20. : 0.;
                for (double x : {offset, 0., 0., 1., 1., 1.})
                    payload.put<double>(x);
            }
            payload.i(3);
            for (unsigned i = 0; i < 3; ++i) {
                name(payload, bones[i]);
                payload.i(invalid == 4 && i == 1 ? 2 : i);
            }
        });
    // Replace the fixture's four opaque bytes with independently encoded native arrays.
    f.script.data.resize(f.script.data.size() - 4);
    f.script.append(payload);
    f.script.put<std::uint32_t>(0x12345678); // Deliberately unread remaining native state.
    const auto import = f.export_offset - 40;
    std::copy(class_name.data.begin(), class_name.data.end(), f.header.data.begin() + import + 20);
    std::copy(engine_name.data.begin(), engine_name.data.end(), f.header.data.begin() + import + 28);
    const auto size = std::int64_t(f.script.data.size());
    std::memcpy(f.header.data.data() + f.export_offset + 28, &size, 8);
    if (filtered) {
        // Header flags follow the fixed /Game/TestAsset FString.
        const std::string marker = "/Game/TestAsset";
        auto found = std::search(f.header.data.begin(), f.header.data.end(), marker.begin(), marker.end());
        const auto position = std::size_t(found - f.header.data.begin()) + marker.size() + 1;
        f.header.patch(position, int(0x80000000u));
    }
    return f;
}
} // namespace cyuediff::test
