#pragma once
#include <cyuediff/core.hpp>

namespace cyuediff::desktop {
// Outer-window screen pixels, including negative coordinates on secondary monitors.
struct WindowPlacement {
    int x = 0, y = 0, width = 1360, height = 900, dpi = 96;
    bool maximized = false;
    Json to_json() const;
    static WindowPlacement from_json(const Json &);
};
struct WindowWorkArea {
    int x = 0, y = 0, width = 1920, height = 1080, dpi = 96;
};
// Scale the stored size to the selected monitor and keep the whole normal rectangle in its work area.
// A disconnected monitor falls back to the nearest available work area. Small screens remain usable.
WindowPlacement fit_window_placement(const WindowPlacement &, const std::vector<WindowWorkArea> &);
} // namespace cyuediff::desktop
