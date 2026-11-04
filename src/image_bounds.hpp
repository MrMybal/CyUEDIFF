#pragma once
#include <cyuediff/core.hpp>
#include <algorithm>
namespace cyuediff::detail {
inline void include_changed_pixel(ImageChangeBounds &b, std::uint32_t x, std::uint32_t y) {
    if (!b.width || !b.height) {
        b = {x, y, 1, 1};
        return;
    }
    const auto right = std::max(b.x + b.width, x + 1), bottom = std::max(b.y + b.height, y + 1);
    b.x = std::min(b.x, x);
    b.y = std::min(b.y, y);
    b.width = right - b.x;
    b.height = bottom - b.y;
}
} // namespace cyuediff::detail
