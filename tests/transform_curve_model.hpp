#pragma once
#include "rich_curve_model.hpp"
namespace cyuediff::test {
inline Package transform_curve_model() {
    auto p = rich_model();
    auto &e = p.exports[0];
    const auto rich = e.properties["FloatCurve"];
    Json curves = Json::array();
    for (const char *name : {"Root~/control", "Hand"}) {
        Json curve = {{"CurveName", rich_tag("NameProperty", name)},
                      {"CurveTypeFlags", rich_tag("IntProperty", 4)}};
        for (const char *part : {"TranslationCurve", "RotationCurve", "ScaleCurve"}) {
            Json vector = Json::object();
            for (unsigned axis = 0; axis < 3; ++axis) {
                auto channel = rich;
                const double base = std::string(part) == "ScaleCurve" ? 1. : axis;
                channel["value"]["Keys"]["value"] = Json::array({rich_key(0, base), rich_key(2, base + 2)});
                vector[axis ? "FloatCurves[" + std::to_string(axis) + "]" : "FloatCurves"] =
                    std::move(channel);
            }
            curve[part] = rich_tag("StructProperty", vector, "VectorCurve");
        }
        curves.push_back(std::move(curve));
    }
    auto array = rich_tag("ArrayProperty", curves, "StructProperty");
    array["type"]["parameters"][0]["parameters"] = Json::array({{{"name", "TransformCurve"}}});
    e.class_name = "/Script/Engine.AnimDataModel";
    e.properties = {
        {"CurveData", rich_tag("StructProperty",
                               {{"TransformCurves", array},
                                {"FloatCurves", rich_tag("ArrayProperty", Json::array(), "StructProperty")}},
                               "AnimationCurveData")}};
    e.properties["CurveData"]["value"]["FloatCurves"]["type"]["parameters"][0]["parameters"] =
        Json::array({{{"name", "FloatCurve"}}});
    return p;
}
inline CurveOptions transform_curve_options() {
    CurveOptions o;
    o.animation_family = AnimationCurveFamily::Transform;
    return o;
}
} // namespace cyuediff::test
