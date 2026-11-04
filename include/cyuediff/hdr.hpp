#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct ImageRgba32f {
    std::uint32_t width = 0, height = 0;
    std::vector<float> pixels;
};
struct HdrImageComparison {
    bool comparable = false, same_size = false;
    std::uint32_t before_width = 0, before_height = 0, after_width = 0, after_height = 0;
    std::uint64_t changed_pixels = 0;
    double max_sample_delta = 0, mean_absolute_error = 0, threshold = 0;
    ImageRgba8 heatmap;
    Json to_json() const;
};
bool is_hdr_format(const std::string &);
// Raw source samples: no exposure, normalization, gamma or tone mapping.
ImageRgba32f decode_texture_hdr(const TextureSource &, std::uint32_t mip = 0,
                                const TextureDecodeOptions &options = {});
// Display only: exposure, positive Reinhard tone mapping and linear-to-sRGB RGB.
// Alpha is clamped separately. The original floating-point samples remain unchanged.
ImageRgba8 tone_map_hdr(const ImageRgba32f &, double exposure_ev = 0, const OperationControl &control = {});
HdrImageComparison compare_hdr_images(const ImageRgba32f &, const ImageRgba32f &, double threshold = 0,
                                      bool heatmap = false, const OperationControl &control = {});
} // namespace cyuediff
