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
        const auto original = p.exports[0].properties;
        auto a = curve_snapshot(p);
        const auto &lane = a["owners"]["Curve"]["lanes"]["value"];
        check(a["complete"] && lane["keys"].size() == 3 && lane["timeOrder"] == "strict",
              "Time-keyed stored source curve");
        const auto middle = lane["keyOrder"][1].get<std::string>();
        auto changed = p;
        changed.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][1]["leaveTangent"] = 2.;
        auto diff = compare_curves(p, changed);
        check(diff.status() == "changed" && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/lanes/value/keys/" + middle + "/leaveTangent",
              "Stored tangent difference at stable key time");
        auto inserted = p;
        inserted.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"].insert(
            inserted.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"].begin() + 1,
            rich_key(.5, 3.));
        diff = compare_curves(p, inserted);
        check(diff.changes.size() == 2,
              "Inserted key changes its identity and stored order without shifting existing records");
        auto renamed = p;
        renamed.exports[0].path = "Other";
        check(compare_curves(p, renamed).status() == "identical", "Selected owner paths are context");
        for (int bad = 0; bad < 4; ++bad) {
            auto x = p;
            auto &keys = x.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"];
            if (bad == 0)
                keys[1]["time"] = 0.;
            if (bad == 1)
                std::swap(keys[0], keys[1]);
            if (bad == 2)
                x.exports[0].properties["FloatCurve"]["value"].erase("Keys");
            if (bad == 3)
                keys[1].erase("value");
            check(compare_curves(x, x).status() == "incomplete",
                  "Unknown, duplicate, unordered or malformed keys incomplete");
        }
        auto empty = p;
        empty.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"] = Json::array();
        check(curve_snapshot(empty)["complete"], "Explicit empty keys differ from omitted defaults");
        auto vector = p;
        vector.exports[0].class_name = "/Script/Engine.CurveVector";
        vector.exports[0].properties["FloatCurves"] = vector.exports[0].properties["FloatCurve"];
        vector.exports[0].properties.erase("FloatCurve");
        check(curve_snapshot(vector)["owners"]["Curve"]["lanes"].size() == 3 &&
                  !curve_snapshot(vector)["complete"].get<bool>(),
              "Fixed vector lanes with missing components explicit");
        auto anim = p;
        auto &e = anim.exports[0];
        e.class_name = "/Script/Engine.AnimDataModel";
        Json entries = Json::array();
        for (const char *n : {"Smile~/curve", "Weight"})
            entries.push_back(
                {{"CurveName", rich_tag("NameProperty", n)}, {"FloatCurve", e.properties["FloatCurve"]}});
        e.properties = {
            {"CurveData", rich_tag("StructProperty",
                                   {{"FloatCurves", rich_tag("ArrayProperty", entries, "StructProperty")}},
                                   "AnimationCurveData")}};
        const auto snapshot = curve_snapshot(anim);
        check(snapshot["complete"] && snapshot["owners"]["Curve"]["lanes"].contains("name:Smile~/curve"),
              "Named animation float curves");
        auto order = anim;
        std::swap(order.exports[0].properties["CurveData"]["value"]["FloatCurves"]["value"][0],
                  order.exports[0].properties["CurveData"]["value"]["FloatCurves"]["value"][1]);
        check(compare_curves(anim, order).status() == "identical",
              "Animation curve storage order irrelevant");
        auto duplicate = anim;
        duplicate.exports[0]
            .properties["CurveData"]["value"]["FloatCurves"]["value"][1]["CurveName"]["value"] =
            "Smile~/curve";
        check(compare_curves(duplicate, duplicate).status() == "incomplete",
              "Duplicate source names retained as indexed records");
        for (int limit = 0; limit < 6; ++limit) {
            CurveOptions o;
            if (limit == 0)
                o.max_owners = 0;
            if (limit == 1)
                o.max_lanes = 0;
            if (limit == 2)
                o.max_keys = 1;
            if (limit == 3)
                o.max_nodes = 1;
            if (limit == 4)
                o.max_text_bytes = 1;
            if (limit == 5)
                o.max_depth = 0;
            auto bounded=p;
            if(limit==5)bounded.exports[0].properties["FloatCurve"]["value"]["Future"]=Json::array({Json::array({1})});
            bool no = false;
            try {
                curve_snapshot(bounded, o);
            } catch (const Error &e) {
                no = e.code == "limit";
            }
            check(no, "Curve resource budget");
        }
        auto nonfinite = p;
        nonfinite.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][1]["time"] =
            std::numeric_limits<double>::infinity();
        bool no = false;
        try {
            curve_snapshot(nonfinite);
        } catch (const Error &e) {
            no = e.code == "curve-value";
        }
        check(no, "Non-finite host key refused");
        CurveOptions o;
        o.control.cancelled = [] { return true; };
        no = false;
        try {
            compare_curves(p, p, "", "", o);
        } catch (const Error &e) {
            no = e.code == "cancelled";
        }
        check(no, "Curve comparison cancellation");
        check(original == p.exports[0].properties, "Input unchanged");
        std::cout << checks << " rich curve model checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
