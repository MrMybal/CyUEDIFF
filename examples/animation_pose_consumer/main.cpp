#include <cyuediff/animation_pose.hpp>
#include <iostream>
namespace {
cyuediff::Json tag(const char *type, cyuediff::Json value, const char *parameter = nullptr) {
    cyuediff::Json t = {{"name", type}};
    if (parameter)
        t["parameters"] = cyuediff::Json::array({{{"name", parameter}}});
    return {{"type", std::move(t)}, {"value", std::move(value)}};
}
cyuediff::Package animation_model() {
    using namespace cyuediff;
    Package p;
    p.complete_input = true;
    p.summary.ue5_version = 1004;
    ObjectExport e;
    e.path = e.name = "Animation.Model";
    e.class_name = "/Script/Engine.AnimDataModel";
    e.properties = {
        {"NumberOfKeys", tag("IntProperty", 2)},
        {"NumberOfFrames", tag("IntProperty", 1)},
        {"FrameRate",
         tag("StructProperty",
             {{"Numerator", tag("IntProperty", 24)}, {"Denominator", tag("IntProperty", 1)}}, "FrameRate")}};
    Json tracks = Json::array();
    for (const char *name : {"root", "arm"}) {
        Json raw = {{"kind", "raw-animation-keys"},
                    {"positions", std::string(name) == "root" ? Json({{0., 0., 0.}, {3., 4., 0.}})
                                                              : Json({{10., 0., 0.}})},
                    {"rotations", {{0., 0., 0., 1.}}},
                    {"scales", {{1., 1., 1.}}}};
        tracks.push_back({{"Name", tag("NameProperty", name)},
                          {"InternalTrackData", tag("StructProperty", raw, "RawAnimSequenceTrack")}});
    }
    e.properties["BoneAnimationTracks"] = tag("ArrayProperty", tracks, "StructProperty");
    p.exports.push_back(e);
    return p;
}
cyuediff::Package skeleton_model() {
    using namespace cyuediff;
    Package p;
    p.complete_input = true;
    ObjectExport e;
    e.path = e.name = "Rig.Skeleton";
    e.class_name = "/Script/Engine.Skeleton";
    e.coverage = "properties";
    e.native_coverage = "reference-skeleton";
    Json pose = {
        {"rotation", {0., 0., 0., 1.}}, {"translation", {999., 999., 999.}}, {"scale", {1., 1., 1.}}};
    e.native_data = {{"kind", "reference-skeleton"},
                     {"bones", Json::array({{{"name", "root"}, {"parentIndex", -1}},
                                            {{"name", "arm"}, {"parentIndex", 0}}})},
                     {"localPoses", Json::array({pose, pose})},
                     {"nameToIndex", {{"root", 0}, {"arm", 1}}}};
    p.exports.push_back(e);
    return p;
}
} // namespace
int main(int argc, char **argv) {
    using namespace cyuediff;
    try {
        if (argc != 1 && argc != 3 && argc != 5)
            throw Error("arguments", "Expected zero, two or four paths: BEFORE_ANIMATION BEFORE_SKELETON "
                                     "[AFTER_ANIMATION AFTER_SKELETON]");
        const auto a = argc == 1 ? animation_model() : Package::open(fs::u8path(argv[1]));
        const auto s = argc == 1 ? skeleton_model() : Package::open(fs::u8path(argv[2]));
        const auto b = argc == 5 ? Package::open(fs::u8path(argv[3])) : a;
        const auto t = argc == 5 ? Package::open(fs::u8path(argv[4])) : s;
        const auto before = animation_pose(a, s, 0), after = animation_pose(b, t, 1);
        std::cout << Json{{"before", before.to_json()},
                          {"after", after.to_json()},
                          {"comparison", compare_animation_poses(before, after)},
                          {"selfComparison", compare_animation_poses(before, before)}}
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
