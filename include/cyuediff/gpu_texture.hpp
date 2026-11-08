#pragma once
#include <cyuediff/hdr.hpp>
namespace cyuediff {
enum class GpuPixelFormat {
    r8_unorm,
    r16_unorm,
    rg8_unorm,
    rgba8_unorm,
    bgra8_unorm,
    rgba16_unorm,
    rgba16_float,
    rgba32_float,
    bc1_unorm,
    bc2_unorm,
    bc3_unorm,
    bc4_unorm,
    bc5_unorm,
    bc6h_ufloat,
    bc7_unorm
};
struct GpuDecodeOptions {
    std::uint64_t max_pixels = 64ull * 1024 * 1024;
    std::uint64_t max_input_bytes = 256ull * 1024 * 1024;
    std::uint64_t max_output_bytes = 512ull * 1024 * 1024;
    OperationControl control;
};
const char *gpu_pixel_format_name(GpuPixelFormat);
GpuPixelFormat unreal_gpu_pixel_format(const std::string &);
bool gpu_pixel_format_is_hdr(GpuPixelFormat);
// Exact encoded 2D footprint, including partial 4x4 edge blocks.
std::uint64_t gpu_texture_bytes(GpuPixelFormat, std::uint32_t width, std::uint32_t height);
// Stored channels, without gamma, exposure, premultiplication or normal-Z reconstruction.
// R-only formats are displayed as gray; RG formats use zero blue and opaque alpha.
ImageRgba8 decode_gpu_rgba8(const std::vector<std::uint8_t> &, std::uint32_t width, std::uint32_t height,
                            GpuPixelFormat, const GpuDecodeOptions & = {});
// Floating formats retain finite samples; UNORM channels are normalized to [0,1].
ImageRgba32f decode_gpu_rgba32f(const std::vector<std::uint8_t> &, std::uint32_t width, std::uint32_t height,
                                GpuPixelFormat, const GpuDecodeOptions & = {});
} // namespace cyuediff
