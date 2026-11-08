#include <cyuediff/animation_compression.hpp>
#include <cyuediff/content_hash.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <limits>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool b, const char *s) {
    ++checks;
    if (!b)
        throw std::runtime_error(s);
}
void near(double a, double b, const char *s, double tolerance = 1e-6) {
    check(std::abs(a - b) <= tolerance, s);
}
template <class F> void refused(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        if (e.code != code)
            std::cerr << e.code << ": " << e.what() << '\n';
        check(e.code == code, "Refusal code");
        return;
    }
    throw std::runtime_error("Malformed compressed animation accepted");
}
using Bytes = std::vector<std::uint8_t>;
void put(Bytes &b, std::uint32_t v, unsigned n = 4) {
    for (unsigned i = 0; i < n; ++i)
        b.push_back(std::uint8_t(v >> (i * 8)));
}
void f32(Bytes &b, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put(b, v);
}
void set32(Bytes &b, std::size_t p, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b[p + i] = std::uint8_t(v >> (i * 8));
}
void align(Bytes &b) {
    while (b.size() % 4)
        b.push_back(0x55);
}
struct Wire {
    std::uint32_t frames = 5;
    Bytes keys;
    std::int32_t t = -1, r = -1, s = -1;
    std::int32_t add(unsigned format, unsigned flags, unsigned count, const Bytes &data,
                     const std::vector<std::uint32_t> &times = {}) {
        align(keys);
        auto offset = static_cast<std::int32_t>(keys.size());
        put(keys, (format << 28) | (flags << 24) | count);
        keys.insert(keys.end(), data.begin(), data.end());
        if (count > 1 && (flags & 8)) {
            align(keys);
            for (auto f : times)
                put(keys, f, frames > 255 ? 2 : 1);
        }
        return offset;
    }
    PerTrackAnimationSource source(bool scales = false) {
        PerTrackAnimationSource out;
        out.frame_count = frames;
        out.track_to_skeleton = {9};
        out.track_offset_count = 2;
        out.scale_offset_count = scales ? 1 : 0;
        out.scale_strip_size = scales ? 1 : 2;
        put(out.buffer, std::uint32_t(t));
        put(out.buffer, std::uint32_t(r));
        if (scales)
            put(out.buffer, std::uint32_t(s));
        out.key_byte_count = static_cast<std::uint32_t>(keys.size());
        out.buffer.insert(out.buffer.end(), keys.begin(), keys.end());
        return out;
    }
};
PerTrackAnimationOptions options() {
    PerTrackAnimationOptions o;
    o.profile = PerTrackAnimationProfile::unreal_5_3_windows;
    return o;
}
PerTrackAnimation decode(const PerTrackAnimationSource &s) {
    return decode_per_track_animation(s, options());
}
std::array<double, 4> normalized(std::array<double, 4> a) {
    double n = 0;
    for (auto v : a)
        n += v * v;
    for (auto &v : a)
        v /= std::sqrt(n);
    return a;
}
void reference(const fs::path &path) {
    Json source;
    {
        std::ifstream file(path);
        check(bool(file), "Open independent numerical references");
        file >> source;
    }
    check(source.at("schema") == "cyuediff.unreal-per-track-synthetic-reference/1",
          "Independent reference schema");
    check(source.at("boneCodecClass") == "/Script/Engine.AnimCompress_PerTrackCompression",
          "Reference actual codec class");
    check(source.at("cases").size() == 424, "Independent reference case count");
    for (const auto &test : source.at("cases")) {
        const auto name = test.at("name").get<std::string>();
        try {
            const auto &input = test.at("input");
            PerTrackAnimationSource wire;
            wire.frame_count = input.at("frames").get<std::uint32_t>();
            wire.track_to_skeleton = input.at("trackToSkeleton").get<std::vector<std::int32_t>>();
            wire.track_offset_count = input.at("trackOffsetCount").get<std::uint32_t>();
            wire.scale_offset_count = input.at("scaleOffsetCount").get<std::uint32_t>();
            wire.scale_strip_size = input.at("scaleStripSize").get<std::uint32_t>();
            wire.key_byte_count = input.at("keyBytes").get<std::uint32_t>();
            const auto hex = test.at("bufferHex").get<std::string>();
            check(hex.size() % 2 == 0, "Reference hexadecimal bytes");
            for (std::size_t i = 0; i < hex.size(); i += 2)
                wire.buffer.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
            auto decoded = decode(wire);
            check(decoded.to_json().at("bufferIoHash") == test.at("bufferIoHash"),
                  "Independent full-buffer IoHash");
            for (const auto *mode : {"linear", "step"}) {
                const auto interpolation = std::string(mode) == "step"
                                               ? CompressedAnimationInterpolation::step
                                               : CompressedAnimationInterpolation::linear;
                for (const auto &expected : test.at(mode)) {
                    const auto pose = decoded.sample(expected.at("frame").get<double>(), interpolation).at(0);
                    for (const auto *channel : {"translation", "rotation", "scale"}) {
                        const auto actual = pose.to_json().at(channel).get<std::vector<double>>();
                        auto values = expected.at(channel).get<std::vector<double>>();
                        if (std::string(channel) == "rotation") {
                            double dot = 0;
                            for (std::size_t i = 0; i < actual.size(); ++i)
                                dot += actual[i] * values[i];
                            if (dot < 0)
                                for (auto &v : values)
                                    v = -v;
                        }
                        check(actual.size() == values.size(), "Independent component count");
                        for (std::size_t i = 0; i < actual.size(); ++i)
                            near(actual[i], values[i], "Independent codec correspondence", 2e-6);
                    }
                }
            }
        } catch (const std::exception &) {
            std::cerr << "reference " << name << '\n';
            throw;
        }
    }
}

} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2) {
            reference(fs::u8path(argv[1]));
            std::cout << checks << " independent codec checks\n";
            return 0;
        }
        auto o = options();
        Wire w;
        Bytes b;
        for (float f : {2.f, 4.f, 6.f, 10.f, 20.f, 30.f})
            f32(b, f);
        w.t = w.add(1, 7, 2, b);
        b.clear();
        put(b, 32767, 2);
        put(b, 49150, 2);
        w.r = w.add(2, 12, 2, b, {0, 4});
        auto source = w.source();
        auto a = decode(source);
        auto j = a.to_json(true);
        check(a.frame_count() == 5 && a.track_count() == 1, "Frame/track dimensions");
        check(j.at("bufferIoHash") == blake3_bytes(source.buffer).substr(0, 40), "Exact full-buffer IoHash");
        check(j.at("bufferSha256") == sha256_bytes(source.buffer), "Exact full-buffer SHA256");
        check(j.at("tracks")[0].at("rotation").at("frames") == Json({0, 4}), "Stored explicit frames");
        check(j.at("tracks")[0].at("translation").at("values").size() == 2, "Stored keys exposed");
        check(!a.to_json()["tracks"][0]["translation"].contains("values"), "Keys opt-in");
        for (double frame : {0., .5, 1., 2., 3.5, 4.}) {
            const auto pose = a.sample(frame)[0];
            check(pose.track == 0 && pose.skeleton_index == 9, "Exact numeric binding");
            near(pose.translation[0], 2 + 2 * frame, "Uniform translation X");
            near(pose.translation[1], 4 + 4 * frame, "Uniform translation Y");
            near(pose.translation[2], 6 + 6 * frame, "Uniform translation Z");
            check(pose.scale == std::array<double, 3>{1, 1, 1}, "Absent scale table keeps unit scale");
            auto end = 49150.f;
            const double z = (end - 32767.f) * (1.f / 32767.f);
            const double alpha = frame / 4.;
            auto q = normalized({0, 0, z * alpha, 1 * (1 - alpha) + std::sqrt(1 - z * z) * alpha});
            for (unsigned i = 0; i < 4; ++i)
                near(pose.rotation[i], q[i], "Shortest quaternion nlerp", 2e-7);
        }
        near(a.sample(2, CompressedAnimationInterpolation::step)[0].translation[0], 2, "Uniform step");
        near(a.sample(3.5, CompressedAnimationInterpolation::step)[0].rotation[2], 0, "Explicit step");
        near(a.sample(4, CompressedAnimationInterpolation::step)[0].translation[0], 10, "Step last endpoint");
        Wire implicit;
        auto implicit_source = implicit.source();
        auto p = decode(implicit_source).sample(0)[0];
        check(p.translation == std::array<double, 3>{0, 0, 0} &&
                  p.rotation == std::array<double, 4>{0, 0, 0, 1},
              "Implicit channels");
        check(decode(implicit.source(true)).sample(0)[0].scale == std::array<double, 3>{0, 0, 0},
              "Present table with missing scale is zero");
        for (unsigned mask = 0; mask < 8; ++mask) {
            Wire v;
            Bytes encoded;
            const unsigned stored = mask ? mask : 7;
            for (unsigned i = 0; i < 3; ++i)
                if (stored & (1u << i))
                    f32(encoded, float(i + 2));
            v.t = v.add(1, mask, 1, encoded);
            v.s = v.t;
            auto d = decode(v.source(true));
            auto q = d.sample(2)[0];
            for (unsigned i = 0; i < 3; ++i) {
                near(q.translation[i], stored & (1u << i) ? i + 2. : 0., "Masked/legacy float");
                near(q.scale[i], q.translation[i], "Shared vector window");
            }
            check(d.to_json()["unreferencedKeyBytes"] == 0, "Exact duplicate window counted once");
            v = Wire{};
            encoded.clear();
            for (unsigned i = 0; i < 3; ++i)
                if (stored & (1u << i))
                    put(encoded, 256 + i, 2);
            v.t = v.add(2, mask, 1, encoded);
            v.r = v.t;
            auto r = decode(v.source()).sample(0)[0];
            for (unsigned i = 0; i < 3; ++i)
                near(r.translation[i], mask & (1u << i) ? 1. + i : 0., "Per-track fixed vector bias");
            auto qx = std::array<double, 4>{0, 0, 0, 0};
            for (unsigned i = 0; i < 3; ++i)
                qx[i] = mask & (1u << i) ? (float(256 + i) - 32767.f) * (1.f / 32767.f) : 0.;
            double sq = 1;
            for (unsigned i = 0; i < 3; ++i)
                sq -= qx[i] * qx[i];
            qx[3] = sq > 0 ? std::sqrt(sq) : 0;
            qx = normalized(qx);
            for (unsigned i = 0; i < 4; ++i)
                near(r.rotation[i], qx[i], "Masked fixed quaternion", 2e-7);
        }
        Wire interval;
        Bytes bounds;
        for (float f : {10.f, 4.f, -2.f, 8.f, 5.f, 2.f})
            f32(bounds, f);
        put(bounds, (1023u << 21) | (0u << 10) | 1022u);
        interval.t = interval.add(3, 7, 1, bounds);
        p = decode(interval.source()).sample(0)[0];
        near(p.translation[0], 14, "Interval X low 10 bits");
        near(p.translation[1], -10, "Interval Y middle 11 bits");
        near(p.translation[2], 5, "Interval Z high 11 bits");
        bounds.clear();
        for (float f : {0.f, .5f, 0.f, .25f, 0.f, .125f})
            f32(bounds, f);
        put(bounds, (2046u << 21) | (0u << 10) | 511u);
        interval.r = interval.add(3, 7, 1, bounds);
        p = decode(interval.source()).sample(0)[0];
        near(p.rotation[0], .5, "Quaternion interval X high bits");
        near(p.rotation[1], -.25, "Quaternion interval Y");
        near(p.rotation[2], 0, "Quaternion interval Z low bits");
        for (unsigned mask = 0; mask < 8; ++mask) {
            Wire v;
            Bytes data;
            for (unsigned i = 0; i < 3; ++i)
                if (!mask || (mask & (1u << i))) {
                    f32(data, float(i + 1));
                    f32(data, 0);
                }
            put(data, 0);
            v.t = v.add(3, mask, 1, data);
            auto value = decode(v.source()).sample(0)[0].translation;
            for (unsigned i = 0; i < 3; ++i)
                near(value[i], mask & (1u << i) ? double(i + 1) : 0.,
                     "Interval component presence and zero range");
        }
        for (unsigned format : {4u, 5u, 6u}) {
            Wire v;
            Bytes d;
            if (format == 4)
                put(d, (2046u << 21) | (1023u << 10) | 511u);
            if (format == 5)
                put(d, (384u << 21) | (1280u << 10));
            v.r = v.add(format, 7, 1, d);
            auto r = decode(v.source()).sample(0)[0].rotation;
            const auto expected = format == 4   ? std::array<double, 4>{1, 0, 0, 0}
                                  : format == 5 ? std::array<double, 4>{.5, -.25, 0, std::sqrt(.6875)}
                                                : std::array<double, 4>{0, 0, 0, 1};
            for (unsigned i = 0; i < 4; ++i)
                near(r[i], expected[i], "Packed quaternion/identity", 1e-7);
        }
        Wire identity;
        identity.t = identity.add(6, 0, 1, {});
        identity.s = identity.t;
        check(decode(identity.source(true)).sample(1)[0].scale == std::array<double, 3>{0, 0, 0},
              "Encoded identity vector scale is zero");
        Wire legacyrot;
        Bytes xyz;
        for (float v : {.25f, -.5f, 0.f})
            f32(xyz, v);
        legacyrot.r = legacyrot.add(1, 0, 1, xyz);
        auto q = decode(legacyrot.source()).sample(0)[0].rotation;
        near(q[0], .25, "Legacy full rotation X");
        near(q[1], -.5, "Legacy full rotation Y");
        for (unsigned frames : {5u, 255u, 256u, 1000u}) {
            Wire v;
            v.frames = frames;
            Bytes d;
            for (float x : {0.f, 10.f, 20.f})
                f32(d, x);
            v.t = v.add(1, 9, 3, d, {0, frames / 2, frames - 1});
            auto seq = decode(v.source());
            near(seq.sample(frames / 2.)[0].translation[0],
                 frames % 2 ? 10. + 5. / (frames - 1 - frames / 2) : 10.,
                 "Byte/u16 frame table interpolation", 1e-5);
            near(seq.sample(frames / 2, CompressedAnimationInterpolation::step)[0].translation[0], 10,
                 "Table exact key step");
            near(seq.sample(frames - 1)[0].translation[0], 20, "Table last key");
        }
        Wire dup;
        Bytes ds;
        for (float v : {1.f, 2.f, 3.f})
            f32(ds, v);
        dup.t = dup.add(1, 9, 3, ds, {0, 0, 4});
        near(decode(dup.source()).sample(0)[0].translation[0], 1, "Duplicate time beginning");
        near(decode(dup.source()).sample(1)[0].translation[0], 2.25,
             "Duplicate times choose last preceding key");
        Wire ends;
        ends.t = ends.add(1, 9, 3, ds, {1, 3, 4});
        near(decode(ends.source()).sample(.5)[0].translation[0], .75,
             "Unanchored table retains codec extrapolation");
        const auto catalog = a.catalog();
        check(!catalog["tracks"][0]["rotation"].contains("frames") &&
                  catalog["tracks"][0]["rotation"]["frameTableEntries"] == 2,
              "Catalogue avoids full timing arrays");
        check(!catalog["tracks"][0]["translation"]["encodedSha256"].get<std::string>().empty(),
              "Stored channel fingerprint is retained");
        for (double frame : {0., .5, 2.5, 4.})
            check(a.sample_track(0, frame).to_json() == a.sample(frame)[0].to_json(),
                  "Focused sample equals full sample");
        const auto preview = a.key_preview(0, 2);
        check(preview["translation"]["points"].size() == 2 &&
                  preview["translation"]["points"][1]["index"] == 1 &&
                  preview["translation"]["points"][1]["frame"] == 4,
              "Focused key endpoints and uniform timing");
        check(preview["scale"]["points"].empty() && preview["scale"]["implicitValue"] == Json({1, 1, 1}),
              "Implicit codec scale is not a fabricated stored key");
        check(a.stored_key(0, CompressedAnimationChannel::rotation, 1)["frame"] == 4,
              "Explicit stored timing is retained");
        refused([&] { a.key_preview(0, 1); }, "animation-compression-limit");
        refused([&] { a.key_preview(0, 4097); }, "animation-compression-limit");
        refused([&] { a.sample_track(1, 0); }, "animation-compression-layout");
        refused([&] { a.stored_key(0, CompressedAnimationChannel::scale, 0); },
                "animation-compression-layout");
        Wire many;
        many.frames = 1000;
        Bytes many_keys;
        for (unsigned i = 0; i < 1000; ++i)
            f32(many_keys, float(i));
        many.t = many.add(1, 1, 1000, many_keys);
        auto reduced = decode(many.source()).key_preview(0, 7);
        check(reduced["translation"]["points"].size() == 7 && reduced["translation"]["omittedKeys"] == 993 &&
                  reduced["translation"]["points"].back()["index"] == 999,
              "Decimated preview preserves original indices and last endpoint");
        auto copy = a;
        source.buffer.clear();
        near(copy.sample(4)[0].translation[0], 10, "Decoded immutable ownership");
        auto bad = w.source();
        for (std::size_t n = 0; n < bad.buffer.size(); ++n) {
            auto cut = bad;
            cut.buffer.resize(n);
            refused([&] { decode(cut); }, "animation-compression-layout");
        }
        for (std::size_t n = 8; n < bad.buffer.size(); ++n) {
            auto cut = w.source();
            cut.buffer.resize(n);
            cut.key_byte_count = static_cast<std::uint32_t>(n - 8);
            refused([&] { decode(cut); }, "animation-compression-layout");
        }
        bad = w.source();
        set32(bad.buffer, 0, 0xfffffffe);
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        set32(bad.buffer, 0, 1);
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        set32(bad.buffer, 0, bad.key_byte_count + 4);
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        set32(bad.buffer, 8, 0x17000000);
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        set32(bad.buffer, 8, 0x17000006);
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        set32(bad.buffer, 8, 0x07000001);
        refused([&] { decode(bad); }, "animation-compression-feature");
        bad = w.source();
        set32(bad.buffer, 8, 0x77000001);
        refused([&] { decode(bad); }, "animation-compression-feature");
        bad = w.source();
        set32(bad.buffer, 8, 0x47000001);
        refused([&] { decode(bad); }, "animation-compression-feature");
        bad = w.source();
        set32(bad.buffer, 12, 0x7fc00000);
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        bad.buffer.back() = 5;
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        bad.buffer[bad.buffer.size() - 2] = 4;
        bad.buffer.back() = 0;
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        bad.frame_count = 0;
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        bad.track_offset_count = 3;
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        bad.scale_offset_count = 1;
        refused([&] { decode(bad); }, "animation-compression-feature");
        bad = w.source();
        bad.scale_offset_count = 2;
        bad.scale_strip_size = 1;
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        bad.track_to_skeleton[0] = -1;
        refused([&] { decode(bad); }, "animation-compression-layout");
        bad = w.source();
        bad.track_to_skeleton = {1, 1};
        bad.track_offset_count = 4;
        bad.buffer.insert(bad.buffer.begin(), 8, 255);
        refused([&] { decode(bad); }, "animation-compression-layout");
        Wire partial;
        Bytes data;
        put(data, 0x17000001);
        for (float v : {0.f, 0.f, 0.f, 0.f, 0.f})
            f32(data, v);
        partial.t = partial.add(1, 7, 2, data);
        partial.r = 4;
        refused([&] { decode(partial.source()); }, "animation-compression-layout");
        Wire partialrot;
        Bytes shortrot;
        f32(shortrot, .1f);
        partialrot.r = partialrot.add(1, 1, 1, shortrot);
        refused([&] { decode(partialrot.source()); }, "animation-compression-feature");
        refused([&] { decode_per_track_animation(w.source(), {}); }, "animation-compression-feature");
        for (double f :
             {-1., 4.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
            refused([&] { a.sample(f); }, "animation-compression-layout");
        refused([&] { a.sample(0, static_cast<CompressedAnimationInterpolation>(99)); },
                "animation-compression-feature");
        for (unsigned mode = 0; mode < 5; ++mode) {
            o = options();
            if (mode == 0)
                o.max_tracks = 0;
            if (mode == 1)
                o.max_frames = 4;
            if (mode == 2)
                o.max_keys = 3;
            if (mode == 3)
                o.max_read_bytes = w.source().buffer.size() - 1;
            if (mode == 4)
                o.max_output_bytes = 1;
            refused([&] { decode_per_track_animation(w.source(), o); }, "animation-compression-limit");
        }
        o = options();
        o.control.cancelled = [] { return true; };
        refused([&] { decode_per_track_animation(w.source(), o); }, "cancelled");
        bool cancelled = false;
        o = options();
        o.control.cancelled = [&] { return cancelled; };
        o.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "decode-compressed-animation")
                cancelled = true;
        };
        refused([&] { decode_per_track_animation(w.source(), o); }, "cancelled");
        OperationControl c;
        c.cancelled = [] { return true; };
        refused([&] { a.sample(0, CompressedAnimationInterpolation::linear, c); }, "cancelled");
        cancelled = false;
        c.cancelled = [&] { return cancelled; };
        c.progress = [&](const OperationProgress &) { cancelled = true; };
        refused([&] { a.sample(0, CompressedAnimationInterpolation::linear, c); }, "cancelled");
        o = options();
        o.max_frames = 0xffffffffu;
        auto signed_frame = w.source();
        signed_frame.frame_count = 0x80000000u;
        refused([&] { decode_per_track_animation(signed_frame, o); }, "animation-compression-layout");
        auto zero_strip = w.source();
        zero_strip.scale_strip_size = 0;
        refused([&] { decode(zero_strip); }, "animation-compression-layout");
        Wire excessive;
        Bytes huge;
        for (float value :
             {std::numeric_limits<float>::max(), 0.f, 0.f, -std::numeric_limits<float>::max(), 0.f, 0.f})
            f32(huge, value);
        excessive.t = excessive.add(1, 7, 2, huge);
        auto overflowing = decode(excessive.source());
        refused([&] { overflowing.sample(2); }, "animation-compression-layout");
        Wire negative_arc;
        Bytes arc;
        for (float value : {-.9f, 0.f, 0.f, .9f, 0.f, 0.f})
            f32(arc, value);
        negative_arc.r = negative_arc.add(1, 7, 2, arc);
        auto short_arc = decode(negative_arc.source()).sample(2)[0].rotation;
        near(std::abs(short_arc[0]), 1, "Negative quaternion dot uses shortest arc");
        near(short_arc[3], 0, "Shortest arc crosses zero W");
        Wire scaled;
        Bytes scale;
        for (float value : {1.f, 2.f, 3.f, 5.f, 6.f, 7.f})
            f32(scale, value);
        scaled.s = scaled.add(1, 7, 2, scale);
        auto scale_pose = decode(scaled.source(true)).sample(2)[0];
        check(scale_pose.scale == std::array<double, 3>{3, 4, 5},
              "Stored scale interpolates without normalization");
        Wire one;
        one.frames = 1;
        one.t = one.add(1, 7, 1, xyz);
        near(decode(one.source()).sample(0)[0].translation[0], .25, "Single frame");
        auto empty = PerTrackAnimationSource{};
        empty.frame_count = 1;
        check(decode(empty).sample(0).empty(), "Empty track view has no invented bones");
        auto padded = w.source();
        padded.buffer.push_back(42);
        ++padded.key_byte_count;
        auto pad = decode(padded);
        check(pad.to_json()["unreferencedKeyBytes"].get<unsigned>() > 0, "Unreferenced bytes retained");
        check(pad.to_json()["bufferSha256"] != a.to_json()["bufferSha256"],
              "Opaque changes retain integrity difference");
        std::cout << checks << " compressed animation checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
