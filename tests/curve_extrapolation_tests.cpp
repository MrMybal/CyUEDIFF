#include "rich_curve_model.hpp"
#include <iostream>
#include <limits>
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
        auto p = rich_model();
        auto &curve = p.exports[0].properties["FloatCurve"]["value"];
        curve["Keys"]["value"] = Json::array({rich_key(0., 0.), rich_key(1., 10.)});
        check(!evaluate_curve(p, -1.)["ready"].get<bool>(), "Omitted extrapolation remains unknown");
        for (const auto &setting :
             {Json(4), Json("RCCE_Constant"), Json("ERichCurveExtrapolation::RCCE_Constant")}) {
            curve["PreInfinityExtrap"] = rich_tag("ByteProperty", setting);
            curve["PostInfinityExtrap"] = rich_tag("ByteProperty", setting);
            check(evaluate_curve(p, -1.)["value"] == 0. && evaluate_curve(p, 2.)["value"] == 10.,
                  "Stored constant extrapolation encodings");
        }
        for (int mode = 0; mode < 4; ++mode) {
            curve["PreInfinityExtrap"] = rich_tag("ByteProperty", mode);
            curve["PostInfinityExtrap"] = rich_tag("ByteProperty", mode);
            for (double time : {-2.25, -1., -.25, 1.25, 2., 3.25}) {
                const auto out = evaluate_curve(p, time);
                check(out["ready"], "Explicit extrapolation usable");
                const double expected =
                    mode == 3 || mode == 1 ? time * 10
                    : mode == 0            ? (time < 0                   ? time - std::floor(time)
                                              : time == std::floor(time) ? 1.
                                                                         : time - std::floor(time)) *
                                      10
                                : (std::int64_t(std::floor(time)) % 2 == 0 ? time - std::floor(time)
                                                                           : 1 - (time - std::floor(time))) *
                                      10;
                check(std::abs(out["value"].get<double>() - expected) < 1e-9,
                      "Cyclic endpoints, offsets and oscillation parity");
            }
        }
        curve["PreInfinityExtrap"] = rich_tag("ByteProperty", 5);
        check(!evaluate_curve(p, -1.)["ready"].get<bool>(), "Explicit None is not repaired to a cycle");
        curve["PostInfinityExtrap"] = rich_tag("ByteProperty", 0);
        check(!evaluate_curve(p, 1e30)["ready"].get<bool>(), "Cycle count bounded");
        curve["Keys"]["value"] = Json::array();
        curve["DefaultValue"] = rich_tag("FloatProperty", 7.);
        check(evaluate_curve(p, 100.)["value"] == 7. && evaluate_curve(p, 100.)["method"] == "stored-default",
              "Explicit empty-curve default");
        curve["DefaultValue"] = rich_tag("FloatProperty", double(std::numeric_limits<float>::max()));
        check(!evaluate_curve(p, 0.)["ready"].get<bool>(), "Engine incoming-default sentinel stays unknown");
        curve["DefaultValue"] = rich_tag("FloatProperty", 0.);
        curve.erase("Keys");
        check(!evaluate_curve(p, 0.)["ready"].get<bool>(),
              "Unknown key table cannot infer empty default curve");
        std::cout << checks << " curve extrapolation checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
