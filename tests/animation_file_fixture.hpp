#pragma once
#include "package_fixture.hpp"
#include <cmath>
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
            integer(rate, "Numerator", change == 14 ? 60 : 30);
            if (change != 4)
                integer(rate, "Denominator", 1);
            name(rate, "None");
            tag(s, "FrameRate", "StructProperty", rate, "FrameRate");
            Bytes length;
            length.put<float>(2.f / 30.f);
            tag(s, "PlayLength", "FloatProperty", length);
            Bytes tracks;
            const int track_count = change == 9 ? 0 : change == 11 ? 1 : 2;
            tracks.i(track_count);
            if (version < 1012) {
                Bytes empty;
                tag(tracks, "BoneAnimationTracks", "StructProperty", empty, "BoneAnimationTrack");
            }
            for (int i = 0; i < track_count; ++i) {
                const auto bone = change == 2 ? 1 - i : i;
                Bytes n;
                name(n, bone == 0 || change == 5 ? "root" : "arm~/track");
                if (change != 13 || bone != 0)
                    tag(tracks, "Name", "NameProperty", n);
                integer(tracks, "BoneTreeIndex", bone);
                Bytes raw;
                for (unsigned channel = 0; channel < 3; ++channel) {
                    const auto width = channel == 1 ? 4u : 3u;
                    const unsigned count = channel == 0 || (change == 17 && channel == 1) ? 3u
                                           : change == 3 && channel == 2                  ? 2u
                                                                                          : 1u;
                    raw.i(width * 4);
                    raw.i(count);
                    for (unsigned key = 0; key < count; ++key)
                        for (unsigned c = 0; c < width; ++c) {
                            float v = channel == 0   ? float(key * 10)
                                      : channel == 1 ? (c == 3 ? 1.f : 0.f)
                                                     : 1.f;
                            if (channel == 0 && bone == 1 && key == 1 && c == 0) {
                                if (change == 1)
                                    v += 1.f;
                                if (change == 15)
                                    v += .25f;
                            }
                            if (channel == 1) {
                                if (change == 6)
                                    v = c == 3 ? -1.f : 0.f;
                                if (change == 7 && bone == 1)
                                    v = c == 2 || c == 3 ? std::sqrt(.5f) : 0.f;
                                if (change == 12 && bone == 1)
                                    v = c == 3 ? 2.f : 0.f;
                                if (change == 16 && bone == 1)
                                    v = c == 0 ? 1.f : 0.f;
                                if (change == 17 && bone == 1) {
                                    constexpr double pi = 3.14159265358979323846;
                                    const auto angle = (key == 1 ? -179. : 179.) * pi / 360.;
                                    v = float(c == 2 ? std::sin(angle) : c == 3 ? std::cos(angle) : 0.);
                                }
                            }
                            if (change == 8 && channel == 2 && bone == 1 && c == 0)
                                v = -1.f;
                            raw.put<float>(v);
                        }
                }
                tag(tracks, "InternalTrackData", "StructProperty", raw, "RawAnimSequenceTrack");
                name(tracks, "None");
            }
            if (change != 10)
                tag(s, "BoneAnimationTracks", "ArrayProperty", tracks, "BoneAnimationTrack", true);
        },
        -1, 39, -1, -1, -1, -1, -1, "AnimDataModel", "/Script/Engine");
}
} // namespace cyuediff::test
