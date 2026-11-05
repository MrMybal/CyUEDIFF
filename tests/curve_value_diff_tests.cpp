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
        auto a = rich_model(), b = a;
        b.exports[0].path = "Other";
        b.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][1]["value"] = 12.;
        CurveValueDiffOptions o;
        o.time_begin = 0;
        o.time_end = 2;
        o.samples = 5;
        auto diff = compare_curve_values(a, b, o);
        check(diff["status"] == "changed" && diff["changedSamples"] == 3 &&
                  diff["maximumAbsoluteDelta"] == 2.,
              "Explicit grid changed values and metrics");
        check(diff["samples"][1]["delta"] == 1. && diff["beforeObject"] == "Curve" &&
                  diff["afterObject"] == "Other",
              "Selected contexts and evaluated deltas");
        o.value_tolerance = 2.;
        check(compare_curve_values(a, b, o)["status"] == "identical", "Inclusive absolute tolerance");
        o.time_begin = -1;
        check(compare_curve_values(a, a, o)["status"] == "incomplete",
              "Unresolved outside samples do not establish equality");
        o.time_begin = o.time_end = 1;
        o.samples = 1;
        o.value_tolerance = 0;
        check(compare_curve_values(a, b, o)["samples"][0]["delta"] == 2., "Explicit single coordinate");
        for (int invalid = 0; invalid < 5; ++invalid) {
            auto bad = o;
            if (invalid == 0)
                bad.samples = 0;
            if (invalid == 1) {
                bad.time_begin = 2;
                bad.time_end = 1;
            }
            if (invalid == 2)
                bad.value_tolerance = -1;
            if (invalid == 3) {
                bad.samples = 2;
                bad.max_samples = 1;
            }
            if (invalid == 4) {
                bad.time_begin = 0;
                bad.time_end = 1;
            }
            bool no = false;
            try {
                compare_curve_values(a, b, bad);
            } catch (const Error &e) {
                no = e.code == "curve-grid" || e.code == "limit";
            }
            check(no, "Invalid sample grid or configured budget");
        }
        auto overflow_before = a, overflow_after = a;
        overflow_before.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][1]["value"] =
            -std::numeric_limits<double>::max();
        overflow_after.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][1]["value"] =
            std::numeric_limits<double>::max();
        const auto overflow = compare_curve_values(overflow_before, overflow_after, o);
        check(overflow["status"] == "incomplete" && overflow["unavailableSamples"] == 1 &&
                  overflow["maximumAbsoluteDelta"].is_null() &&
                  overflow["samples"][0]["before"]["ready"] == true &&
                  overflow["samples"][0]["after"]["ready"] == true,
              "Finite values with overflowing delta remain unavailable");
        auto empty = a;
        empty.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"] = Json::array();
        empty.exports[0].properties["FloatCurve"]["value"]["DefaultValue"] = {
            {"type", {{"name", "FloatProperty"}}}, {"value", 7.}};
        auto extreme = o;
        extreme.time_begin = -std::numeric_limits<double>::max();
        extreme.time_end = std::numeric_limits<double>::max();
        extreme.samples = 5;
        const auto grid = compare_curve_values(empty, empty, extreme);
        check(grid["status"] == "identical" && grid["samples"][2]["time"] == 0. &&
                  grid["comparedSamples"] == 5,
              "Convex grid avoids endpoint-subtraction overflow");
        o.curves.control.cancelled = [] { return true; };
        bool no = false;
        try {
            compare_curve_values(a, b, o);
        } catch (const Error &e) {
            no = e.code == "cancelled";
        }
        check(no, "Sample comparison cancellation");
        std::cout << checks << " sampled curve comparison checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
