#include "rich_curve_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        auto close = [&](double a, double b) { return std::abs(a - b) < 1e-8; };
        auto p = rich_model();
        auto &keys = p.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"];
        keys = Json::array({rich_key(0, 0, 2), rich_key(1, 1, 2)});
        keys[0]["tangentWeightMode"] = 2;
        keys[0]["leaveTangentWeight"] = .2;
        keys[1]["tangentWeightMode"] = 1;
        keys[1]["arriveTangentWeight"] = .4;
        // Independently parameterize control points x=(0,.2,.6,1), y=(0,0,1,1).
        for (double u : {.01, .1, .25, .5, .75, .9, .99}) {
            const double x = 3 * (1 - u) * (1 - u) * u * .2 + 3 * (1 - u) * u * u * .6 + u * u * u;
            const double y = 3 * (1 - u) * u * u + u * u * u;
            const auto out = evaluate_curve(p, x);
            check(out["ready"] && out["method"] == "cubic-weighted" && close(out["value"].get<double>(), y),
                  "Weighted Bezier inverse matches independent parameter samples");
        }
        // Overshooting time handles have three roots at x=.5; choose the largest parameter.
        keys[0]["leaveTangentWeight"] = 2.;
        keys[1]["arriveTangentWeight"] = 2.;
        const double u = (5. + std::sqrt(15.)) / 10.;
        const double expected = 3 * (1 - u) * u * u + u * u * u;
        check(close(evaluate_curve(p, .5)["value"].get<double>(), expected),
              "Largest in-range time root selected");
        keys[0]["leaveTangentWeight"] = 0.;
        keys[1]["arriveTangentWeight"] = 0.;
        check(close(evaluate_curve(p, .5)["value"].get<double>(), .5), "Zero-length handles");
        keys[0]["leaveTangentWeight"] = -1.;
        check(!evaluate_curve(p, .5)["ready"].get<bool>() && evaluate_curve(p, 0.)["ready"],
              "Negative active weight unavailable; exact key preserved");
        keys[0]["tangentWeightMode"] = 255;
        check(!evaluate_curve(p, .5)["ready"].get<bool>(), "Unknown weight mode unavailable");
        keys[0]["tangentWeightMode"] = 2;
        keys[0]["leaveTangentWeight"] = .2;
        bool stop = false;
        CurveOptions o;
        o.control.cancelled = [&] { return stop; };
        o.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "Solving weighted curve time")
                stop = true;
        };
        bool cancelled = false;
        try {
            evaluate_curve(p, .3, "", "", o);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Cancellation during bounded root solve");
        std::cout << checks << " weighted curve checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
