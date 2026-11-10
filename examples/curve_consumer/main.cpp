#include <cyuediff/curves.hpp>
#include <iostream>
namespace {
cyuediff::Package model(bool transform = false) {
    using namespace cyuediff;
    auto key = [](double t, double v) {
        return Json{{"kind", "rich-curve-key"},
                    {"time", t},
                    {"value", v},
                    {"interpMode", 0},
                    {"tangentMode", 0},
                    {"tangentWeightMode", 0},
                    {"arriveTangent", 0.},
                    {"arriveTangentWeight", 0.},
                    {"leaveTangent", 0.},
                    {"leaveTangentWeight", 0.}};
    };
    Package p;
    p.complete_input = true;
    p.summary.ue5_version = 1004;
    ObjectExport e;
    e.name = e.path = "Curve";
    e.class_name = "/Script/Engine.CurveFloat";
    e.properties = {
        {"FloatCurve",
         {{"type", {{"name", "StructProperty"}, {"parameters", Json::array({{{"name", "RichCurve"}}})}}},
          {"value",
           {{"Keys",
             {{"type",
               {{"name", "ArrayProperty"}, {"parameters", Json::array({{{"name", "StructProperty"}}})}}},
              {"value", Json::array({key(0., 0.), key(1., 8.)})}}}}}}}};
    if (transform) {
        auto tag = [](const char *kind, Json value, const char *parameter = nullptr) {
            Json type = {{"name", kind}};
            if (parameter)
                type["parameters"] = Json::array({{{"name", parameter}}});
            return Json{{"type", type}, {"value", std::move(value)}};
        };
        const auto rich = e.properties["FloatCurve"];
        Json vector = {{"FloatCurves", rich}, {"FloatCurves[1]", rich}, {"FloatCurves[2]", rich}};
        Json curve = {{"CurveName", tag("NameProperty", "Root")}};
        for (const char *part : {"TranslationCurve", "RotationCurve", "ScaleCurve"})
            curve[part] = tag("StructProperty", vector, "VectorCurve");
        e.class_name = "/Script/Engine.AnimDataModel";
        e.properties = {{"CurveData", tag("StructProperty",
                                          {{"TransformCurves",
                                            tag("ArrayProperty", Json::array({curve}), "StructProperty")}},
                                          "AnimationCurveData")}};
    }
    p.exports.push_back(std::move(e));
    // The authored transform demo declares Root as a literal name; no suffix inference is used.
    if (transform)
        p.name_values = {
            {"schema", "cyuediff.name-values/1"},
            {"root", "semantic-snapshot"},
            {"status", "complete"},
            {"names",
             {{"/objects/Curve/properties/CurveData/value/TransformCurves/value/0/CurveName/value",
               {{"base", "Root"}, {"number", 0}, {"source", "host-demo-literal"}}}}}};
    return p;
}
} // namespace
int main(int argc, char **argv) {
    using namespace cyuediff;
    try {
        if (argc == 2 && std::string(argv[1]) == "--transform")
            throw Error("arguments", "--transform requires a curve ID");
        const bool transform = argc >= 3 && std::string(argv[1]) == "--transform";
        const int first = transform ? 3 : 1;
        const auto curve_id = transform ? std::string(argv[2]) : std::string();
        if (argc - first < 0 || argc - first > 2)
            throw Error("arguments", "Expected [--transform CURVE_ID] [BEFORE [AFTER]]");
        const auto before = argc > first ? Package::open(fs::u8path(argv[first])) : model(transform);
        auto after = argc > first + 1 ? Package::open(fs::u8path(argv[first + 1])) : before;
        if (argc == first) {
            if (transform)
                after.exports[0]
                    .properties["CurveData"]["value"]["TransformCurves"]["value"][0]["TranslationCurve"]
                               ["value"]["FloatCurves"]["value"]["Keys"]["value"][1]["value"] = 10.;
            else
                after.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][1]["value"] = 10.;
        }
        CurveOptions options;
        if (transform)
            options.animation_family = AnimationCurveFamily::Transform;
        const auto lane = transform ? curve_id + "/translation.x" : std::string();
        CurveValueDiffOptions grid;
        grid.curves = options;
        grid.before_lane = grid.after_lane = lane;
        grid.time_begin = 0;
        grid.time_end = 1;
        grid.samples = 5;
        const auto sampled = compare_curve_values(before, after, grid);
        std::cout << Json{{"snapshot", curve_snapshot(before, options)},
                          {"value", evaluate_curve(before, .5, "", lane, options)},
                          {"comparison", compare_curves(before, after, "", "", options).to_json()},
                          {"sampledComparison", sampled},
                          {"components", evaluate_curve_components(before, .5, "", curve_id, options)}}
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
