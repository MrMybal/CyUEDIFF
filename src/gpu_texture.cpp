#include <cyuediff/gpu_texture.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#define BCDEC_BC4BC5_PRECISE
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <bcdec/bcdec.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
namespace cyuediff {
namespace {
struct FormatInfo {
    GpuPixelFormat format;
    const char *name;
    unsigned bytes;
    bool blocks;
    bool hdr = false;
};
constexpr FormatInfo formats[] = {{GpuPixelFormat::r8_unorm, "PF_G8", 1, false},
                                  {GpuPixelFormat::r16_unorm, "PF_G16", 2, false},
                                  {GpuPixelFormat::rg8_unorm, "PF_R8G8", 2, false},
                                  {GpuPixelFormat::rgba8_unorm, "PF_R8G8B8A8", 4, false},
                                  {GpuPixelFormat::bgra8_unorm, "PF_B8G8R8A8", 4, false},
                                  {GpuPixelFormat::rgba16_unorm, "PF_A16B16G16R16", 8, false},
                                  {GpuPixelFormat::rgba16_float, "PF_FloatRGBA", 8, false, true},
                                  {GpuPixelFormat::rgba32_float, "PF_A32B32G32R32F", 16, false, true},
                                  {GpuPixelFormat::bc1_unorm, "PF_DXT1", 8, true},
                                  {GpuPixelFormat::bc2_unorm, "PF_DXT3", 16, true},
                                  {GpuPixelFormat::bc3_unorm, "PF_DXT5", 16, true},
                                  {GpuPixelFormat::bc4_unorm, "PF_BC4", 8, true},
                                  {GpuPixelFormat::bc5_unorm, "PF_BC5", 16, true},
                                  {GpuPixelFormat::bc6h_ufloat, "PF_BC6H", 16, true, true},
                                  {GpuPixelFormat::bc7_unorm, "PF_BC7", 16, true}};
const FormatInfo &info(GpuPixelFormat format) {
    for (const auto &entry : formats)
        if (entry.format == format)
            return entry;
    throw Error("gpu-format", "Unsupported GPU pixel format");
}
std::uint16_t u16(const std::uint8_t *p) {
    return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}
float f32(const std::uint8_t *p) {
    const auto bits = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
                      (std::uint32_t(p[3]) << 24);
    float result;
    std::memcpy(&result, &bits, 4);
    return result;
}
float half(std::uint16_t value) {
    const unsigned exponent = (value >> 10) & 31, fraction = value & 1023;
    if (exponent == 31)
        throw Error("gpu-value", "GPU texture contains a non-finite half sample");
    float result =
        exponent ? std::ldexp(float(fraction + 1024), int(exponent) - 25) : std::ldexp(float(fraction), -24);
    return value & 0x8000 ? -result : result;
}
void validate(const std::vector<std::uint8_t> &input, std::uint32_t width, std::uint32_t height,
              GpuPixelFormat format, unsigned output_bpp, const GpuDecodeOptions &options) {
    options.control.check();
    const auto pixels = std::uint64_t(width) * height;
    if (!width || !height || pixels > options.max_pixels || pixels > SIZE_MAX / output_bpp ||
        pixels > options.max_output_bytes / output_bpp || input.size() > options.max_input_bytes)
        throw Error("gpu-limit", "GPU texture dimensions or buffers exceed configured limits");
    if (input.size() != gpu_texture_bytes(format, width, height))
        throw Error("gpu-layout", "GPU texture payload does not match its exact encoded footprint");
}
std::uint8_t unorm8(std::uint16_t value) {
    return static_cast<std::uint8_t>((std::uint32_t(value) + 128) / 257);
}
void block8(const std::uint8_t *input, GpuPixelFormat format, std::array<std::uint8_t, 64> &rgba) {
    if (format == GpuPixelFormat::bc1_unorm)
        bcdec_bc1(input, rgba.data(), 16);
    else if (format == GpuPixelFormat::bc2_unorm)
        bcdec_bc2(input, rgba.data(), 16);
    else if (format == GpuPixelFormat::bc3_unorm)
        bcdec_bc3(input, rgba.data(), 16);
    else if (format == GpuPixelFormat::bc7_unorm)
        bcdec_bc7(input, rgba.data(), 16);
    else {
        alignas(16) std::array<std::uint8_t, 32> components{};
        const bool single = format == GpuPixelFormat::bc4_unorm;
        if (single)
            bcdec_bc4(input, components.data(), 4, 0);
        else
            bcdec_bc5(input, components.data(), 8, 0);
        for (unsigned pixel = 0; pixel < 16; ++pixel) {
            const auto red = components[pixel * (single ? 1 : 2)];
            rgba[pixel * 4] = red;
            rgba[pixel * 4 + 1] = single ? red : components[pixel * 2 + 1];
            rgba[pixel * 4 + 2] = single ? red : 0;
            rgba[pixel * 4 + 3] = 255;
        }
    }
}
} // namespace
const char *gpu_pixel_format_name(GpuPixelFormat format) {
    return info(format).name;
}
GpuPixelFormat unreal_gpu_pixel_format(const std::string &name) {
    for (const auto &entry : formats)
        if (name == entry.name)
            return entry.format;
    throw Error("gpu-format", "Unsupported Unreal GPU pixel format: " + name);
}
bool gpu_pixel_format_is_hdr(GpuPixelFormat format) {
    return info(format).hdr;
}
std::uint64_t gpu_texture_bytes(GpuPixelFormat format, std::uint32_t width, std::uint32_t height) {
    const auto &layout = info(format);
    if (!width || !height)
        throw Error("gpu-layout", "GPU texture dimensions must be positive");
    const auto w = layout.blocks ? (std::uint64_t(width) + 3) / 4 : width;
    const auto h = layout.blocks ? (std::uint64_t(height) + 3) / 4 : height;
    if (w > std::numeric_limits<std::uint64_t>::max() / h / layout.bytes)
        throw Error("gpu-limit", "GPU texture footprint overflows its size representation");
    return w * h * layout.bytes;
}
ImageRgba8 decode_gpu_rgba8(const std::vector<std::uint8_t> &input, std::uint32_t width, std::uint32_t height,
                            GpuPixelFormat format, const GpuDecodeOptions &options) {
    const auto &layout = info(format);
    if (layout.hdr)
        throw Error("gpu-format", "Decode floating GPU samples before selecting a display tone map");
    validate(input, width, height, format, 4, options);
    ImageRgba8 out{width, height, std::vector<std::uint8_t>(std::size_t(width) * height * 4)};
    if (layout.blocks) {
        const auto blocks_x = (std::uint64_t(width) + 3) / 4, blocks_y = (std::uint64_t(height) + 3) / 4;
        alignas(16) std::array<std::uint8_t, 64> decoded{};
        for (std::uint64_t by = 0; by < blocks_y; ++by) {
            options.control.checkpoint("gpu-block-decode", by, blocks_y);
            for (std::uint64_t bx = 0; bx < blocks_x; ++bx) {
                if ((bx & 63) == 0)
                    options.control.check();
                block8(input.data() + std::size_t((by * blocks_x + bx) * layout.bytes), format, decoded);
                for (unsigned y = 0; y < 4 && by * 4 + y < height; ++y)
                    for (unsigned x = 0; x < 4 && bx * 4 + x < width; ++x)
                        std::copy_n(decoded.data() + (y * 4 + x) * 4, 4,
                                    out.pixels.data() + std::size_t(((by * 4 + y) * width + bx * 4 + x) * 4));
            }
        }
    } else {
        for (std::uint32_t y = 0; y < height; ++y) {
            options.control.checkpoint("gpu-pixel-decode", y, height);
            for (std::uint32_t x = 0; x < width; ++x) {
                if ((x & 255) == 0)
                    options.control.check();
                const auto pixel = std::size_t(y) * width + x;
                const auto *p = input.data() + pixel * layout.bytes;
                auto *dst = out.pixels.data() + pixel * 4;
                if (format == GpuPixelFormat::r8_unorm || format == GpuPixelFormat::r16_unorm) {
                    dst[0] = dst[1] = dst[2] = format == GpuPixelFormat::r8_unorm ? p[0] : unorm8(u16(p));
                    dst[3] = 255;
                } else if (format == GpuPixelFormat::rg8_unorm) {
                    dst[0] = p[0];
                    dst[1] = p[1];
                    dst[2] = 0;
                    dst[3] = 255;
                } else if (format == GpuPixelFormat::rgba16_unorm)
                    for (unsigned c = 0; c < 4; ++c)
                        dst[c] = unorm8(u16(p + c * 2));
                else {
                    dst[0] = p[format == GpuPixelFormat::bgra8_unorm ? 2 : 0];
                    dst[1] = p[1];
                    dst[2] = p[format == GpuPixelFormat::bgra8_unorm ? 0 : 2];
                    dst[3] = p[3];
                }
            }
        }
    }
    options.control.check();
    return out;
}
ImageRgba32f decode_gpu_rgba32f(const std::vector<std::uint8_t> &input, std::uint32_t width,
                                std::uint32_t height, GpuPixelFormat format,
                                const GpuDecodeOptions &options) {
    const auto &layout = info(format);
    validate(input, width, height, format, 16, options);
    ImageRgba32f out{width, height, std::vector<float>(std::size_t(width) * height * 4)};
    if (layout.blocks) {
        const auto blocks_x = (std::uint64_t(width) + 3) / 4, blocks_y = (std::uint64_t(height) + 3) / 4;
        alignas(16) std::array<float, 48> hdr{};
        alignas(16) std::array<std::uint8_t, 64> ldr{};
        for (std::uint64_t by = 0; by < blocks_y; ++by) {
            options.control.checkpoint("gpu-block-decode", by, blocks_y);
            for (std::uint64_t bx = 0; bx < blocks_x; ++bx) {
                if ((bx & 63) == 0)
                    options.control.check();
                const auto *block = input.data() + std::size_t((by * blocks_x + bx) * layout.bytes);
                if (format == GpuPixelFormat::bc6h_ufloat)
                    bcdec_bc6h_float(block, hdr.data(), 12, 0);
                else
                    block8(block, format, ldr);
                for (unsigned y = 0; y < 4 && by * 4 + y < height; ++y)
                    for (unsigned x = 0; x < 4 && bx * 4 + x < width; ++x) {
                        auto *dst = out.pixels.data() + std::size_t(((by * 4 + y) * width + bx * 4 + x) * 4);
                        for (unsigned c = 0; c < 4; ++c) {
                            const auto value = format == GpuPixelFormat::bc6h_ufloat
                                                   ? (c == 3 ? 1.f : hdr[(y * 4 + x) * 3 + c])
                                                   : float(ldr[(y * 4 + x) * 4 + c]) / 255.f;
                            if (!std::isfinite(value))
                                throw Error("gpu-value", "GPU texture contains a non-finite sample");
                            dst[c] = value;
                        }
                    }
            }
        }
    } else {
        for (std::uint32_t y = 0; y < height; ++y) {
            options.control.checkpoint("gpu-pixel-decode", y, height);
            for (std::uint32_t x = 0; x < width; ++x) {
                if ((x & 255) == 0)
                    options.control.check();
                const auto pixel = std::size_t(y) * width + x;
                const auto *p = input.data() + pixel * layout.bytes;
                auto *dst = out.pixels.data() + pixel * 4;
                if (format == GpuPixelFormat::rgba16_float || format == GpuPixelFormat::rgba32_float) {
                    for (unsigned c = 0; c < 4; ++c) {
                        dst[c] =
                            format == GpuPixelFormat::rgba16_float ? half(u16(p + c * 2)) : f32(p + c * 4);
                        if (!std::isfinite(dst[c]))
                            throw Error("gpu-value", "GPU texture contains a non-finite sample");
                    }
                } else if (format == GpuPixelFormat::r16_unorm || format == GpuPixelFormat::rgba16_unorm) {
                    for (unsigned c = 0; c < 4; ++c)
                        dst[c] = format == GpuPixelFormat::r16_unorm
                                     ? (c == 3 ? 1.f : float(u16(p)) / 65535.f)
                                     : float(u16(p + c * 2)) / 65535.f;
                } else if (format == GpuPixelFormat::r8_unorm) {
                    dst[0] = dst[1] = dst[2] = float(p[0]) / 255.f;
                    dst[3] = 1;
                } else if (format == GpuPixelFormat::rg8_unorm) {
                    dst[0] = float(p[0]) / 255.f;
                    dst[1] = float(p[1]) / 255.f;
                    dst[2] = 0;
                    dst[3] = 1;
                } else
                    for (unsigned c = 0; c < 4; ++c)
                        dst[c] =
                            float(p[format == GpuPixelFormat::bgra8_unorm && c != 1 && c != 3 ? 2 - c : c]) /
                            255.f;
            }
        }
    }
    options.control.check();
    return out;
}
} // namespace cyuediff
