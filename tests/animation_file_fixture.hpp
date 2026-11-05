#pragma once
#include "package_fixture.hpp"
namespace cyuediff::test {
inline Fixture animation_key_fixture(int invalid = 0, int version = 1013, int release = 39) {
    return fixture(
        version, 1, false, 0, {"Track", "RawAnimSequenceTrack"},
        [=](Bytes &s, const NameWriter &name) {
            Bytes keys;
            for (unsigned channel = 0; channel < 3; ++channel) {
                const unsigned width = channel == 1 ? 4 : 3;
                keys.i(invalid == 1 && channel == 0 ? 16 : width * 4);
                keys.i(invalid == 2 && channel == 0 ? -1 : 2);
                for (unsigned key = 0; key < 2; ++key)
                    for (unsigned c = 0; c < width; ++c)
                        keys.put<float>(invalid == 3 && channel == 0 && c == 0
                                            ? std::numeric_limits<float>::infinity()
                                        : channel == 1 ? (c == 3 ? 1.f : 0.f)
                                        : channel == 2 ? 1.f
                                                       : float(key * 10 + c));
            }
            if (invalid == 4)
                keys.data.pop_back();
            if (invalid == 5)
                keys.i(123);
            name(s, "Track");
            name(s, "StructProperty");
            if (version >= 1012) {
                s.i(1);
                name(s, "RawAnimSequenceTrack");
                s.i(0);
            }
            s.i(static_cast<int>(keys.data.size()));
            if (version >= 1012)
                s.put<std::uint8_t>(0);
            else {
                s.i(0);
                name(s, "RawAnimSequenceTrack");
                s.zeros(16);
                s.put<std::uint8_t>(0);
                if (version >= 1011)
                    s.put<std::uint8_t>(0);
            }
            s.append(keys);
        },
        -1, release);
}
} // namespace cyuediff::test
