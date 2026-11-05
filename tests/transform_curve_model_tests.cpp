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
        auto s = curve_snapshot(a, o);
        const auto &owner = s["owners"]["Curve"];
        check(s["complete"] && owner["lanes"].size() == 18 && owner["curveGroups"].size() == 2,
              "Nine exact channels per named transform");
        check(owner["lanes"].contains("name:Root~/control/rotation.z"),
              "Raw names retained in stable lane identities");
        check(curve_snapshot(a)["owners"]["Curve"]["lanes"].empty(),
              "Float scope remains separate and backward compatible");
        check(compare_curves(a, a, "", "", o).status() == "identical", "Selected transform self comparison");
        auto b = a;
        auto &curves = b.exports[0].properties["CurveData"]["value"]["TransformCurves"]["value"];
        std::swap(curves[0], curves[1]);
        check(compare_curves(a, b, "", "", o).status() == "identical", "Unique group order is irrelevant");
        curves[1]["TranslationCurve"]["value"]["FloatCurves"]["value"]["Keys"]["value"][1]["value"] = 4.;
        auto diff = compare_curves(a, b, "", "", o);
        check(diff.changes.size() == 1 && diff.changes[0]["path"].get<std::string>().find(
                                              "Root~0~1control~1translation.x") != std::string::npos,
              "Named changed component and pointer escaping");
        for (int bad = 0; bad < 5; ++bad) {
            auto x = a;
            auto &data = x.exports[0].properties["CurveData"]["value"];
            auto &items = data["TransformCurves"]["value"];
            if (bad == 0)
                data.erase("TransformCurves");
            if (bad == 1)
                items[0].erase("ScaleCurve");
            if (bad == 2)
                items[0]["RotationCurve"]["value"].erase("FloatCurves[1]");
            if (bad == 3)
                items[1]["CurveName"]["value"] = "Root~/control";
            if (bad == 4)
                items[0].erase("CurveName");
            check(compare_curves(x, x, "", "", o).status() == "incomplete",
                  "Missing or ambiguous data stays incomplete");
        }
        auto empty = a;
        empty.exports[0].properties["CurveData"]["value"]["TransformCurves"]["value"] = Json::array();
        check(curve_snapshot(empty, o)["complete"], "Explicit empty transform table is known");
        auto metadata = a;
        metadata.exports[0].properties["CurveData"]["value"]["TransformCurves"]["value"][0]
                                      ["TranslationCurve"]["value"]["Future"] = rich_tag("IntProperty", 3);
        check(compare_curves(a, metadata, "", "", o).changes.size() == 1,
              "Additional vector metadata remains visible");
        for (int limit = 0; limit < 5; ++limit) {
            auto bounded = o;
            if (limit == 0)
                bounded.max_lanes = 17;
            if (limit == 1)
                bounded.max_keys = 35;
            if (limit == 2)
                bounded.max_nodes = 1;
            if (limit == 3)
                bounded.max_text_bytes = 1;
            if (limit == 4)
                bounded.max_depth = 0;
            bool no = false;
            try {
                curve_snapshot(limit == 4 ? metadata : a, bounded);
            } catch (const Error &e) {
                no = e.code == "limit";
            }
            check(no, "Shared transform resource bound");
        }
        o.control.cancelled = [] { return true; };
        bool no = false;
        try {
            curve_snapshot(a, o);
        } catch (const Error &e) {
            no = e.code == "cancelled";
        }
        check(no, "Transform projection cancellation");
        auto invalid = transform_curve_options();
        invalid.animation_family = static_cast<AnimationCurveFamily>(100);
        no = false;
        try {
            curve_snapshot(a, invalid);
        } catch (const Error &e) {
            no = e.code == "curve-family";
        }
        check(no, "Invalid host family rejected");
        check(original == a.exports[0].properties, "Input model unchanged");
        std::cout << checks << " transform curve projection checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
