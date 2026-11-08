#include "nanite_attributes_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace cyuediff {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw Error("nanite-attributes", message);
}
std::int32_t signed_bits(std::uint32_t value) {
    std::int32_t out;
    std::memcpy(&out, &value, 4);
    return out;
}
std::uint32_t mask(unsigned n) {
    return n ? (1u << n) - 1u : 0u;
}
std::int32_t uv_integer(std::uint32_t packed, const detail::NaniteUvRange &r, unsigned k) {
    const auto extra = packed > r.gap_start[k] ? r.gap_length[k] : 0u;
    return signed_bits(packed + extra + static_cast<std::uint32_t>(r.minimum[k]));
}
std::int32_t nearest_even(float value) {
    require(std::isfinite(value), "Nanite UV rescaling is non-finite");
    const double low = std::floor(static_cast<double>(value));
    const double fraction = static_cast<double>(value) - low;
    const double rounded = low + (fraction > .5 || (fraction == .5 && std::fmod(low, 2.) != 0.) ? 1. : 0.);
    require(rounded >= INT32_MIN && rounded <= INT32_MAX, "Nanite UV rescaling exceeds native int32");
    return static_cast<std::int32_t>(rounded);
}
std::array<float, 3> oct_normal(std::uint32_t bits, unsigned precision) {
    const float scale = 2.f / static_cast<float>(mask(precision));
    std::array<float, 3> n{static_cast<float>(bits & mask(precision)) * scale - 1.f,
                           static_cast<float>((bits >> precision) & mask(precision)) * scale - 1.f, 0};
    n[2] = 1.f - std::abs(n[0]) - std::abs(n[1]);
    const float fold = std::min(1.f, std::max(-n[2], 0.f));
    n[0] += n[0] >= 0 ? -fold : fold;
    n[1] += n[1] >= 0 ? -fold : fold;
    const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (auto &v : n)
        v /= length;
    return n;
}
std::array<float, 4> tangent(std::array<float, 3> n, std::uint32_t bits, unsigned precision) {
    const bool swap = std::abs(n[2]) > std::abs(n[0]);
    if (swap)
        std::swap(n[0], n[2]);
    const std::array<float, 3> x{-n[1], n[0], 0}, y{-n[2] * n[0], -n[2] * n[1], n[0] * n[0] + n[1] * n[1]};
    const float factor = 1.f / std::sqrt(x[0] * x[0] + x[1] * x[1]);
    const float angle = static_cast<float>(bits & mask(precision)) *
                        ((2.f * 3.14159265358979323846f) / static_cast<float>(1u << precision));
    const float a = std::cos(angle) * factor, b = std::sin(angle) * factor;
    std::array<float, 4> out{x[0] * a + y[0] * b, x[1] * a + y[1] * b, x[2] * a + y[2] * b,
                             (bits & (1u << precision)) ? -1.f : 1.f};
    if (swap)
        std::swap(out[0], out[2]);
    return out;
}
} // namespace
namespace detail {
NaniteAttributeLayout nanite_attribute_layout(const std::array<std::uint32_t, 28> &w,
                                              const std::array<std::array<std::uint32_t, 8>, 4> &ranges,
                                              unsigned uv_count) {
    NaniteAttributeLayout out;
    out.normal = (w[7] >> 24) & 15;
    out.tangent = (w[7] >> 28) & 15;
    out.has_tangent = (w[21] & (1u << 22)) != 0;
    out.uv_count = uv_count;
    out.color_mode = (w[21] >> 26) & 3;
    out.color_min = w[2];
    require(uv_count <= 4 && out.color_mode <= 2 && (!out.has_tangent || out.tangent <= 12),
            "Unsupported Nanite attribute layout");
    unsigned total = 2 * out.normal + (out.has_tangent ? 1 + out.tangent : 0), colors = 0;
    for (unsigned k = 0; k < 4; ++k) {
        out.color_bits[k] = (w[3] >> (4 * k)) & 15;
        require(out.color_bits[k] <= 8, "Nanite color component exceeds byte precision");
        colors += out.color_bits[k];
    }
    require(out.color_mode == 2 || colors == 0, "Constant Nanite color has variable component bits");
    total += colors;
    for (unsigned u = 0; u < uv_count; ++u) {
        auto &r = out.ranges[u];
        r.precision = signed_bits(ranges[u][6]);
        require(r.precision >= -126 && r.precision <= 14, "Unsupported Nanite UV quantization exponent");
        for (unsigned k = 0; k < 2; ++k) {
            r.minimum[k] = signed_bits(ranges[u][k]);
            r.gap_start[k] = ranges[u][2 + k];
            r.gap_length[k] = ranges[u][4 + k];
            out.uv_bits[u][k] = (w[22] >> (8 * u + 4 * k)) & 15;
            total += out.uv_bits[u][k];
        }
    }
    require(total == (w[20] >> 22), "Nanite attribute fields disagree with stored stride");
    return out;
}
NaniteAttributeCode transcode_nanite_attribute(const NaniteAttributeCode &code,
                                               const NaniteAttributeLayout &src,
                                               const NaniteAttributeLayout &dst) {
    require(src.normal == dst.normal && src.has_tangent == dst.has_tangent &&
                (!src.has_tangent || src.tangent == dst.tangent) && src.uv_count == dst.uv_count,
            "Referenced Nanite shading formats disagree");
    auto out = code;
    if (dst.color_mode != 2)
        out.color = dst.color_min;
    else {
        const auto delta = code.color - dst.color_min;
        std::uint32_t packed_delta = 0;
        for (unsigned k = 0; k < 4; ++k) {
            const auto part = (delta >> (8 * k)) & 255;
            require(part <= mask(dst.color_bits[k]), "Referenced Nanite color exceeds destination precision");
            packed_delta |= part << (8 * k);
        }
        out.color = dst.color_min + packed_delta;
    }
    for (unsigned u = 0; u < dst.uv_count; ++u) {
        const auto &s = src.ranges[u], &d = dst.ranges[u];
        const int exponent = d.precision - s.precision;
        require(exponent >= -126 && exponent <= 127, "Unsupported Nanite UV reference scale");
        const float scale = std::ldexp(1.f, exponent);
        for (unsigned k = 0; k < 2; ++k) {
            const auto absolute = uv_integer(code.uv[u][k], s, k);
            const auto quantized = nearest_even(static_cast<float>(absolute) * scale);
            // Integer operations mirror the native unsigned bit representation, including wrap.
            auto value = static_cast<std::uint32_t>(std::max(
                signed_bits(static_cast<std::uint32_t>(quantized) - static_cast<std::uint32_t>(d.minimum[k])),
                0));
            const bool high = value > d.gap_start[k] + (d.gap_length[k] >> 1);
            const auto low = high ? d.gap_start[k] + 1u : 0u,
                       high_bound = high ? mask(dst.uv_bits[u][k]) : d.gap_start[k];
            if (high)
                value -= d.gap_length[k];
            value = std::min(std::max(value, low), high_bound);
            // The writer stores exactly the destination bit count.
            out.uv[u][k] = value & mask(dst.uv_bits[u][k]);
            require(std::isfinite(static_cast<float>(uv_integer(out.uv[u][k], d, k)) *
                                  std::ldexp(1.f, -d.precision)),
                    "Decoded Nanite UV is non-finite");
        }
    }
    return out;
}
NaniteVertexAttributes decode_nanite_attribute(const NaniteAttributeCode &code,
                                               const NaniteAttributeLayout &layout) {
    NaniteVertexAttributes out;
    out.packed_normal = code.normal;
    out.normal_precision = layout.normal;
    out.tangent_precision = layout.tangent;
    if (layout.normal)
        out.normal = oct_normal(code.normal, layout.normal);
    if (layout.has_tangent) {
        out.packed_tangent = code.tangent;
        if (out.normal)
            out.tangent = tangent(*out.normal, code.tangent, layout.tangent);
    }
    for (unsigned k = 0; k < 4; ++k)
        out.color[k] = static_cast<std::uint8_t>(code.color >> (8 * k));
    for (unsigned u = 0; u < layout.uv_count; ++u) {
        std::array<std::int32_t, 2> quantized{};
        std::array<float, 2> values{};
        const float scale = std::ldexp(1.f, -layout.ranges[u].precision);
        for (unsigned k = 0; k < 2; ++k) {
            quantized[k] = uv_integer(code.uv[u][k], layout.ranges[u], k);
            values[k] = static_cast<float>(quantized[k]) * scale;
            require(std::isfinite(values[k]), "Decoded Nanite UV is non-finite");
        }
        out.packed_uvs.push_back(code.uv[u]);
        out.quantized_uvs.push_back(quantized);
        out.uvs.push_back(values);
    }
    return out;
}
} // namespace detail
Json NaniteVertexAttributes::to_json() const {
    return {{"packedNormal", packed_normal},
            {"normalPrecision", normal_precision},
            {"normal", normal ? Json(*normal) : Json(nullptr)},
            {"normalStatus", normal ? "decoded" : "zero-bit-unavailable"},
            {"packedTangent", packed_tangent ? Json(*packed_tangent) : Json(nullptr)},
            {"tangentPrecision", tangent_precision},
            {"tangent", tangent ? Json(*tangent) : Json(nullptr)},
            {"tangentStatus", !packed_tangent ? "absent"
                              : tangent       ? "decoded"
                                              : "normal-unavailable"},
            {"color", color},
            {"packedUVs", packed_uvs},
            {"quantizedUVs", quantized_uvs},
            {"uvs", uvs}};
}
} // namespace cyuediff
