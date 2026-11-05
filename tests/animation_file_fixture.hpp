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
inline Fixture animation_model_file_fixture(int change = 0, int version = 1013) {
    return fixture(
        version, 1, false, 0,
        {"BoneAnimationTracks", "BoneAnimationTrack", "InternalTrackData", "RawAnimSequenceTrack", "Name",
         "NameProperty", "root", "arm~/track", "BoneTreeIndex", "NumberOfKeys", "NumberOfFrames", "FrameRate",
         "Numerator", "Denominator", "FloatProperty", "PlayLength"},
        [=](Bytes &s, const NameWriter &name) {
            auto tag = [&](Bytes &b, const char *field, const char *kind, const Bytes &payload,
                           const char *structure = nullptr, bool array = false) {
                name(b, field);
                name(b, kind);
                if (version >= 1012) {
                    b.i(structure ? 1 : 0);
                    if (structure) {
                        if (array) {
                            name(b, "StructProperty");
                            b.i(1);
                        }
                        name(b, structure);
                        b.i(0);
                    }
                }
                b.i(static_cast<int>(payload.data.size()));
                if (version >= 1012)
                    b.put<std::uint8_t>(0);
                else {
                    b.i(0);
                    if (array)
                        name(b, "StructProperty");
                    else if (structure) {
                        name(b, structure);
                        b.zeros(16);
                    }
                    b.put<std::uint8_t>(0);
                    if (version >= 1011)
                        b.put<std::uint8_t>(0);
                }
                b.append(payload);
            };
            auto integer = [&](Bytes &b, const char *field, int n) {
                Bytes payload;
                payload.i(n);
                tag(b, field, "IntProperty", payload);
            };
            integer(s, "NumberOfKeys", 3);
            integer(s, "NumberOfFrames", 2);
            Bytes rate;
            integer(rate, "Numerator", 30);
            if (change != 4)
                integer(rate, "Denominator", 1);
            name(rate, "None");
            tag(s, "FrameRate", "StructProperty", rate, "FrameRate");
            Bytes length;
            length.put<float>(2.f / 30.f);
            tag(s, "PlayLength", "FloatProperty", length);
            Bytes tracks;
            tracks.i(2);
            if (version < 1012) {
                Bytes empty;
                tag(tracks, "BoneAnimationTracks", "StructProperty", empty, "BoneAnimationTrack");
            }
            for (int i = 0; i < 2; ++i) {
                const auto bone = change == 2 ? 1 - i : i;
                Bytes n;
                name(n, bone == 0 || change == 5 ? "root" : "arm~/track");
                tag(tracks, "Name", "NameProperty", n);
                integer(tracks, "BoneTreeIndex", bone);
                Bytes raw;
                for (unsigned channel = 0; channel < 3; ++channel) {
                    const auto width = channel == 1 ? 4u : 3u, count = channel == 0                  ? 3u
                                                                       : change == 3 && channel == 2 ? 2u
                                                                                                     : 1u;
                    raw.i(width * 4);
                    raw.i(count);
                    for (unsigned key = 0; key < count; ++key)
                        for (unsigned c = 0; c < width; ++c)
                            raw.put<float>(
                                channel == 0   ? float(key * 10 +
                                                     (change == 1 && bone == 1 && key == 1 && c == 0 ? 1 : 0))
                                : channel == 1 ? (c == 3 ? 1.f : 0.f)
                                               : 1.f);
                }
                tag(tracks, "InternalTrackData", "StructProperty", raw, "RawAnimSequenceTrack");
                name(tracks, "None");
            }
            tag(s, "BoneAnimationTracks", "ArrayProperty", tracks, "BoneAnimationTrack", true);
        },
        -1, 39, -1, -1, -1, -1, -1, "AnimDataModel", "/Script/Engine");
}
} // namespace cyuediff::test
