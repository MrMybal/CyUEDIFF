#pragma once
#include <cyuediff/curves.hpp>
namespace cyuediff::test {
inline Json rich_tag(const char *kind, Json value, const char *parameter = nullptr) {
    Json type = {{"name", kind}};
    if (parameter)
        type["parameters"] = Json::array({{{"name", parameter}}});
    return {{"type", type}, {"value", std::move(value)}};
}
inline Json rich_key(double t, double v, int interpolation = 0) {
    return {{"kind", "rich-curve-key"},
            {"time", t},
            {"value", v},
            {"interpMode", interpolation},
            {"tangentMode", 1},
            {"tangentWeightMode", 0},
            {"arriveTangent", 0.},
            {"arriveTangentWeight", 0.},
            {"leaveTangent", 0.},
            {"leaveTangentWeight", 0.}};
}
inline Package rich_model() {
    Package p;
    p.complete_input = true;
    p.summary.ue5_version = 1004;
    ObjectExport e;
    e.name = e.path = "Curve";
    e.class_name = "/Script/Engine.CurveFloat";
    e.coverage = "properties";
    e.properties = {
        {"FloatCurve",
         rich_tag("StructProperty",
                  {{"Keys", rich_tag("ArrayProperty",
                                     Json::array({rich_key(0., 0.), rich_key(1., 10.), rich_key(2., 0.)}),
                                     "StructProperty")}},
                  "RichCurve")}};
    p.exports.push_back(std::move(e));
    return p;
}
} // namespace cyuediff::test
