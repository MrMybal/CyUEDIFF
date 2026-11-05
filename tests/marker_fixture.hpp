#pragma once
#include "curve_fixture.hpp"
namespace cyuediff::test {
inline Fixture marker_file_fixture(int changed = 0, bool reorder = false, int anim = 15, unsigned global = 0,
                                   unsigned specific = 0, int invalid = 0) {
    return curve_file_fixture(0, 0, false, 37, 17, 1013, anim, [&](Bytes &b, const NameWriter &name) {
        if (anim < 2)
            return;
        b.put<std::uint8_t>(global);
        b.put<std::uint8_t>(specific);
        if (global & 1)
            return;
        b.i(invalid == 1 ? -1 : 3);
        for (auto key : reorder ? std::vector<std::string>{"Left", "Left", changed ? "Weight" : "Right"}
                                : std::vector<std::string>{changed ? "Weight" : "Right", "Left", "Left"}) {
            if (invalid == 2) {
                b.i(9999999);
                b.i(0);
            } else
                name(b, key);
        }
    });
}
} // namespace cyuediff::test
