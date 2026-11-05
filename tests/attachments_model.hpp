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
    owner.properties["VirtualBones"] = attachment_property(Json::array(), "ArrayProperty", "StructProperty");
    owner.properties["VirtualBones"]["type"]["parameters"][0]["parameters"] =
        Json::array({{{"name", "VirtualBone"}}});
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
        if (std::string(key) == "Muzzle")
            socket.properties["RelativeLocation"]["value"] = {3., -2., 5.};
        p.exports.push_back(socket);
    }
    return p;
}
inline Json virtual_definition(const std::string &name, const std::string &source,
                               const std::string &target) {
    return {{"VirtualBoneName", attachment_property(name, "NameProperty")},
            {"SourceBoneName", attachment_property(source, "NameProperty")},
            {"TargetBoneName", attachment_property(target, "NameProperty")}};
}
inline Package virtual_bones_model() {
    auto p = attachments_model();
    p.exports[0].properties["VirtualBones"]["value"] = Json::array(
        {virtual_definition("VB~/Grip", "root", "arm"), virtual_definition("VB_tip", "VB~/Grip", "tip")});
    return p;
}
} // namespace cyuediff::test
