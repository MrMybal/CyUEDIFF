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
inline Fixture rich_curve_file_fixture(int change = 0, int version = 1013) {
    return fixture(
        version, 1, false, 0, {"FloatCurve", "RichCurve", "Keys", "RichCurveKey"},
        [=](Bytes &s, const NameWriter &name) {
            auto tag = [&](Bytes &out, const char *field, const char *kind, const Bytes &payload,
                           const char *structure, bool array = false) {
                name(out, field);
                name(out, kind);
                if (version >= 1012) {
                    out.i(1);
                    if (array) {
                        name(out, "StructProperty");
                        out.i(1);
                    }
                    name(out, structure);
                    out.i(0);
                }
                out.i(static_cast<int>(payload.data.size()));
                if (version >= 1012)
                    out.put<std::uint8_t>(0);
                else {
                    out.i(0);
                    if (array)
                        name(out, "StructProperty");
                    else {
                        name(out, structure);
                        out.zeros(16);
                    }
                    out.put<std::uint8_t>(0);
                    if (version >= 1011)
                        out.put<std::uint8_t>(0);
                }
                out.append(payload);
            };
            Bytes body, array;
            const int n = change == 5 ? 0 : change == 2 ? 4 : 3;
            array.i(n);
            if (version < 1012) {
                Bytes none;
                tag(array, "Keys", "StructProperty", none, "RichCurveKey");
            }
            for (int i = 0; i < n; ++i) {
                const float time = change == 2             ? (i == 0   ? 0.f
                                                              : i == 1 ? .5f
                                                                       : float(i - 1))
                                   : change == 3 && i == 1 ? 0.f
                                   : change == 7           ? (i == 0   ? 1.f
                                                              : i == 1 ? 0.f
                                                                       : 2.f)
                                                           : float(i);
                const float value = change == 2 ? (i == 0 || i == 3 ? 0.f
                                                   : i == 1         ? 4.f
                                                                    : 10.f)
                                    : i == 1    ? (change == 1 ? 12.f : 10.f)
                                                : 0.f;
                auto key = rich_curve_key(time, value, (change == 4 || change == 8) && i == 0 ? 2 : 0);
                if (change == 4 && i == 0)
                    key.data[2] = 2;
                array.append(key);
            }
            if (change != 6)
                tag(body, "Keys", "ArrayProperty", array, "RichCurveKey", true);
            name(body, "None");
            tag(s, "FloatCurve", "StructProperty", body, "RichCurve");
        },
        -1, -1, -1, -1, -1, -1, -1, "CurveFloat", "/Script/Engine");
}
} // namespace cyuediff::test
