#include "window_placement.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cyuediff::desktop {
namespace {
void require(bool good) {
    if (!good)
        throw Error("desktop-window-placement", "Invalid window placement or work area");
}
int integer(const Json &j, const char *key, int low, int high) {
    require(j.contains(key) && j.at(key).is_number_integer() && j.at(key) >= low && j.at(key) <= high);
    return j.at(key).get<int>();
}
void validate(const WindowPlacement &p) {
    require(p.x >= -1000000 && p.x <= 1000000 && p.y >= -1000000 && p.y <= 1000000 && p.width >= 1 &&
            p.width <= 65536 && p.height >= 1 && p.height <= 65536 && p.dpi >= 48 && p.dpi <= 768);
}
} // namespace
Json WindowPlacement::to_json() const {
    validate(*this);
    return {{"x", x}, {"y", y}, {"width", width}, {"height", height}, {"dpi", dpi}, {"maximized", maximized}};
}
WindowPlacement WindowPlacement::from_json(const Json &j) {
    require(j.is_object() && j.size() == 6 && j.contains("maximized") && j.at("maximized").is_boolean());
    return {integer(j, "x", -1000000, 1000000), integer(j, "y", -1000000, 1000000),
            integer(j, "width", 1, 65536),      integer(j, "height", 1, 65536),
            integer(j, "dpi", 48, 768),         j.at("maximized").get<bool>()};
}
WindowPlacement fit_window_placement(const WindowPlacement &p, const std::vector<WindowWorkArea> &areas) {
    validate(p);
    require(!areas.empty() && areas.size() <= 256);
    const WindowWorkArea *selected = nullptr;
    double best_overlap = -1, best_distance = std::numeric_limits<double>::infinity();
    for (const auto &a : areas) {
        require(a.x >= -1000000 && a.x <= 1000000 && a.y >= -1000000 && a.y <= 1000000 && a.width >= 1 &&
                a.width <= 65536 && a.height >= 1 && a.height <= 65536 && a.dpi >= 48 && a.dpi <= 768);
        const auto overlap_x = std::max(0, std::min(p.x + p.width, a.x + a.width) - std::max(p.x, a.x));
        const auto overlap_y = std::max(0, std::min(p.y + p.height, a.y + a.height) - std::max(p.y, a.y));
        const double overlap = double(overlap_x) * overlap_y;
        const double dx = std::max({0, a.x - (p.x + p.width), p.x - (a.x + a.width)});
        const double dy = std::max({0, a.y - (p.y + p.height), p.y - (a.y + a.height)});
        const double distance = dx * dx + dy * dy;
        if (overlap > best_overlap || (overlap == best_overlap && distance < best_distance)) {
            selected = &a;
            best_overlap = overlap;
            best_distance = distance;
        }
    }
    const auto &a = *selected;
    auto size = [&](int old, int minimum, int available) {
        const auto scaled = int(std::lround(double(old) * a.dpi / p.dpi));
        const auto floor = std::min(available, int(std::lround(double(minimum) * a.dpi / 96)));
        return std::clamp(scaled, floor, available);
    };
    WindowPlacement result = p;
    result.width = size(p.width, 960, a.width);
    result.height = size(p.height, 640, a.height);
    result.dpi = a.dpi;
    result.x = std::clamp(p.x, a.x, a.x + a.width - result.width);
    result.y = std::clamp(p.y, a.y, a.y + a.height - result.height);
    validate(result);
    return result;
}
} // namespace cyuediff::desktop
