#include <cyuediff/animation_compression.hpp>
#include <cyuediff/content_hash.hpp>
#include "archive.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <utility>
namespace cyuediff {
namespace {
void require(bool b, const char *s, std::uint64_t pos = 0) {
    if (!b)
        throw Error("animation-compression-layout", s, pos);
}
void support(bool b, const char *s) {
    if (!b)
        throw Error("animation-compression-feature", s);
}
void limit(bool b, const char *s) {
    if (!b)
        throw Error("animation-compression-limit", s);
}
struct Cursor {
    const std::vector<std::uint8_t> &bytes;
    std::uint64_t pos = 0, end = 0;
    void range(std::uint64_t n) const {
        require(pos <= end && n <= end - pos, "Animation field exceeds bounded buffer", pos);
    }
    std::uint32_t number(unsigned n) {
        range(n);
        std::uint32_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= std::uint32_t(bytes[std::size_t(pos++)]) << (8 * i);
        return v;
    }
    std::int32_t i32() {
        auto v = number(4);
        std::int32_t s;
        std::memcpy(&s, &v, 4);
        return s;
    }
    float f32() {
        auto v = number(4);
        float f;
        std::memcpy(&f, &v, 4);
        require(std::isfinite(f), "Non-finite animation float", pos - 4);
        return f;
    }
};
struct Channel {
    std::int32_t offset = -1;
    std::uint32_t format = 6, flags = 0, bytes = 0;
    std::string encoded_sha256;
    // Four components allow vector and quaternion channels to share bounded storage.
    std::vector<std::array<float, 4>> keys;
    std::vector<std::uint32_t> frames;
};
struct Track {
    std::int32_t skeleton = 0;
    Channel translation, rotation, scale;
};
struct Budget {
    const PerTrackAnimationOptions &o;
    std::uint64_t keys = 0, output = 0;
    void reserve(std::uint64_t n) {
        limit(output <= o.max_output_bytes && n <= o.max_output_bytes - output,
              "Decoded animation exceeds output byte budget");
        output += n;
    }
    void add_keys(std::uint64_t n) {
        limit(keys <= o.max_keys && n <= o.max_keys - keys, "Animation exceeds aggregate key budget");
        keys += n;
    }
};
float mini_float(std::uint32_t v, unsigned mantissa) {
    if (!v)
        return 0;
    const auto sign = (v >> (mantissa + 3)) & 1;
    const auto exponent = ((v >> mantissa) & 7) + 123;
    const auto bits = (sign << 31) | (exponent << 23) | ((v & ((1u << mantissa) - 1)) << (23 - mantissa));
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
std::array<float, 4> unpack(Cursor &r, unsigned format, unsigned mask, bool rotation,
                            const std::array<float, 3> &mins, const std::array<float, 3> &ranges) {
    std::array<float, 4> out{};
    if (format == 1) {
        auto present = mask ? mask : 7;
        for (unsigned i = 0; i < 3; ++i)
            if (present & (1u << i))
                out[i] = r.f32();
    } else if (format == 2) {
        if (!mask) {
            // Legacy zero mask still reserves three encoded components.
            r.range(6);
            r.pos += 6;
        }
        for (unsigned i = 0; i < 3; ++i)
            if (mask & (1u << i)) {
                const auto v = static_cast<int>(r.number(2));
                out[i] = rotation ? (v - 32767.f) * (1.f / 32767.f) : float(v - 255);
            }
    } else if (format >= 3 && format <= 5) {
        const auto v = r.number(4);
        if (format == 5) {
            out[0] = mini_float(v >> 21, 7);
            out[1] = mini_float((v >> 10) & 2047, 7);
            out[2] = mini_float(v & 1023, 6);
        } else {
            const std::array<std::uint32_t, 3> raw =
                rotation ? std::array<std::uint32_t, 3>{v >> 21, (v >> 10) & 2047, v & 1023}
                         : std::array<std::uint32_t, 3>{v & 1023, (v >> 10) & 2047, v >> 21};
            const std::array<int, 3> bias =
                rotation ? std::array<int, 3>{1023, 1023, 511} : std::array<int, 3>{511, 1023, 1023};
            for (unsigned i = 0; i < 3; ++i) {
                const float normalized = (static_cast<int>(raw[i]) - bias[i]) * (1.f / bias[i]);
                out[i] = format == 3 ? normalized * ranges[i] + mins[i] : normalized;
            }
        }
    }
    for (unsigned i = 0; i < 3; ++i)
        require(std::isfinite(out[i]), "Non-finite decoded animation component", r.pos);
    if (rotation) {
        const auto square = 1.f - out[0] * out[0] - out[1] * out[1] - out[2] * out[2];
        out[3] = square > 0 ? std::sqrt(square) : 0;
    }
    return out;
}
Channel channel(Cursor buffer, std::uint64_t key_start, std::int32_t offset, bool rotation,
                std::uint32_t frames, Budget &b,
                std::vector<std::pair<std::uint64_t, std::uint64_t>> &windows) {
    Channel out;
    out.offset = offset;
    require(offset >= -1, "Invalid negative animation channel offset");
    if (offset == -1)
        return out;
    require((offset & 3) == 0, "Animation channel offset is not four-byte aligned", offset);
    buffer.pos = key_start + static_cast<std::uint32_t>(offset);
    const auto begin = buffer.pos;
    const auto header = buffer.number(4), count = header & 0x00ffffff;
    out.format = header >> 28;
    out.flags = (header >> 24) & 15;
    const auto mask = out.flags & 7;
    require(count && count <= frames, "Animation channel key count disagrees with frame count", begin);
    support(out.format >= 1 && out.format <= 6, "Unsupported per-track animation key format");
    support(rotation || (out.format != 4 && out.format != 5),
            "Packed quaternion key format cannot decode a vector channel");
    support(!rotation || out.format != 1 || mask == 0 || mask == 7,
            "Partial Float96 rotation needs another producer profile");
    b.add_keys(count);
    b.reserve(std::uint64_t(count) * sizeof(std::array<float, 4>));
    const unsigned components =
        mask ? unsigned(bool(mask & 1)) + unsigned(bool(mask & 2)) + unsigned(bool(mask & 4)) : 3;
    const unsigned stride = out.format == 1   ? components * 4
                            : out.format == 2 ? components * 2
                            : out.format == 6 ? 0
                                              : 4;
    std::array<float, 3> mins{}, ranges{};
    if (out.format == 3) {
        for (unsigned i = 0; i < 3; ++i)
            if (!mask || (mask & (1u << i))) {
                const auto a = buffer.f32(), d = buffer.f32();
                if (mask) {
                    mins[i] = a;
                    ranges[i] = d;
                }
            }
    }
    buffer.range(std::uint64_t(stride) * count);
    out.keys.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if ((i & 255) == 0)
            b.o.control.check();
        out.keys.push_back(unpack(buffer, out.format, mask, rotation, mins, ranges));
    }
    if (count > 1 && (out.flags & 8)) {
        buffer.pos = (buffer.pos + 3) & ~std::uint64_t(3);
        const unsigned width = frames > 255 ? 2 : 1;
        buffer.range(std::uint64_t(count) * width);
        b.reserve(std::uint64_t(count) * sizeof(std::uint32_t));
        out.frames.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if ((i & 255) == 0)
                b.o.control.check();
            auto f = buffer.number(width);
            require(f < frames && (i == 0 || f >= out.frames.back()),
                    "Animation frame table is outside sequence or unordered", buffer.pos - width);
            out.frames.push_back(f);
        }
    }
    out.bytes = static_cast<std::uint32_t>(buffer.pos - begin);
    b.reserve(64);
    out.encoded_sha256 = detail::hash_range(buffer.bytes, begin, out.bytes, b.o.control);
    windows.emplace_back(begin, buffer.pos);
    return out;
}
struct Pair {
    std::size_t a = 0, b = 0;
    float alpha = 0;
};
Pair pair(const Channel &c, std::uint32_t frames, float relative, bool step) {
    Pair p;
    const auto n = c.keys.size();
    if (n < 2 || relative <= 0)
        return p;
    if (relative >= 1) {
        p.a = p.b = n - 1;
        return p;
    }
    if (c.frames.empty()) {
        const float position = relative * float(n - 1);
        const float low = std::floor(position);
        p.a = std::min(std::size_t(low), n - 1);
        p.b = std::min(p.a + 1, n - 1);
        p.alpha = step ? 0.f : position - low;
    } else {
        const float position = relative * float(frames - 1);
        const auto low_frame = std::min(static_cast<std::uint32_t>(position), frames - 2);
        auto next = std::upper_bound(c.frames.begin(), c.frames.end(), low_frame);
        p.a = next == c.frames.begin() ? 0 : std::size_t(next - c.frames.begin() - 1);
        p.b = std::min(p.a + 1, n - 1);
        const auto delta = std::max(c.frames[p.b] - c.frames[p.a], 1u);
        p.alpha = step ? 0.f : (position - float(c.frames[p.a])) / float(delta);
    }
    return p;
}
std::array<double, 3> vector_sample(const Channel &c, std::uint32_t frames, float relative, bool step,
                                    const std::array<double, 3> &absent) {
    if (c.keys.empty())
        return absent;
    auto p = pair(c, frames, relative, step);
    std::array<double, 3> out;
    for (unsigned i = 0; i < 3; ++i) {
        float a = c.keys[p.a][i], z = c.keys[p.b][i];
        float v = p.a == p.b ? a : a + p.alpha * (z - a);
        require(std::isfinite(v), "Non-finite interpolated animation vector");
        out[i] = v;
    }
    return out;
}
std::array<double, 4> rotation_sample(const Channel &c, std::uint32_t frames, float relative, bool step) {
    if (c.keys.empty())
        return {0, 0, 0, 1};
    auto p = pair(c, frames, relative, step);
    const auto &a = c.keys[p.a], &z = c.keys[p.b];
    std::array<double, 4> out{};
    float dot = 0;
    if (p.a != p.b) {
        for (unsigned i = 0; i < 4; ++i)
            dot += a[i] * z[i];
        require(std::isfinite(dot), "Non-finite interpolated animation quaternion dot product");
    }
    const float bias = dot >= 0 ? 1.f : -1.f;
    double square = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const float v = p.a == p.b ? a[i] : z[i] * p.alpha + a[i] * (bias * (1.f - p.alpha));
        require(std::isfinite(v), "Non-finite interpolated animation quaternion");
        out[i] = v;
        square += out[i] * out[i];
    }
    if (square < 1e-8)
        return {0, 0, 0, 1};
    const auto inverse = 1. / std::sqrt(square);
    for (auto &v : out)
        v *= inverse;
    return out;
}
Json channel_json(const Channel &c, bool rotation, bool include_keys, bool timing = true) {
    Json j = {{"offset", c.offset},
              {"bytes", c.bytes},
              {"format", c.format},
              {"flags", c.flags},
              {"keys", c.keys.size()},
              {"encodedSha256", c.encoded_sha256},
              {"frameTableEntries", c.frames.size()}};
    if (timing)
        j["frames"] = c.frames;
    j["timing"] = c.keys.empty() ? "implicit" : c.frames.empty() ? "uniform" : "explicit";
    if (include_keys) {
        j["values"] = Json::array();
        for (const auto &k : c.keys) {
            if (rotation)
                j["values"].push_back(k);
            else
                j["values"].push_back(std::array<float, 3>{k[0], k[1], k[2]});
        }
    }
    return j;
}
} // namespace
struct PerTrackAnimation::Impl {
    std::uint32_t frames = 0, scale_strip_size = 0;
    bool scale_present = false;
    std::uint64_t buffer_bytes = 0, key_bytes = 0, unreferenced_bytes = 0, decoded_keys = 0;
    std::string sha256, io_hash;
    std::vector<Track> tracks;
};
PerTrackAnimation::PerTrackAnimation(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
std::uint32_t PerTrackAnimation::frame_count() const {
    return impl_->frames;
}
std::size_t PerTrackAnimation::track_count() const {
    return impl_->tracks.size();
}
Json CompressedAnimationBonePose::to_json() const {
    return {{"track", track},
            {"skeletonIndex", skeleton_index},
            {"translation", translation},
            {"rotation", rotation},
            {"scale", scale}};
}
Json PerTrackAnimation::to_json(bool include_keys) const {
    return json(include_keys, true);
}
Json PerTrackAnimation::catalog(const OperationControl &control) const {
    return json(false, false, control);
}
Json PerTrackAnimation::json(bool include_keys, bool timing, const OperationControl &control) const {
    control.check();
    const auto &v = *impl_;
    Json j = {{"schema", "cyuediff.per-track-animation/1"},
              {"profile", "ue5.3-windows"},
              {"frames", v.frames},
              {"bufferBytes", v.buffer_bytes},
              {"keyBytes", v.key_bytes},
              {"bufferSha256", v.sha256},
              {"bufferIoHash", v.io_hash},
              {"unreferencedKeyBytes", v.unreferenced_bytes},
              {"decodedKeys", v.decoded_keys},
              {"scaleOffsetsPresent", v.scale_present},
              {"scaleStripSize", v.scale_strip_size},
              {"tracks", Json::array()}};
    if (!timing)
        j["schema"] = "cyuediff.per-track-animation-catalog/1";
    for (std::size_t i = 0; i < v.tracks.size(); ++i) {
        control.check();
        const auto &t = v.tracks[i];
        j["tracks"].push_back({{"track", i},
                               {"skeletonIndex", t.skeleton},
                               {"translation", channel_json(t.translation, false, include_keys, timing)},
                               {"rotation", channel_json(t.rotation, true, include_keys, timing)},
                               {"scale", channel_json(t.scale, false, include_keys, timing)}});
    }
    return j;
}
Json PerTrackAnimation::stored_key(std::size_t track, CompressedAnimationChannel which, std::uint32_t key,
                                   const OperationControl &control) const {
    control.check();
    const auto &v = *impl_;
    require(track < v.tracks.size(), "Compressed track index exceeds table");
    support(which == CompressedAnimationChannel::translation ||
                which == CompressedAnimationChannel::rotation || which == CompressedAnimationChannel::scale,
            "Unsupported compressed channel selector");
    const auto &t = v.tracks[track];
    const auto &c = which == CompressedAnimationChannel::translation ? t.translation
                    : which == CompressedAnimationChannel::rotation  ? t.rotation
                                                                     : t.scale;
    require(key < c.keys.size(), "Stored compressed key index exceeds channel");
    const double frame = !c.frames.empty()   ? double(c.frames[key])
                         : c.keys.size() > 1 ? double(key) * double(v.frames - 1) / double(c.keys.size() - 1)
                                             : 0.;
    Json values = Json::array();
    for (unsigned i = 0; i < (which == CompressedAnimationChannel::rotation ? 4u : 3u); ++i)
        values.push_back(c.keys[key][i]);
    return {{"index", key}, {"frame", frame}, {"value", std::move(values)}};
}
Json PerTrackAnimation::key_preview(std::size_t track, std::uint32_t maximum,
                                    const OperationControl &control) const {
    control.check();
    require(track < impl_->tracks.size(), "Compressed track index exceeds table");
    limit(maximum >= 2 && maximum <= 4096, "Key preview point count must be between 2 and 4096");
    const auto &t = impl_->tracks[track];
    Json j = {{"track", track},
              {"skeletonIndex", t.skeleton},
              {"frameCount", impl_->frames},
              {"bufferSha256", impl_->sha256},
              {"scaleOffsetsPresent", impl_->scale_present}};
    unsigned index = 0;
    for (const auto *c : {&t.translation, &t.rotation, &t.scale}) {
        auto channel = channel_json(*c, index == 1, false, false);
        const auto count = c->keys.size();
        const auto n = std::min<std::size_t>(count, maximum);
        channel["points"] = Json::array();
        channel["omittedKeys"] = count - n;
        if (!count)
            channel["implicitValue"] = index == 1                            ? Json({0, 0, 0, 1})
                                       : index == 2 && !impl_->scale_present ? Json({1, 1, 1})
                                                                             : Json({0, 0, 0});
        for (std::size_t i = 0; i < n; ++i) {
            control.check();
            const auto k = n > 1 ? std::uint64_t(i) * (count - 1) / (n - 1) : 0;
            channel["points"].push_back(stored_key(track, static_cast<CompressedAnimationChannel>(index),
                                                   static_cast<std::uint32_t>(k), control));
        }
        j[index == 0 ? "translation" : index == 1 ? "rotation" : "scale"] = std::move(channel);
        ++index;
    }
    return j;
}
CompressedAnimationBonePose PerTrackAnimation::sample_track(std::size_t track, double frame,
                                                            CompressedAnimationInterpolation interpolation,
                                                            const OperationControl &control) const {
    control.check();
    const auto &v = *impl_;
    require(track < v.tracks.size(), "Compressed track index exceeds table");
    require(std::isfinite(frame) && frame >= 0 && frame <= double(v.frames - 1),
            "Animation sample frame exceeds sequence");
    support(interpolation == CompressedAnimationInterpolation::linear ||
                interpolation == CompressedAnimationInterpolation::step,
            "Unsupported animation interpolation");
    const float relative = v.frames > 1 ? float(frame / double(v.frames - 1)) : 0.f;
    const bool step = interpolation == CompressedAnimationInterpolation::step;
    const auto &t = v.tracks[track];
    CompressedAnimationBonePose p;
    p.track = static_cast<std::uint32_t>(track);
    p.skeleton_index = t.skeleton;
    p.translation = vector_sample(t.translation, v.frames, relative, step, {0, 0, 0});
    p.rotation = rotation_sample(t.rotation, v.frames, relative, step);
    p.scale =
        vector_sample(t.scale, v.frames, relative, step,
                      v.scale_present ? std::array<double, 3>{0, 0, 0} : std::array<double, 3>{1, 1, 1});
    return p;
}
std::vector<CompressedAnimationBonePose>
PerTrackAnimation::sample(double frame, CompressedAnimationInterpolation interpolation,
                          const OperationControl &control) const {
    control.check();
    const auto &v = *impl_;
    require(std::isfinite(frame) && frame >= 0 && frame <= double(v.frames - 1),
            "Animation sample frame exceeds sequence");
    support(interpolation == CompressedAnimationInterpolation::linear ||
                interpolation == CompressedAnimationInterpolation::step,
            "Unsupported animation interpolation");
    const auto relative = v.frames > 1 ? float(frame / double(v.frames - 1)) : 0.f;
    const bool step = interpolation == CompressedAnimationInterpolation::step;
    std::vector<CompressedAnimationBonePose> out;
    out.reserve(v.tracks.size());
    for (std::size_t i = 0; i < v.tracks.size(); ++i) {
        control.checkpoint("sample-compressed-animation", i, v.tracks.size());
        const auto &t = v.tracks[i];
        CompressedAnimationBonePose p;
        p.track = static_cast<std::uint32_t>(i);
        p.skeleton_index = t.skeleton;
        p.translation = vector_sample(t.translation, v.frames, relative, step, {0, 0, 0});
        p.rotation = rotation_sample(t.rotation, v.frames, relative, step);
        p.scale =
            vector_sample(t.scale, v.frames, relative, step,
                          v.scale_present ? std::array<double, 3>{0, 0, 0} : std::array<double, 3>{1, 1, 1});
        out.push_back(p);
    }
    control.checkpoint("sample-compressed-animation", out.size(), out.size());
    return out;
}
PerTrackAnimation decode_per_track_animation(const PerTrackAnimationSource &s,
                                             const PerTrackAnimationOptions &o) {
    o.control.check();
    support(o.profile == PerTrackAnimationProfile::unreal_5_3_windows,
            "An explicit supported per-track animation profile is required");
    limit(s.buffer.size() <= o.max_read_bytes, "Animation exceeds read byte budget");
    limit(s.track_to_skeleton.size() <= o.max_tracks && s.frame_count <= o.max_frames,
          "Animation exceeds track or frame budget");
    require(s.frame_count != 0 && s.frame_count <= 0x7fffffffu,
            "Compressed animation frame count exceeds serialized signed range");
    require(s.track_offset_count <= 0x7fffffffu && s.scale_offset_count <= 0x7fffffffu &&
                s.key_byte_count <= 0x7fffffffu && s.scale_strip_size > 0 &&
                s.scale_strip_size <= 0x7fffffffu,
            "Compressed animation views exceed serialized signed range");
    require(std::uint64_t(s.track_to_skeleton.size()) * 2 == s.track_offset_count,
            "Animation track offset view disagrees with track map");
    support(s.scale_offset_count == 0 || s.scale_strip_size == 1,
            "Per-track scale offset view requires strip size one");
    require(!s.scale_offset_count || s.scale_offset_count == s.track_to_skeleton.size(),
            "Animation scale offset view disagrees with track map");
    const auto key_start = (std::uint64_t(s.track_offset_count) + s.scale_offset_count) * 4;
    require(key_start + s.key_byte_count == s.buffer.size(),
            "Serialized animation views do not exactly cover compressed buffer");
    Budget budget{o};
    budget.reserve(sizeof(PerTrackAnimation::Impl));
    budget.reserve(std::uint64_t(s.track_to_skeleton.size()) *
                   (sizeof(Track) + sizeof(CompressedAnimationBonePose)));
    auto v = std::make_shared<PerTrackAnimation::Impl>();
    v->frames = s.frame_count;
    v->scale_present = s.scale_offset_count != 0;
    v->scale_strip_size = s.scale_strip_size;
    v->buffer_bytes = s.buffer.size();
    v->key_bytes = s.key_byte_count;
    v->sha256 = sha256_bytes(s.buffer, o.control);
    v->io_hash = blake3_bytes(s.buffer, o.control).substr(0, 40);
    v->tracks.reserve(s.track_to_skeleton.size());
    std::set<std::int32_t> bones;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> windows;
    Cursor offsets{s.buffer, 0, key_start}, bytes{s.buffer, key_start, s.buffer.size()};
    for (std::size_t i = 0; i < s.track_to_skeleton.size(); ++i) {
        o.control.checkpoint("decode-compressed-animation", i, s.track_to_skeleton.size());
        const auto bone = s.track_to_skeleton[i];
        require(bone >= 0 && bones.insert(bone).second, "Animation track map has invalid or duplicate bone");
        Track t;
        t.skeleton = bone;
        const auto translation = offsets.i32(), rotation = offsets.i32();
        t.translation = channel(bytes, key_start, translation, false, s.frame_count, budget, windows);
        t.rotation = channel(bytes, key_start, rotation, true, s.frame_count, budget, windows);
        if (s.scale_offset_count) {
            Cursor scale{s.buffer, std::uint64_t(s.track_offset_count) * 4 + i * 4, key_start};
            t.scale = channel(bytes, key_start, scale.i32(), false, s.frame_count, budget, windows);
        }
        v->tracks.push_back(std::move(t));
    }
    std::sort(windows.begin(), windows.end());
    std::uint64_t used = 0, last_begin = key_start, last_end = key_start;
    for (const auto &w : windows) {
        o.control.check();
        if (w.first < last_end) {
            require(w.first == last_begin && w.second == last_end,
                    "Animation channel windows partially overlap", w.first);
        } else {
            used += w.second - w.first;
            last_begin = w.first;
            last_end = w.second;
        }
    }
    require(used <= s.key_byte_count, "Animation channel union exceeds key bytes");
    v->unreferenced_bytes = s.key_byte_count - used;
    v->decoded_keys = budget.keys;
    o.control.checkpoint("decode-compressed-animation", v->tracks.size(), v->tracks.size());
    return PerTrackAnimation(std::move(v));
}
} // namespace cyuediff
