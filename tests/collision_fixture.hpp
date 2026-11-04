#pragma once
#include <cyuediff/collision.hpp>
namespace cyuediff::test {
inline Json collision_property(const Json &value, const char *type = "FloatProperty") {
    return {{"type", {{"name", type}}}, {"value", value}};
}
inline Json collision_shape(const std::string &kind, const std::string &name) {
    Json fields = {{"Name", collision_property(name, "NameProperty")},
                   {"Center", collision_property(Json::array({1., 2., 3.}), "StructProperty")},
                   {"RestOffset", collision_property(.125)}};
    if (kind != "sphere")
        fields["Rotation"] = collision_property(Json::array({0., 90., 0.}), "StructProperty");
    if (kind == "box") {
        fields["X"] = collision_property(20.);
        fields["Y"] = collision_property(30.);
        fields["Z"] = collision_property(40.);
    } else {
        fields["Radius"] = collision_property(5.);
        if (kind == "capsule")
            fields["Length"] = collision_property(10.);
    }
    return fields;
}
inline Package collision_fixture() {
    Package p;
    ObjectExport body;
    body.path = body.name = "SM.Body";
    body.class_name = "/Script/Engine.BodySetup";
    body.coverage = "properties";
    body.properties = {
        {"AggGeom", collision_property(
                        {{"SphereElems", collision_property(Json::array({collision_shape("sphere", "Ball")}),
                                                            "ArrayProperty")},
                         {"BoxElems",
                          collision_property(Json::array({collision_shape("box", "Case")}), "ArrayProperty")},
                         {"SphylElems", collision_property(Json::array({collision_shape("capsule", "Stem")}),
                                                           "ArrayProperty")}},
                        "StructProperty")},
        {"BoneName", collision_property("root", "NameProperty")}};
    p.exports.push_back(body);
    return p;
}
} // namespace cyuediff::test
