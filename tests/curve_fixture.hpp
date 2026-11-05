#pragma once
#include "retarget_fixture.hpp"
namespace cyuediff::test {
// Stored wire data only: modern SmartName containers, not runtime curve evaluation.
inline Fixture curve_file_fixture(int changed = 0, int invalid = 0, bool reorder = false, int framework = 37,
                                  int anim_phys = 17, int version = 1013, int anim = -1,
                                  const ExtraScript &markers = {}) {
    return retarget_file_fixture(
        0, 0, anim_phys, version, false, false, false, true,
        [&](Bytes &b, const NameWriter &name) {
            b.i(2);
            for (unsigned i = 0; i < 2; ++i) {
                const auto index = reorder ? 1 - i : i;
                name(b, invalid == 1 ? "Track" : index ? "Track" : "AnimCurveMappingName");
                b.i(index ? 0 : 2);
                if (!index)
                    for (unsigned j = 0; j < 2; ++j) {
                        const auto c = reorder ? 1 - j : j;
                        name(b, invalid == 2 ? "Weight" : c ? "Weight" : "Smile~/curve");
                        b.i(invalid == 3 ? 2 : c ? 0 : 1);
                        b.i(c ? 1 : 0);
                        b.i(invalid == 4 ? -1 : 2);
                        name(b, "arm");
                        name(b, "None"); // Preserve unresolved/None links and array order.
                        if (anim_phys >= 12)
                            b.put<std::uint8_t>(c ? 255 : 2 + changed);
                    }
            }
            if (markers)
                markers(b, name);
        },
        framework, anim);
}
} // namespace cyuediff::test
