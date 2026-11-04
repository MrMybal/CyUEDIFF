#include "image_bounds.hpp"
#include <cyuediff/hdr.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace cyuediff {
namespace {
std::uint64_t samples(const ImageRgba32f &image) {
    auto count = std::uint64_t(image.width) * image.height;
    if (!count || count > SIZE_MAX / 4 || count * 4 != image.pixels.size())
        throw Error("hdr-layout", "Invalid RGBA32F buffer");
    return count;
}
unsigned stride(const std::string &format) {
    if (format == "TSF_R16F")
        return 2;
    if (format == "TSF_R32F" || format == "TSF_BGRE8" || format == "TSF_RGBE8_DEPRECATED")
        return 4;
    if (format == "TSF_RGBA16F")
        return 8;
    if (format == "TSF_RGBA32F")
        return 16;
    throw Error("texture-format", "Unsupported HDR source format: " + format);
}
float half(std::uint16_t bits) {
    auto exponent = (bits >> 10) & 31, fraction = bits & 1023;
    double result = exponent ? std::ldexp(1. + double(fraction) / 1024, int(exponent) - 15)
                             : std::ldexp(double(fraction), -24);
    if (exponent == 31)
        throw Error("hdr-sample", "Non-finite half-float source sample");
    return static_cast<float>(bits & 0x8000 ? -result : result);
}
float floating(const std::uint8_t *data, bool sixteen) {
    std::uint32_t bits = std::uint32_t(data[0]) | (std::uint32_t(data[1]) << 8);
    if (sixteen)
        return half(static_cast<std::uint16_t>(bits));
    bits |= (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    float result;
    std::memcpy(&result, &bits, 4);
    if (!std::isfinite(result))
        throw Error("hdr-sample", "Non-finite float source sample");
    return result;
}
} // namespace
bool is_hdr_format(const std::string &format) {
    return format == "TSF_R16F" || format == "TSF_R32F" || format == "TSF_RGBA16F" ||
           format == "TSF_RGBA32F" || format == "TSF_BGRE8" || format == "TSF_RGBE8_DEPRECATED";
}
ImageRgba32f decode_texture_hdr(const TextureSource &texture, std::uint32_t mip,
                                const TextureDecodeOptions &options) {
    options.control.checkpoint("Decoding HDR source");
    if (!texture.mips || texture.mips > 32 || mip >= texture.mips)
        throw Error("texture-mip", "Requested HDR mip does not exist");
    if (!texture.width || !texture.height)
        throw Error("texture-metadata", "HDR texture has no dimensions");
    if (texture.slices != 1 || texture.layers != 1 || texture.block_offsets != std::vector<std::uint64_t>{0})
        throw Error("texture-layout", "HDR decoding requires one 2D source block, layer and slice");
    if (texture.compression != "TSCF_None")
        throw Error("texture-codec", "HDR decoding currently requires raw source samples");
    const auto pixel_bytes = stride(texture.format);
    ImageRgba32f result;
    result.width = std::max(1u, texture.width >> mip);
    result.height = std::max(1u, texture.height >> mip);
    auto count = std::uint64_t(result.width) * result.height;
    if (count > options.max_pixels || count > SIZE_MAX / (4 * sizeof(float)) ||
        count > options.max_decoder_bytes / (4 * sizeof(float)))
        throw Error("limit", "HDR output exceeds its pixel/allocation budget");
    std::uint64_t total = 0, offset = 0;
    for (std::uint32_t level = 0; level < texture.mips; ++level) {
        auto pixels =
            std::uint64_t(std::max(1u, texture.width >> level)) * std::max(1u, texture.height >> level);
        if (pixels > options.max_payload_bytes / pixel_bytes ||
            pixels * pixel_bytes > options.max_payload_bytes - total)
            throw Error("limit", "HDR mip chain exceeds payload budget");
        auto size = pixels * pixel_bytes;
        if (level < mip)
            offset += size;
        total += size;
    }
    auto source = decode_bulk_payload(texture.payload, options);
    if (source.size() != total)
        throw Error("texture-layout", "HDR source does not match its mip chain");
    result.pixels.resize(static_cast<std::size_t>(count * 4));
    bool rgbe = texture.format == "TSF_BGRE8" || texture.format == "TSF_RGBE8_DEPRECATED";
    bool single = texture.format == "TSF_R16F" || texture.format == "TSF_R32F";
    bool sixteen = texture.format == "TSF_RGBA16F" || texture.format == "TSF_R16F";
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 65536 == 0)
            options.control.checkpoint("Decoding HDR source", i, count);
        auto input = source.data() + offset + i * pixel_bytes;
        auto output = result.pixels.data() + i * 4;
        if (rgbe) {
            auto scale = input[3] ? std::ldexp(1.f, int(input[3]) - 136) : 0.f;
            for (unsigned c = 0; c < 3; ++c)
                output[c] = (float(input[texture.format == "TSF_BGRE8" ? 2 - c : c]) + 0.5f) * scale;
            output[3] = 1;
        } else if (single) {
            output[0] = output[1] = output[2] = floating(input, sixteen);
            output[3] = 1;
        } else
            for (unsigned c = 0; c < 4; ++c)
                output[c] = floating(input + c * (sixteen ? 2 : 4), sixteen);
    }
    return result;
}
ImageRgba8 tone_map_hdr(const ImageRgba32f &image, double exposure_ev, const OperationControl &control) {
    auto count = samples(image);
    if (!std::isfinite(exposure_ev) || exposure_ev < -32 || exposure_ev > 32)
        throw Error("hdr-exposure", "HDR exposure must be between -32 and 32 EV");
    ImageRgba8 result{image.width, image.height, {}};
    result.pixels.resize(image.pixels.size());
    auto exposure = std::exp2(exposure_ev);
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 65536 == 0)
            control.checkpoint("Tone mapping HDR preview", i, count);
        for (unsigned c = 0; c < 4; ++c) {
            double v = image.pixels[i * 4 + c];
            if (!std::isfinite(v))
                throw Error("hdr-sample", "Non-finite HDR sample");
            if (c < 3) {
                v = std::max(0., v) * exposure;
                v = v / (1. + v);
                v = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1. / 2.4) - 0.055;
            }
            result.pixels[i * 4 + c] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0., 1.) * 255));
        }
    }
    return result;
}
HdrImageComparison compare_hdr_images(const ImageRgba32f &before, const ImageRgba32f &after, double threshold,
                                      bool heatmap, const OperationControl &control) {
    auto count = samples(before);
    samples(after);
    if (!std::isfinite(threshold) || threshold < 0)
        throw Error("hdr-threshold", "HDR threshold must be finite and non-negative");
    HdrImageComparison result;
    result.before_width = before.width;
    result.before_height = before.height;
    result.after_width = after.width;
    result.after_height = after.height;
    result.same_size = result.comparable = before.width == after.width && before.height == after.height;
    result.threshold = threshold;
    if (!result.comparable) {
        if (heatmap)
            throw Error("image-size", "HDR mask requires matching dimensions");
        return result;
    }
    if (heatmap)
        result.heatmap = {before.width, before.height, std::vector<std::uint8_t>(before.pixels.size(), 0)};
    double sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 65536 == 0)
            control.checkpoint("Comparing HDR samples", i, count);
        double maximum = 0;
        for (unsigned c = 0; c < 4; ++c) {
            auto a = double(before.pixels[i * 4 + c]), b = double(after.pixels[i * 4 + c]);
            if (!std::isfinite(a) || !std::isfinite(b))
                throw Error("hdr-sample", "Non-finite HDR sample");
            auto delta = std::abs(a - b);
            sum += delta;
            maximum = std::max(maximum, delta);
        }
        result.max_sample_delta = std::max(result.max_sample_delta, maximum);
        if (maximum > threshold) {
            ++result.changed_pixels;
            detail::include_changed_pixel(result.changed_bounds, static_cast<std::uint32_t>(i % before.width),
                                          static_cast<std::uint32_t>(i / before.width));
            if (heatmap) {
                result.heatmap.pixels[i * 4] = 255;
                result.heatmap.pixels[i * 4 + 3] = 255;
            }
        }
    }
    result.mean_absolute_error = sum / (double(count) * 4.);
    return result;
}
Json HdrImageComparison::to_json() const {
    return {{"schema", "cyuediff.hdr-diff/1"},
            {"status", comparable && !changed_pixels ? "identical" : "changed"},
            {"comparison", "floating-point source samples before tone mapping; no color-space conversion"},
            {"sameSize", same_size},
            {"pixelsComparable", comparable},
            {"before", {{"width", before_width}, {"height", before_height}}},
            {"after", {{"width", after_width}, {"height", after_height}}},
            {"threshold", threshold},
            {"changedPixels", comparable ? Json(changed_pixels) : Json(nullptr)},
            {"changedBounds", comparable && changed_pixels ? changed_bounds.to_json() : Json(nullptr)},
            {"maxSampleDelta", comparable ? Json(max_sample_delta) : Json(nullptr)},
            {"meanAbsoluteError", comparable ? Json(mean_absolute_error) : Json(nullptr)}};
}
} // namespace cyuediff
