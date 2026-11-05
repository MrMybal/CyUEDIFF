#pragma once
#include "package_fixture.hpp"
#include <limits>
namespace cyuediff::test {
inline Bytes rich_curve_key(float time, float value, int mode = 0, int invalid = 0) {
    Bytes b;
    b.put<std::uint8_t>(mode);
    b.put<std::uint8_t>(invalid == 4 ? 255 : 1);
    b.put<std::uint8_t>(invalid == 5 ? 255 : 0);
    for (float n : {time, value, 2.f, 0.f, -1.f, 0.f})
        b.put<float>(n);
    if (invalid == 1)
        b.data.pop_back();
    if (invalid == 2)
        b.put<std::uint8_t>(0);
    if (invalid == 3) {
        const float n = std::numeric_limits<float>::infinity();
        std::memcpy(b.data.data() + 3, &n, 4);
    }
    return b;
}
inline Fixture rich_key_fixture(int invalid = 0, int version = 1013) {
    return fixture(version, 1, false, 0, {"Key", "SecondKey", "RichCurveKey"},
                   [=](Bytes &s, const NameWriter &name) {
                       auto payload = rich_curve_key(1.25f, 4.5f, 2, invalid);
                       for (const char *field : {"Key", "SecondKey"}) {
                           name(s, field);
                           name(s, "StructProperty");
                           if (version >= 1012) {
                               s.i(1);
                               name(s, "RichCurveKey");
                               s.i(0);
                           }
                           s.i(static_cast<int>(payload.data.size()));
                           if (version >= 1012)
                               s.put<std::uint8_t>(0);
                           else {
                               s.i(0);
                               name(s, "RichCurveKey");
                               s.zeros(16);
                               s.put<std::uint8_t>(0);
                               if (version >= 1011)
                                   s.put<std::uint8_t>(0);
                           }
                           s.append(payload);
                       }
                   });
}
} // namespace cyuediff::test
