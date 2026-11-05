#include "../src/animation_transform.hpp"
#include <cmath>
#include <limits>
#include <iostream>
using namespace cyuediff;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool ok, const char *why) {
            ++checks;
            if (!ok)
                throw std::runtime_error(why);
        };
        Json a = {{"ready", true},
                  {"positions", {0., 0., 0.}},
                  {"rotations", {0., 0., 0., 1.}},
                  {"scales", {-1., 2., 3.}}};
        auto b = a;
        auto compare = [&](const Json &x, const Json &y, double p = 0., double r = 0., double s = 0.) {
            return detail::compare_local_animation_values(x, y, p, r, s);
        };
        check(compare(a, a)["changed"] == false, "Identical local values");
        b["rotations"] = {0., 0., 0., -1.};
        check(compare(a, b)["rotationDeltaDegrees"] == 0., "Antipodal quaternions have the same orientation");
        const double half = std::sqrt(.5);
        b["rotations"] = {0., 0., half, half};
        check(std::abs(compare(a, b)["rotationDeltaDegrees"].get<double>() - 90.) < 1e-12,
              "Quarter-turn angular distance");
        b["rotations"] = {1., 0., 0., 0.};
        check(compare(a, b)["rotationDeltaDegrees"] == 180., "Half-turn angular distance");
        const double tiny = 1e-10;
        b["rotations"] = {tiny, 0., 0., std::sqrt(1. - tiny * tiny)};
        check(compare(a, b)["rotationDeltaDegrees"].get<double>() > 0.,
              "Small orientation differences do not collapse through acos rounding");
        auto c = a;
        auto q = [](double degrees) {
            constexpr double pi = 3.14159265358979323846;
            return Json::array({0., 0., std::sin(degrees * pi / 360.), std::cos(degrees * pi / 360.)});
        };
        b["rotations"] = q(179);
        c["rotations"] = q(-179);
        check(std::abs(compare(b, c)["rotationDeltaDegrees"].get<double>() - 2.) < 1e-12,
              "Shortest orientation across signed angle wrap");
        b = a;
        b["positions"] = {3., 4., 0.};
        b["scales"] = {-1.5, 2., 3.};
        auto metric = compare(a, b);
        check(metric["positionDistance"] == 5. && metric["scaleMaxDelta"] == .5 &&
                  metric["positionDelta"] == Json({3., 4., 0.}),
              "Euclidean translation and maximum scale-axis delta");
        check(compare(a, b, 5., 0., .5)["changed"] == false, "Inclusive independent tolerances");
        check(compare(a, b, 4.9, 0., .5)["positionChanged"] == true, "Position threshold");
        check(compare(a, b, 5., 0., .49)["scaleChanged"] == true, "Scale threshold");
        b = a;
        b["rotations"] = q(90);
        auto angle = compare(a, b)["rotationDeltaDegrees"].get<double>();
        check(compare(a, b, 0., angle, 0.)["rotationChanged"] == false, "Inclusive rotation threshold");
        b = a;
        b["rotations"][3] = 1.00002;
        check(compare(a, b)["changed"] == false,
              "Orientation metric normalizes only an accepted near-unit quaternion");
        for (int bad = 0; bad < 5; ++bad) {
            b = a;
            if (bad == 0)
                b["ready"] = false;
            if (bad == 1)
                b.erase("scales");
            if (bad == 2)
                b["rotations"][3] = 2.;
            if (bad == 3)
                b["positions"] = {1., 2.};
            if (bad == 4)
                b["positions"][0] = std::numeric_limits<double>::infinity();
            check(compare(a, b)["ready"] == false, "Unavailable local channels remain unknown");
        }
        b = a;
        c = a;
        b["positions"][0] = -std::numeric_limits<double>::max();
        c["positions"][0] = std::numeric_limits<double>::max();
        check(compare(b, c)["ready"] == false,
              "Finite source values with overflowing delta remain unavailable");
        for (int invalid = 0; invalid < 4; ++invalid) {
            bool no = false;
            try {
                compare(a, a, invalid == 0 ? -1. : 0., invalid == 1 ? 181. : 0.,
                        invalid == 2   ? -1.
                        : invalid == 3 ? std::numeric_limits<double>::quiet_NaN()
                                       : 0.);
            } catch (const Error &e) {
                no = e.code == "animation-tolerance";
            }
            check(no, "Invalid tolerances rejected");
        }
        OperationControl control;
        control.cancelled = [] { return true; };
        bool no = false;
        try {
            detail::compare_local_animation_values(a, a, 0, 0, 0, control);
        } catch (const Error &e) {
            no = e.code == "cancelled";
        }
        check(no, "Metric cancellation");
        check(a["rotations"] == Json({0., 0., 0., 1.}), "Raw input quaternion remains unchanged");
        std::cout << checks << " local animation transform checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
