#pragma once
#include "skeleton_model.hpp"
namespace cyuediff::test {
inline Json attachment_property(const Json &v, const char *type, const char *parameter = nullptr) {
    Json t = {{"name", type}};
    if (parameter)
        t["parameters"] = Json::array({{{"name", parameter}}});
    return {{"type", t}, {"value", v}};
}
inline Package attachments_model() {
    auto p = retarget_model();
    auto &owner = p.exports[0];
    owner.path = owner.name = "Rig";
    owner.properties["Sockets"] =
        attachment_property({"Rig.Grip", "Rig.Muzzle"}, "ArrayProperty", "ObjectProperty");
    for (const char *key : {"Grip", "Muzzle"}) {
        ObjectExport socket;
        socket.name = key;
        socket.path = "Rig." + std::string(key);
        socket.outer_index = 1;
        socket.class_name = "/Script/Engine.SkeletalMeshSocket";
        socket.coverage = "properties";
        socket.properties = {
            {"SocketName", attachment_property(key, "NameProperty")},
            {"BoneName", attachment_property("arm", "NameProperty")},
            {"RelativeLocation", attachment_property({1., 2., 3.}, "StructProperty", "Vector")},
            {"RelativeRotation", attachment_property({0., 90., 0.}, "StructProperty", "Rotator")},
            {"RelativeScale", attachment_property({1., 1., 1.}, "StructProperty", "Vector")},
            {"bForceAlwaysAnimated", attachment_property(true, "BoolProperty")}};
        p.exports.push_back(socket);
    }
    return p;
}
} // namespace cyuediff::test
