#include <cyuediff/hdr.hpp>
#include <iostream>
using namespace cyuediff;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        ImageRgba8 before{5, 3, std::vector<std::uint8_t>(5 * 3 * 4, 0)};
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 5; ++x)
                for (bool mask : {false, true}) {
                    auto after = before;
                    after.pixels[(y * 5 + x) * 4 + 3] = 5;
                    auto diff = compare_images(before, after, 4, mask);
                    Json expected = {{"x", x}, {"y", y}, {"width", 1}, {"height", 1}};
                    check(diff.changed_pixels == 1 && diff.changed_bounds.to_json() == expected,
                          "Single alpha change bounds independent of heatmap");
                    check(diff.to_json()["changedBounds"] == expected, "CLI bounds contract");
                    check(compare_images(before, after, 5, mask).to_json()["changedBounds"].is_null(),
                          "Threshold equality does not expand bounds");
                }
        auto after = before;
        after.pixels[(1 * 5 + 1) * 4] = 10;
        after.pixels[(2 * 5 + 3) * 4] = 20;
        check(compare_images(before, after).changed_bounds.to_json() ==
                  Json{{"x", 1}, {"y", 1}, {"width", 3}, {"height", 2}},
              "Disjoint changes produce half-open union bounds");
        check(compare_images(before, before).to_json()["changedBounds"].is_null(), "Identical bounds absent");
        ImageRgba8 mismatch{1, 1, {0, 0, 0, 0}};
        check(compare_images(before, mismatch).to_json()["changedBounds"].is_null(),
              "Incomparable bounds absent");
        ImageRgba32f hdr_before{5, 3, std::vector<float>(5 * 3 * 4, 1)};
        auto hdr_after = hdr_before;
        hdr_after.pixels[(2 * 5 + 4) * 4] += .25f;
        for (bool mask : {false, true}) {
            auto diff = compare_hdr_images(hdr_before, hdr_after, .2, mask);
            check(diff.changed_pixels == 1 &&
                      diff.changed_bounds.to_json() == Json{{"x", 4}, {"y", 2}, {"width", 1}, {"height", 1}},
                  "HDR source floats determine changed bounds");
            check(compare_hdr_images(hdr_before, hdr_after, .25, mask).to_json()["changedBounds"].is_null(),
                  "HDR threshold equality leaves no bounds");
        }
        auto single_a = ImageRgba8{1, 1, {0, 0, 0, 0}}, single_b = ImageRgba8{1, 1, {1, 0, 0, 0}};
        check(compare_images(single_a, single_b).changed_bounds.to_json() ==
                  Json{{"x", 0}, {"y", 0}, {"width", 1}, {"height", 1}},
              "One-pixel image bounds");
        OperationControl stopped;
        bool cancel = false;
        stopped.cancelled = [&] { return cancel; };
        stopped.progress = [&](const auto &p) {
            if (p.phase == "Comparing pixels")
                cancel = true;
        };
        try {
            compare_images(before, before, 0, true, stopped);
            throw std::runtime_error("Pixel comparison cancellation ignored");
        } catch (const Error &e) {
            check(e.code == "cancelled", "Pixel comparison cancellation propagates");
        }
        std::cout << checks << " image bounds checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
