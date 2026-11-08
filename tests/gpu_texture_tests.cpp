#include <cyuediff/gpu_texture.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void refused(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, "Expected GPU refusal");
        return;
    }
    throw std::runtime_error("Malformed GPU payload accepted");
}
using Bytes = std::vector<std::uint8_t>;
Bytes color() {
    return {0x00, 0xf8, 0xe0, 0x07, 0xe4, 0xe4, 0xe4, 0xe4};
}
Bytes bc7() {
    Bytes b(16);
    unsigned bit = 0;
    auto put = [&](unsigned value, unsigned count) {
        for (unsigned n = 0; n < count; ++n, ++bit)
            b[bit / 8] |= ((value >> n) & 1) << (bit % 8);
    };
    put(64, 7);
    for (auto v : {127, 127, 0, 0, 0, 0, 127, 127})
        put(v, 7);
    put(0, 2);
    put(0, 3);
    for (unsigned i = 1; i < 16; ++i)
        put(0, 4);
    check(bit == 128, "Independent BC7 mode 6 has exactly 128 bits");
    return b;
}
void pixel(const ImageRgba8 &image, unsigned at, std::array<unsigned, 4> expected) {
    for (unsigned c = 0; c < 4; ++c)
        check(image.pixels.at(at * 4 + c) == expected[c],
              "Decoded channel matches independently specified pixel");
}
} // namespace
int main() {
    try {
        for (unsigned i = 0; i <= static_cast<unsigned>(GpuPixelFormat::bc7_unorm); ++i) {
            auto format = static_cast<GpuPixelFormat>(i);
            check(unreal_gpu_pixel_format(gpu_pixel_format_name(format)) == format,
                  "Pixel format identity round trip");
            auto size = gpu_texture_bytes(format, 3, 5);
            Bytes bytes(static_cast<std::size_t>(size));
            auto image = decode_gpu_rgba32f(bytes, 3, 5, format);
            check(image.width == 3 && image.height == 5 && image.pixels.size() == 60,
                  "All formats support exact partial dimensions");
            for (auto sample : image.pixels)
                check(std::isfinite(sample), "Output samples stay finite");
            for (std::size_t n = 0; n < bytes.size(); ++n) {
                Bytes cut(bytes.begin(), bytes.begin() + n);
                refused([&] { decode_gpu_rgba32f(cut, 3, 5, format); }, "gpu-layout");
            }
            bytes.push_back(0);
            refused([&] { decode_gpu_rgba32f(bytes, 3, 5, format); }, "gpu-layout");
            bytes.pop_back();
            GpuDecodeOptions limits;
            limits.max_pixels = 14;
            refused([&] { decode_gpu_rgba32f(bytes, 3, 5, format, limits); }, "gpu-limit");
            limits = {};
            limits.max_output_bytes = 239;
            refused([&] { decode_gpu_rgba32f(bytes, 3, 5, format, limits); }, "gpu-limit");
            limits = {};
            limits.max_input_bytes = size - 1;
            refused([&] { decode_gpu_rgba32f(bytes, 3, 5, format, limits); }, "gpu-limit");
            limits = {};
            limits.control.cancelled = [] { return true; };
            refused([&] { decode_gpu_rgba32f(bytes, 3, 5, format, limits); }, "cancelled");
        }
        auto bc1 = decode_gpu_rgba8(color(), 4, 4, GpuPixelFormat::bc1_unorm);
        pixel(bc1, 0, {255, 0, 0, 255});
        pixel(bc1, 1, {0, 255, 0, 255});
        pixel(bc1, 2, {170, 85, 0, 255});
        pixel(bc1, 3, {85, 170, 0, 255});
        auto transparent = color();
        transparent[0] = transparent[1] = 0;
        transparent[2] = transparent[3] = 255;
        pixel(decode_gpu_rgba8(transparent, 4, 4, GpuPixelFormat::bc1_unorm), 3, {0, 0, 0, 0});
        Bytes bc2(8);
        for (unsigned i = 0; i < 8; ++i)
            bc2[i] = (i * 2) | ((i * 2 + 1) << 4);
        auto c = color();
        c[4] = c[5] = c[6] = c[7] = 0;
        bc2.insert(bc2.end(), c.begin(), c.end());
        auto sharp = decode_gpu_rgba8(bc2, 4, 4, GpuPixelFormat::bc2_unorm);
        for (unsigned i = 0; i < 16; ++i)
            pixel(sharp, i, {255, 0, 0, i * 17});
        Bytes bc3(8);
        bc3[0] = 240;
        bc3[1] = 16;
        bc3.insert(bc3.end(), c.begin(), c.end());
        pixel(decode_gpu_rgba8(bc3, 4, 4, GpuPixelFormat::bc3_unorm), 0, {255, 0, 0, 240});
        Bytes bc4(8);
        bc4[0] = 17;
        pixel(decode_gpu_rgba8(bc4, 4, 4, GpuPixelFormat::bc4_unorm), 0, {17, 17, 17, 255});
        Bytes bc5(16);
        bc5[0] = 32;
        bc5[8] = 64;
        pixel(decode_gpu_rgba8(bc5, 4, 4, GpuPixelFormat::bc5_unorm), 0, {32, 64, 0, 255});
        auto mode6 = bc7();
        auto b7 = decode_gpu_rgba8(mode6, 4, 4, GpuPixelFormat::bc7_unorm);
        for (unsigned i = 0; i < 16; ++i)
            pixel(b7, i, {254, 0, 0, 254});
        Bytes edge = c;
        edge.insert(edge.end(), mode6.size() / 2, 0); // Two BC1 blocks: red top, black bottom.
        auto cropped = decode_gpu_rgba8(edge, 3, 5, GpuPixelFormat::bc1_unorm);
        pixel(cropped, 11, {255, 0, 0, 255});
        pixel(cropped, 12, {0, 0, 0, 255});
        pixel(decode_gpu_rgba8({4, 9}, 2, 1, GpuPixelFormat::r8_unorm), 1, {9, 9, 9, 255});
        pixel(decode_gpu_rgba8({0, 0, 255, 255}, 2, 1, GpuPixelFormat::r16_unorm), 1, {255, 255, 255, 255});
        pixel(decode_gpu_rgba8({3, 5}, 1, 1, GpuPixelFormat::rg8_unorm), 0, {3, 5, 0, 255});
        pixel(decode_gpu_rgba8({3, 5, 7, 9}, 1, 1, GpuPixelFormat::rgba8_unorm), 0, {3, 5, 7, 9});
        pixel(decode_gpu_rgba8({3, 5, 7, 9}, 1, 1, GpuPixelFormat::bgra8_unorm), 0, {7, 5, 3, 9});
        pixel(decode_gpu_rgba8({0, 0, 255, 255, 128, 128, 255, 255}, 1, 1, GpuPixelFormat::rgba16_unorm), 0,
              {0, 255, 128, 255});
        auto h = decode_gpu_rgba32f({0, 0xbc, 1, 0, 0, 0x80, 0, 0x40}, 1, 1, GpuPixelFormat::rgba16_float);
        check(h.pixels[0] == -1 && h.pixels[1] == std::ldexp(1.f, -24) && std::signbit(h.pixels[2]) &&
                  h.pixels[3] == 2,
              "Half samples preserve negative, subnormal, signed zero and HDR values");
        for (auto invalid : {0x7c00, 0x7e00, 0xfc00}) {
            Bytes b(8);
            b[0] = invalid & 255;
            b[1] = invalid >> 8;
            refused([&] { decode_gpu_rgba32f(b, 1, 1, GpuPixelFormat::rgba16_float); }, "gpu-value");
        }
        for (auto invalid : {0x7f800000u, 0x7fc00000u, 0xff800000u}) {
            Bytes b(16);
            for (unsigned i = 0; i < 4; ++i)
                b[i] = (invalid >> (i * 8)) & 255;
            refused([&] { decode_gpu_rgba32f(b, 1, 1, GpuPixelFormat::rgba32_float); }, "gpu-value");
        }
        refused([] { decode_gpu_rgba8(Bytes(8), 1, 1, GpuPixelFormat::rgba16_float); }, "gpu-format");
        refused([] { unreal_gpu_pixel_format("PF_ASTC_4x4"); }, "gpu-format");
        refused([] { gpu_texture_bytes(GpuPixelFormat::rgba32_float, 0, 2); }, "gpu-layout");
        refused([] { gpu_texture_bytes(GpuPixelFormat::rgba32_float, UINT32_MAX, UINT32_MAX); }, "gpu-limit");
        GpuDecodeOptions stop;
        unsigned calls = 0;
        stop.control.cancelled = [&] { return ++calls == 4; };
        refused([&] { decode_gpu_rgba8(Bytes(8192), 4096, 1, GpuPixelFormat::bc1_unorm, stop); },
                "cancelled");
        stop = {};
        calls = 0;
        stop.control.cancelled = [&] { return ++calls == 4; };
        refused([&] { decode_gpu_rgba32f(Bytes(4096), 4096, 1, GpuPixelFormat::r8_unorm, stop); },
                "cancelled");
        std::cout << checks << " GPU pixel checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
