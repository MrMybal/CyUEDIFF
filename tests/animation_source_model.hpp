#pragma once
#include <cyuediff/animation.hpp>
namespace cyuediff::test {
inline Json animation_tag(const char *type, Json value, const char *param = nullptr) {
    Json t = {{"name", type}};
    if (param)
        t["parameters"] = Json::array({{{"name", param}}});
    return {{"type", std::move(t)}, {"value", std::move(value)}};
}
inline Package animation_source_model() {
    Package p;
    p.complete_input = true;
    p.summary.ue5_version = 1004;
    ObjectExport e;
    e.name = e.path = "Clip.Model";
    e.class_name = "/Script/Engine.AnimDataModel";
    e.coverage = "properties";
    auto &v = e.properties;
    v = Json::object();
    v["NumberOfKeys"] = animation_tag("IntProperty", 3);
    v["NumberOfFrames"] = animation_tag("IntProperty", 2);
    v["FrameRate"] = animation_tag(
        "StructProperty",
        {{"Numerator", animation_tag("IntProperty", 30)}, {"Denominator", animation_tag("IntProperty", 1)}},
        "FrameRate");
    v["PlayLength"] = animation_tag("FloatProperty", 2. / 30.);
    Json tracks = Json::array();
    for (const char *name : {"root", "arm~/track"}) {
        Json keys = {{"kind", "raw-animation-keys"},
                     {"positions", {{0., 0., 0.}, {10., 1., 0.}, {20., 2., 0.}}},
                     {"rotations", {{0., 0., 0., 1.}}},
                     {"scales", {{1., 1., 1.}}}};
        tracks.push_back(
            {{"Name", animation_tag("NameProperty", name)},
             {"BoneTreeIndex", animation_tag("IntProperty", tracks.size())},
             {"InternalTrackData", animation_tag("StructProperty", keys, "RawAnimSequenceTrack")}});
    }
    v["BoneAnimationTracks"] = animation_tag("ArrayProperty", tracks, "StructProperty");
    p.exports.push_back(std::move(e));
    return p;
}
} // namespace cyuediff::test
