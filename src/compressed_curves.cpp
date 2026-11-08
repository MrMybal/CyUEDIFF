#include <cyuediff/compressed_curves.hpp>
#include <cyuediff/content_hash.hpp>
#include "archive.hpp"
#include "string_value.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
namespace cyuediff {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Curve profile requires IEEE754 binary32");
namespace {
void need(bool ok, const char *message, std::uint64_t pos = 0) {
    if (!ok)
        throw Error("curve-compression-layout", message, pos);
}
void limit(bool ok, const char *message) {
    if (!ok)
        throw Error("curve-compression-limit", message);
}
void support(bool ok, const char *message) {
    if (!ok)
        throw Error("curve-compression-feature", message);
}
struct Reader {
    const std::vector<std::uint8_t> &bytes;
    std::uint64_t pos = 0, end = 0;
    void range(std::uint64_t n) const {
        need(pos <= end && n <= end - pos, "Curve field exceeds bounded buffer", pos);
    }
    std::uint32_t u(unsigned n = 4) {
        range(n);
        std::uint32_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= std::uint32_t(bytes[std::size_t(pos++)]) << (8 * i);
        return v;
    }
    float f() {
        auto v = u();
        float x;
        std::memcpy(&x, &v, 4);
        need(std::isfinite(x), "Non-finite compressed curve float", pos - 4);
        return x;
    }
};
std::uint64_t aligned(std::uint64_t n, unsigned a) {
    return (n + a - 1) / a * a;
}
struct Key {
    float time = 0, value = 0, arrive = 0, leave = 0, arrive_weight = 0, leave_weight = 0;
    unsigned mode = 2, weight_mode = 0;
};
struct Curve {
    std::string name, hash;
    unsigned format = 0, time_format = 1, pre = 4, post = 4;
    float constant = 0;
    bool empty = false, uniform = false, constant_sample = false;
    std::uint64_t begin = 0, bytes = 0;
    std::vector<Key> keys;
};
struct Budget {
    const CompressedCurveOptions &o;
    std::uint64_t output = 0, keys = 0, text = 0;
    void reserve(std::uint64_t n) {
        limit(output <= o.max_output_bytes && n <= o.max_output_bytes - output,
              "Decoded curve output exceeds budget");
        output += n;
    }
    void add_keys(std::uint64_t n) {
        limit(keys <= o.max_keys && n <= o.max_keys - keys, "Compressed curves exceed aggregate key budget");
        reserve(n * sizeof(Key));
        keys += n;
    }
};
const char *codec_name(CompressedCurveCodec c) {
    return c == CompressedCurveCodec::rich_curve          ? "AnimCurveCompressionCodec_CompressedRichCurve"
           : c == CompressedCurveCodec::uniform_indexable ? "AnimCurveCompressionCodec_UniformIndexable"
                                                          : "AnimCurveCompressionCodec_UniformlySampled";
}
Json key_json(const Key &k, std::size_t index, bool uniform) {
    Json j = {{"index", index},
              {"time", k.time},
              {"value", k.value},
              {"timeSource", uniform ? "derived-uniform-sample-grid" : "decoded-key-time"}};
    if (!uniform) {
        j["interpolation"] = k.mode;
        j["tangentWeightMode"] = k.weight_mode;
        j["arriveTangent"] = k.arrive;
        j["leaveTangent"] = k.leave;
        j["arriveTangentWeight"] = k.arrive_weight;
        j["leaveTangentWeight"] = k.leave_weight;
    }
    return j;
}
Curve read_rich(const CompressedCurveSource &s, std::size_t index, Budget &b) {
    Reader d{s.buffer, index * 12, s.buffer.size()};
    Curve c;
    c.name = s.names[index];
    c.format = d.u(1);
    c.time_format = d.u(1);
    c.pre = d.u(1);
    c.post = d.u(1);
    support(c.format <= 5 && c.time_format <= 1 && c.pre <= 5 && c.post <= 5,
            "Unsupported compressed rich curve tags");
    const auto count_or_value = d.u();
    const auto offset = d.u();
    need(offset >= s.names.size() * 12 && offset <= s.buffer.size(), "Rich curve key offset exceeds data",
         offset);
    c.begin = offset;
    if (c.format <= 1) {
        std::memcpy(&c.constant, &count_or_value, 4);
        need(std::isfinite(c.constant), "Non-finite compressed curve constant");
        c.empty = c.format == 0;
        return c;
    }
    const auto count = count_or_value;
    need(count >= 2 && count <= 0x7fffffffu, "Invalid compressed rich curve key count");
    b.add_keys(count);
    const std::uint64_t modes = c.format == 4 ? count : c.format == 5 ? std::uint64_t(count) * 2 : 0;
    const auto time_at = aligned(modes, c.time_format == 0 ? 2 : 4);
    const auto times_end = time_at + std::uint64_t(count) * (c.time_format == 0 ? 2 : 4);
    const auto data_at = aligned(times_end, 4) + (c.time_format == 0 ? 8 : 0);
    const unsigned components = c.format == 5 ? 5 : c.format == 3 || c.format == 4 ? 3 : 1;
    c.bytes = data_at + std::uint64_t(count) * components * 4;
    Reader r{s.buffer, offset, s.buffer.size()};
    r.range(c.bytes);
    c.keys.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        b.o.control.check();
        auto &k = c.keys[i];
        k.mode = c.format == 4 || c.format == 5 ? r.u(1) : c.format;
        support(k.mode >= 1 && k.mode <= 3, "Unsupported compressed key interpolation");
        if (c.format == 5) {
            k.weight_mode = r.u(1);
            support(k.weight_mode <= 3, "Unsupported compressed tangent weight mode");
        }
    }
    float minimum = 0, span = 0;
    if (c.time_format == 0) {
        Reader range{s.buffer, offset + aligned(times_end, 4), offset + c.bytes};
        minimum = range.f();
        span = range.f();
        need(span > 0, "Quantized curve time span must be positive");
    }
    r.pos = offset + time_at;
    for (std::uint32_t i = 0; i < count; ++i) {
        b.o.control.check();
        const float t = c.time_format == 0 ? (float(r.u(2)) * (1.f / 65535.f)) * span + minimum : r.f();
        need(std::isfinite(t) && (!i || t >= c.keys[i - 1].time),
             "Unordered or non-finite compressed key times", r.pos);
        c.keys[i].time = t;
    }
    need(c.keys.back().time > c.keys.front().time, "Compressed key time domain is empty");
    r.pos = offset + data_at;
    for (auto &k : c.keys) {
        b.o.control.check();
        k.value = r.f();
        if (components >= 3) {
            k.arrive = r.f();
            k.leave = r.f();
        }
        if (components == 5) {
            k.arrive_weight = r.f();
            k.leave_weight = r.f();
        }
    }
    return c;
}
float checked_float(double x, const char *message) {
    need(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max(), message);
    const float f = static_cast<float>(x);
    need(std::isfinite(f), message);
    return f;
}
float lerp(float a, float b, float alpha) {
    return a + alpha * (b - a);
}
float bezier(float a, float b, float c, float d, float t) {
    const float ab = lerp(a, b, t), bc = lerp(b, c, t), cd = lerp(c, d, t);
    return lerp(lerp(ab, bc, t), lerp(bc, cd, t), t);
}
} // namespace
struct CompressedCurves::Impl {
    CompressedCurveCodec codec;
    std::vector<std::uint8_t> raw;
    std::vector<Curve> curves;
    std::string sha256, io_hash;
    std::uint64_t unreferenced = 0;
    float sample_rate = 0;
    std::uint32_t samples = 0;
};
CompressedCurves::CompressedCurves(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
std::size_t CompressedCurves::curve_count() const {
    return impl_->curves.size();
}
CompressedCurves decode_compressed_curves(const CompressedCurveSource &s, const CompressedCurveOptions &o) {
    o.control.check();
    support(o.profile == CompressedCurveProfile::unreal_5_3_windows,
            "Explicit UE 5.3 Windows curve profile required");
    support(s.codec == CompressedCurveCodec::rich_curve ||
                s.codec == CompressedCurveCodec::uniform_indexable ||
                s.codec == CompressedCurveCodec::uniformly_sampled,
            "Explicit supported curve codec required");
    limit(s.buffer.size() <= o.max_read_bytes && s.names.size() <= o.max_curves,
          "Compressed curve read/count budget exceeded");
    Budget budget{o};
    budget.reserve(sizeof(CompressedCurves::Impl) + s.buffer.size() + s.names.size() * sizeof(Curve));
    std::set<std::string> names;
    for (const auto &name : s.names) {
        o.control.check();
        limit(name.size() <= o.max_text_bytes - budget.text, "Curve names exceed text budget");
        budget.text += name.size();
        budget.reserve(name.size() * 2 + 256);
        detail::validate_byte_string(name, false, o.control, "curve-compression-layout", 0);
        need(!name.empty() && name != "None" && names.insert(name).second,
             "Invalid or duplicate curve identity");
    }
    auto v = std::make_shared<CompressedCurves::Impl>();
    v->codec = s.codec;
    v->curves.reserve(s.names.size());
    v->raw = s.buffer;
    v->sha256 = sha256_bytes(s.buffer, o.control);
    v->io_hash = blake3_bytes(s.buffer, o.control).substr(0, 40);
    std::uint64_t covered = 0;
    if (s.codec == CompressedCurveCodec::rich_curve) {
        const auto header = std::uint64_t(s.names.size()) * 12;
        Reader r{s.buffer, 0, s.buffer.size()};
        r.range(header);
        covered = header;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> windows;
        budget.reserve(s.names.size() * sizeof(std::pair<std::uint64_t, std::uint64_t>));
        windows.reserve(s.names.size());
        for (std::size_t i = 0; i < s.names.size(); ++i) {
            o.control.checkpoint("decode-compressed-curves", i, s.names.size());
            auto c = read_rich(s, i, budget);
            if (c.bytes)
                windows.push_back({c.begin, c.begin + c.bytes});
            std::vector<std::uint8_t> fingerprint(s.buffer.begin() + i * 12, s.buffer.begin() + i * 12 + 8);
            const auto key_hash = detail::hash_range(s.buffer, c.begin, c.bytes, o.control);
            fingerprint.insert(fingerprint.end(), key_hash.begin(), key_hash.end());
            c.hash = sha256_bytes(fingerprint, o.control);
            v->curves.push_back(std::move(c));
        }
        std::sort(windows.begin(), windows.end());
        std::pair<std::uint64_t, std::uint64_t> previous{header, header};
        for (const auto &w : windows) {
            if (w == previous)
                continue;
            need(w.first >= previous.second, "Partially overlapping compressed curve windows", w.first);
            covered += w.second - w.first;
            previous = w;
        }
    } else {
        Reader r{s.buffer, 0, s.buffer.size()};
        const auto constants = s.codec == CompressedCurveCodec::uniformly_sampled ? r.u() : 0;
        const auto n = r.u();
        const float rate = r.f();
        need(constants <= s.names.size() && n >= 1 && n <= 0x7fffffffu && rate > 0,
             "Invalid uniform curve sample metadata");
        v->samples = n;
        v->sample_rate = rate;
        const auto bits_at = r.pos;
        const std::uint64_t bit_bytes =
            s.codec == CompressedCurveCodec::uniformly_sampled ? ((s.names.size() + 31) / 32) * 4 : 0;
        r.range(bit_bytes);
        r.pos += bit_bytes;
        const auto constants_at = r.pos;
        const auto animated = s.names.size() - constants;
        const auto animated_at = constants_at + std::uint64_t(constants) * 4;
        const auto end = animated_at + std::uint64_t(animated) * n * 4;
        need(end <= s.buffer.size(), "Uniform curve samples exceed buffer", end);
        covered = end;
        std::uint32_t constant_index = 0, animated_index = 0;
        for (std::size_t i = 0; i < s.names.size(); ++i) {
            o.control.checkpoint("decode-compressed-curves", i, s.names.size());
            Curve c;
            c.name = s.names[i];
            c.uniform = true;
            c.format = 2;
            const bool constant = bit_bytes && (s.buffer[std::size_t(bits_at + i / 8)] & (1u << (i % 8)));
            c.constant_sample = constant;
            const std::uint32_t count = constant ? 1 : n;
            budget.add_keys(count);
            c.keys.reserve(count);
            if (constant)
                need(constant_index < constants, "Uniform constant bitset exceeds declared count");
            else
                need(animated_index < animated, "Uniform animated bitset exceeds declared count");
            std::vector<std::uint8_t> fingerprint;
            budget.reserve(std::uint64_t(count) * 4 + 12);
            fingerprint.reserve(std::size_t(count) * 4 + 12);
            for (std::uint32_t k = 0; k < count; ++k) {
                o.control.check();
                const auto at = constant ? constants_at + std::uint64_t(constant_index) * 4
                                : s.codec == CompressedCurveCodec::uniform_indexable
                                    ? animated_at + (std::uint64_t(i) * n + k) * 4
                                    : animated_at + (std::uint64_t(k) * animated + animated_index) * 4;
                Reader sample{s.buffer, at, end};
                const float value = sample.f();
                const float time = float(k) * (1.f / rate);
                need(std::isfinite(time), "Uniform key time overflow");
                Key key;
                key.time = time;
                key.value = value;
                key.mode = constant ? 1 : 2;
                c.keys.push_back(key);
                fingerprint.insert(fingerprint.end(), s.buffer.begin() + std::size_t(at),
                                   s.buffer.begin() + std::size_t(at + 4));
            }
            fingerprint.push_back(constant ? 1 : 0);
            const auto rate_bits = reinterpret_cast<const std::uint8_t *>(&rate);
            // Copy bytes explicitly for the already required little-endian wire profile.
            std::uint32_t rb = 0;
            std::memcpy(&rb, rate_bits, 4);
            for (unsigned j = 0; j < 4; ++j)
                fingerprint.push_back(std::uint8_t(rb >> (8 * j)));
            c.hash = sha256_bytes(fingerprint, o.control);
            c.bytes = std::uint64_t(count) * 4;
            c.constant = constant ? c.keys[0].value : 0;
            v->curves.push_back(std::move(c));
            if (constant)
                ++constant_index;
            else
                ++animated_index;
        }
        need(constant_index == constants && animated_index == animated,
             "Uniform constant bitset disagrees with declared count");
    }
    need(covered <= s.buffer.size(), "Curve coverage exceeds buffer");
    v->unreferenced = s.buffer.size() - covered;
    o.control.checkpoint("decode-compressed-curves", v->curves.size(), v->curves.size());
    return CompressedCurves(std::move(v));
}

namespace {
float weighted(const Key &a, const Key &b, float time, const OperationControl &control) {
    const float width = b.time - a.time, alpha = (time - a.time) / width;
    auto tangent = [&](float slope, float weight, bool active) {
        const float angle = std::atan(slope), x = width, y = slope * x;
        const float length = active ? weight : std::sqrt(x * x + y * y) * (1.f / 3.f);
        return std::array<float, 2>{std::cos(angle) * length, std::sin(angle) * length};
    };
    const auto left = tangent(a.leave, a.leave_weight, a.weight_mode == 2 || a.weight_mode == 3);
    const auto right = tangent(b.arrive, b.arrive_weight, b.weight_mode == 1 || b.weight_mode == 3);
    const float x1 = ((a.time + left[0]) - a.time) / width, x2 = ((b.time - right[0]) - a.time) / width;
    const float y1 = a.value + left[1], y2 = b.value - right[1];
    for (float v : {width, alpha, x1, x2, y1, y2})
        need(std::isfinite(v), "Weighted curve arithmetic overflow");
    auto x = [&](double t) {
        const double v = 1. - t;
        return 3 * v * v * t * double(x1) + 3 * v * t * t * double(x2) + t * t * t;
    };
    std::vector<double> cuts{0., 1.};
    const double qa = 1. + 3. * x1 - 3. * x2, qb = 2. * x2 - 4. * x1, qc = x1;
    auto add = [&](double r) {
        if (std::isfinite(r) && r > 0 && r < 1)
            cuts.push_back(r);
    };
    if (std::abs(qa) < 1e-14) {
        if (qb != 0)
            add(-qc / qb);
    } else {
        const double d = qb * qb - 4 * qa * qc;
        if (d >= 0) {
            add((-qb + std::sqrt(d)) / (2 * qa));
            add((-qb - std::sqrt(d)) / (2 * qa));
        }
    }
    std::sort(cuts.begin(), cuts.end());
    double chosen = -1;
    for (double c : cuts)
        if (std::abs(x(c) - alpha) < 1e-10)
            chosen = std::max(chosen, c);
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        control.check();
        double lo = cuts[i - 1], hi = cuts[i], xl = x(lo), xh = x(hi);
        if (alpha < std::min(xl, xh) || alpha > std::max(xl, xh))
            continue;
        const bool ascending = xh >= xl;
        for (unsigned n = 0; n < 80; ++n) {
            control.check();
            const double mid = (lo + hi) * .5;
            if (mid == lo || mid == hi)
                break;
            if ((x(mid) < alpha) == ascending)
                lo = mid;
            else
                hi = mid;
        }
        const double root = (lo + hi) * .5;
        if (std::abs(x(root) - alpha) <= 1e-8)
            chosen = std::max(chosen, root);
    }
    need(chosen >= 0, "Weighted compressed curve time cannot be resolved");
    return bezier(a.value, y1, y2, b.value, static_cast<float>(chosen));
}
float rich_sample(const Curve &c, const std::vector<std::uint8_t> &raw, float time, float fallback,
                  const OperationControl &control) {
    if (c.format <= 1)
        return c.empty && c.constant == std::numeric_limits<float>::max() ? fallback : c.constant;
    const auto &keys = c.keys;
    const auto &first = keys.front(), &last = keys.back();
    float offset = 0;
    auto extrapolate = [&](bool before, unsigned mode) {
        const auto &a = before ? first : last, &b = before ? keys[1] : keys[keys.size() - 2];
        if (mode != 3 || std::abs(b.time - a.time) <= 1e-8f)
            return a.value;
        return ((b.value - a.value) / (b.time - a.time)) * (time - a.time) + a.value;
    };
    auto cycle = [&](bool before, unsigned mode) {
        const float original = time, width = last.time - first.time;
        const float q = before ? (time - first.time) / width : (last.time - time) / width;
        const double integer = std::floor(double(q));
        need(std::isfinite(integer) && integer >= -2147483647. && integer <= 2147483647.,
             "Curve extrapolation cycle count exceeds native domain");
        const auto cycles = static_cast<std::int32_t>(integer);
        time = before ? time - width * float(cycles) : time + width * float(cycles);
        if (time == last.time && original < first.time)
            time = first.time;
        if (time == first.time && original > last.time)
            time = last.time;
        const auto count = std::uint32_t(cycles < 0 ? -std::int64_t(cycles) : cycles);
        if (mode == 1)
            offset = (before ? first.value - last.value : last.value - first.value) * float(count);
        if (mode == 2 && count % 2)
            time = first.time + (last.time - time);
        need(std::isfinite(time) && std::isfinite(offset), "Curve cycle arithmetic overflow");
    };
    if (time < first.time) {
        if (c.pre == 3 || c.pre == 4)
            return extrapolate(true, c.pre);
        cycle(true, c.pre);
    }
    if (time > last.time) {
        if (c.post == 3 || c.post == 4)
            return extrapolate(false, c.post);
        cycle(false, c.post);
    }
    const auto it = std::upper_bound(keys.begin() + 1, keys.end() - 1, time,
                                     [](float t, const Key &k) { return t < k.time; });
    const auto right = std::size_t(it - keys.begin()), left = right - 1;
    const auto &a = keys[left], &b = keys[right];
    const float width = b.time - a.time;
    float value = a.value;
    if (width > 0 && a.mode != 1) {
        const float alpha = (time - a.time) / width;
        if (a.mode == 2)
            value = lerp(a.value, b.value, alpha);
        else {
            bool unweighted = true;
            if (c.format == 5) {
                // This explicit UE 5.3 profile reads weight selectors using native key-data handles.
                // Keep those reads within the owned encoded window, including native layout quirks.
                const auto x = std::uint64_t(left) * 10 + 1, y = std::uint64_t(right) * 10 + 1;
                need(x < c.bytes && y < c.bytes, "Native tangent selector exceeds curve window");
                const auto m0 = raw[std::size_t(c.begin + x)], m1 = raw[std::size_t(c.begin + y)];
                unweighted = (m0 == 0 || m0 == 1) && (m1 == 0 || m1 == 2);
            }
            if (unweighted) {
                const float third = width * (1.f / 3.f);
                value =
                    bezier(a.value, a.value + a.leave * third, b.value - b.arrive * third, b.value, alpha);
            } else {
                // The selected native weighted branch returns before adding the cycle-value offset.
                return weighted(a, b, time, control);
            }
        }
    }
    return value + offset;
}
Json metadata(const Curve &c, std::size_t i) {
    return {{"index", i},
            {"name", c.name},
            {"keys", c.keys.size()},
            {"format", c.format},
            {"timeFormat", c.time_format},
            {"preInfinity", c.pre},
            {"postInfinity", c.post},
            {"constantValue", c.format <= 1 || c.constant_sample ? Json(c.constant) : Json(nullptr)},
            {"empty", c.empty},
            {"emptyUsesCallerDefault", c.empty && c.constant == std::numeric_limits<float>::max()},
            {"uniformSamples", c.uniform},
            {"constantSamples", c.constant_sample},
            {"offset", c.uniform ? Json(nullptr) : Json(c.begin)},
            {"encodedBytes", c.bytes},
            {"encodingSha256", c.hash}};
}
} // namespace
Json CompressedCurves::catalog(const OperationControl &control) const {
    return to_json(false, control);
}
Json CompressedCurves::to_json(bool include_keys, const OperationControl &control) const {
    control.check();
    const auto &v = *impl_;
    Json out = {{"schema", "cyuediff.compressed-curves/1"},
                {"profile", "ue5.3-windows"},
                {"codec", codec_name(v.codec)},
                {"scope", "encoded-order scalar curves; no runtime blending or filtering"},
                {"bufferBytes", v.raw.size()},
                {"bufferSha256", v.sha256},
                {"bufferIoHash", v.io_hash},
                {"unreferencedBytes", v.unreferenced},
                {"sampleRate", v.sample_rate},
                {"sampleCount", v.samples},
                {"curves", Json::array()}};
    for (std::size_t i = 0; i < v.curves.size(); ++i) {
        control.check();
        auto row = metadata(v.curves[i], i);
        if (include_keys) {
            row["values"] = Json::array();
            for (std::size_t k = 0; k < v.curves[i].keys.size(); ++k) {
                control.check();
                row["values"].push_back(key_json(v.curves[i].keys[k], k, v.curves[i].uniform));
            }
        }
        out["curves"].push_back(std::move(row));
    }
    return out;
}
Json CompressedCurves::stored_key(std::size_t curve, std::uint32_t key,
                                  const OperationControl &control) const {
    control.check();
    need(curve < impl_->curves.size(), "Compressed curve index exceeds table");
    const auto &c = impl_->curves[curve];
    need(key < c.keys.size(), "Compressed curve key index exceeds table");
    return key_json(c.keys[key], key, c.uniform);
}
Json CompressedCurves::key_preview(std::size_t curve, std::uint32_t maximum,
                                   const OperationControl &control) const {
    control.check();
    need(curve < impl_->curves.size(), "Compressed curve index exceeds table");
    limit(maximum >= 2 && maximum <= 4096, "Curve key preview count must be between 2 and 4096");
    const auto &c = impl_->curves[curve];
    auto out = metadata(c, curve);
    const auto count = c.keys.size(), n = std::min<std::size_t>(maximum, count);
    out["points"] = Json::array();
    out["omittedKeys"] = count - n;
    for (std::size_t i = 0; i < n; ++i) {
        control.check();
        const auto k = n > 1 ? std::uint64_t(i) * (count - 1) / (n - 1) : 0;
        out["points"].push_back(key_json(c.keys[std::size_t(k)], std::size_t(k), c.uniform));
    }
    return out;
}
double CompressedCurves::sample_curve(std::size_t curve, double seconds, double empty_default,
                                      const OperationControl &control) const {
    control.check();
    need(curve < impl_->curves.size(), "Compressed curve index exceeds table");
    const float time = checked_float(seconds, "Invalid curve sample time"),
                fallback = checked_float(empty_default, "Invalid empty curve default");
    const auto &v = *impl_;
    const auto &c = v.curves[curve];
    float value = 0;
    if (v.codec == CompressedCurveCodec::rich_curve)
        value = rich_sample(c, v.raw, time, fallback, control);
    else if (c.keys.size() == 1)
        value = c.keys[0].value;
    else {
        const float point = time * v.sample_rate;
        need(std::isfinite(point) && point >= -2147483647.f && point < 2147483647.f,
             "Uniform curve sample point exceeds native integer domain");
        const auto i0 = std::size_t(std::clamp(std::floor(double(point)), 0., double(c.keys.size() - 1))),
                   i1 = std::min(i0 + 1, c.keys.size() - 1);
        value = lerp(c.keys[i0].value, c.keys[i1].value, point - float(i0));
    }
    need(std::isfinite(value), "Compressed curve sample arithmetic overflow");
    return value;
}
Json CompressedCurves::sample(double seconds, double empty_default, const OperationControl &control) const {
    control.check();
    checked_float(seconds, "Invalid curve sample time");
    checked_float(empty_default, "Invalid empty curve default");
    Json out = {{"schema", "cyuediff.compressed-curve-sample/1"},
                {"time", seconds},
                {"nativeTime", static_cast<float>(seconds)},
                {"scope", "encoded-order scalar curves; no runtime blending or filtering"},
                {"values", Json::array()}};
    for (std::size_t i = 0; i < impl_->curves.size(); ++i) {
        control.checkpoint("sample-compressed-curves", i, impl_->curves.size());
        out["values"].push_back({{"index", i},
                                 {"name", impl_->curves[i].name},
                                 {"value", sample_curve(i, seconds, empty_default, control)}});
    }
    return out;
}
} // namespace cyuediff
