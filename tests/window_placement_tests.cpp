#include "../apps/desktop/window_placement.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
int main() {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Window placement check failed");
    };
    auto rejects = [&](auto action) {
        bool failed = false;
        try {
            action();
        } catch (const Error &) {
            failed = true;
        }
        check(failed);
    };
    try {
        WindowPlacement p{100, 80, 1200, 800, 96, false};
        const std::vector<WindowWorkArea> single{{0, 0, 1920, 1040, 96}};
        check(WindowPlacement::from_json(p.to_json()).to_json() == p.to_json());
        check(fit_window_placement(p, single).to_json() == p.to_json());
        auto fit = fit_window_placement({-1800, 60, 1000, 700, 96, true},
                                        {{0, 0, 1920, 1040, 96}, {-1920, 40, 1920, 1040, 96}});
        check(fit.x == -1800 && fit.y == 60 && fit.maximized);
        fit = fit_window_placement({5000, -2000, 1400, 900, 96, true}, single);
        check(fit.x == 520 && fit.y == 0 && fit.width == 1400 && fit.height == 900 && fit.maximized);
        fit = fit_window_placement({100, 80, 1000, 700, 96, false}, {{0, 0, 3840, 2100, 192}});
        check(fit.width == 2000 && fit.height == 1400 && fit.dpi == 192);
        fit = fit_window_placement({0, 0, 2000, 1400, 192, false}, single);
        check(fit.width == 1000 && fit.height == 700 && fit.dpi == 96);
        fit = fit_window_placement({0, 0, 160, 120, 96, false}, single);
        check(fit.width == 960 && fit.height == 640);
        fit = fit_window_placement(p, {{20, 40, 640, 480, 144}});
        check(fit.x == 20 && fit.y == 40 && fit.width == 640 && fit.height == 480);
        fit = fit_window_placement({-1000, 0, 1800, 900, 96, false},
                                   {{0, 0, 1920, 1040, 96}, {-1920, 0, 1920, 1040, 144}});
        check(fit.dpi == 144 && fit.x == -1920 && fit.width == 1920);
        fit = fit_window_placement({3000, 0, 1200, 800, 96, false},
                                   {{0, 0, 1920, 1040, 96}, {4000, 0, 1920, 1040, 96}});
        check(fit.x == 4000);
        // Repeated launches must not compound DPI scaling after a saved placement.
        fit = fit_window_placement(p, {{0, 0, 3840, 2100, 192}});
        check(fit_window_placement(fit, {{0, 0, 3840, 2100, 192}}).to_json() == fit.to_json());
        for (const char *key : {"x", "y", "width", "height", "dpi", "maximized"}) {
            auto j = p.to_json();
            j.erase(key);
            rejects([&] { WindowPlacement::from_json(j); });
            j = p.to_json();
            j[key] = "wrong";
            rejects([&] { WindowPlacement::from_json(j); });
        }
        for (const auto &entry : {std::make_pair("x", 1000001),
                                  {"y", -1000001},
                                  {"width", 0},
                                  {"height", 65537},
                                  {"dpi", 0},
                                  {"dpi", 769}}) {
            auto j = p.to_json();
            j[entry.first] = entry.second;
            rejects([&] { WindowPlacement::from_json(j); });
        }
        auto j = p.to_json();
        j["width"] = 1000.0;
        rejects([&] { WindowPlacement::from_json(j); });
        j = p.to_json();
        j["extra"] = true;
        rejects([&] { WindowPlacement::from_json(j); });
        rejects([&] { fit_window_placement(p, {}); });
        rejects([&] { fit_window_placement(p, std::vector<WindowWorkArea>(257)); });
        rejects([&] { fit_window_placement(p, {{0, 0, 0, 1000, 96}}); });
        rejects([&] { fit_window_placement(p, {{0, 0, 1920, 1080, 800}}); });
        std::cout << checks << " window placement checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
