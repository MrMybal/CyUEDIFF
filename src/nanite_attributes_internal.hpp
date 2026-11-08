#pragma once
#include <cyuediff/nanite_geometry.hpp>
namespace cyuediff::detail {
struct NaniteUvRange {
    std::array<std::int32_t, 2> minimum{};
    std::array<std::uint32_t, 2> gap_start{}, gap_length{};
    int precision = 0;
};
struct NaniteAttributeLayout {
    unsigned normal = 0, tangent = 0, uv_count = 0, color_mode = 0;
    bool has_tangent = false;
    std::uint32_t color_min = 0;
    std::array<unsigned, 4> color_bits{};
    std::array<std::array<unsigned, 2>, 4> uv_bits{};
    std::array<NaniteUvRange, 4> ranges{};
};
struct NaniteAttributeCode {
    std::uint32_t normal = 0, tangent = 0, color = 0;
    std::array<std::array<std::uint32_t, 2>, 4> uv{};
};
NaniteAttributeLayout nanite_attribute_layout(const std::array<std::uint32_t, 28> &,
                                              const std::array<std::array<std::uint32_t, 8>, 4> &, unsigned);
NaniteAttributeCode transcode_nanite_attribute(const NaniteAttributeCode &, const NaniteAttributeLayout &,
                                               const NaniteAttributeLayout &);
NaniteVertexAttributes decode_nanite_attribute(const NaniteAttributeCode &, const NaniteAttributeLayout &);
} // namespace cyuediff::detail
