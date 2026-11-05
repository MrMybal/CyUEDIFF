#include "transform_curve_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool ok, const char *why) {
            ++checks;
            if (!ok)
                throw std::runtime_error(why);
        };
        auto a = transform_curve_model();
        const auto original = a.exports[0].properties;
        auto o = transform_curve_options();
        auto r = evaluate_curve_components(a, 1., "", "name:Root~/control", o);
        check(r["ready"] && r["values"].size() == 9 && r["values"]["translation.x"] == 1. &&
                  r["values"]["scale.z"] == 2.,
              "Nine independent component evaluations");
        check(r["rotationAxes"]["x"] == "roll-degrees" && r["values"]["rotation.y"] == 2.,
              "Stored Euler axes without quaternion or blend conversion");
        auto b = a;
        auto &data = b.exports[0].properties["CurveData"]["value"]["TransformCurves"]["value"];
        data[0]["ScaleCurve"]["value"].erase("FloatCurves[1]");
        r = evaluate_curve_components(b, 1., "", "name:Root~/control", o);
        check(!r["ready"].get<bool>() && r["values"]["scale.y"].is_null() &&
                  r["values"]["translation.x"] == 1.,
              "Missing scale never becomes one; other components survive");
        check(!evaluate_curve_components(a, -1., "", "name:Hand", o)["ready"].get<bool>(),
              "Omitted extrapolation stays unavailable");
        data[1]["CurveName"]["value"] = "Root~/control";
        check(!evaluate_curve_components(b, 1., "", "index:0", o)["ready"].get<bool>(),
              "Ambiguous names cannot yield usable group values");
        for (int bad = 0; bad < 4; ++bad) {
            bool no = false;
            try {
                evaluate_curve_components(a, bad == 3 ? std::numeric_limits<double>::infinity() : 1.,
                                          bad == 1 ? "missing" : "",
                                          bad == 0   ? ""
                                          : bad == 2 ? "missing"
                                                     : "name:Hand",
                                          o);
            } catch (const Error &e) {
                no = e.code == "curve-group" || e.code == "curve-object" || e.code == "curve-time";
            }
            check(no, "Explicit group/object/time guards");
        }
        auto scalar = rich_model();
        check(evaluate_curve_components(scalar, .5)["values"]["value"] == 5.,
              "Scalar component remains usable");
        auto vector = scalar;
        auto rich = vector.exports[0].properties["FloatCurve"];
        vector.exports[0].class_name = "/Script/Engine.CurveVector";
        vector.exports[0].properties = {
            {"FloatCurves", rich}, {"FloatCurves[1]", rich}, {"FloatCurves[2]", rich}};
        check(evaluate_curve_components(vector, .5)["values"] == Json({{"x", 5.}, {"y", 5.}, {"z", 5.}}),
              "Stored vector components");
        vector.exports[0].class_name = "/Script/Engine.CurveLinearColor";
        vector.exports[0].properties["FloatCurves[3]"] = rich;
        check(evaluate_curve_components(vector, .5)["values"].size() == 4,
              "Four color components without postprocessing");
        auto singleton = a;
        singleton.exports[0].properties["CurveData"]["value"]["TransformCurves"]["value"].erase(1);
        check(evaluate_curve_components(singleton, 1., "", "", o)["curveId"] == "name:Root~/control",
              "Single group can be selected implicitly");
        auto defaults = a;
        auto &items = defaults.exports[0].properties["CurveData"]["value"]["TransformCurves"]["value"];
        for (auto &item : items)
            for (const char *part : {"TranslationCurve", "RotationCurve", "ScaleCurve"})
                for (auto &channel : item[part]["value"]) {
                    channel["value"]["Keys"]["value"] = Json::array();
                    channel["value"]["DefaultValue"] = rich_tag("FloatProperty", 3.);
                }
        check(evaluate_curve_components(defaults, 100., "", "name:Hand", o)["ready"],
              "Explicit empty-channel defaults remain usable");
        auto partial = a;
        partial.complete_input = false;
        check(!evaluate_curve_components(partial, 1., "", "name:Hand", o)["ready"].get<bool>(),
              "Partial input does not certify component values");
        o.control.cancelled = [] { return true; };
        bool no = false;
        try {
            evaluate_curve_components(a, 1., "", "name:Hand", o);
        } catch (const Error &e) {
            no = e.code == "cancelled";
        }
        check(no, "Component evaluation cancellation");
        auto during = transform_curve_options();
        bool stopped = false;
        during.control.cancelled = [&] { return stopped; };
        during.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "Evaluating curve components")
                stopped = true;
        };
        no = false;
        try {
            evaluate_curve_components(a, 1., "", "name:Hand", during);
        } catch (const Error &e) {
            no = e.code == "cancelled";
        }
        check(no, "Cancellation during component evaluation");
        check(original == a.exports[0].properties, "Input unchanged");
        std::cout << checks << " curve component checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
