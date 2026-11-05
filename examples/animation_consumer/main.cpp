#include <cyuediff/animation.hpp>
#include <iostream>
namespace {
cyuediff::Json tag(const char *kind, cyuediff::Json value, const char *parameter = nullptr) {
    cyuediff::Json type = {{"name", kind}};
    if (parameter)
        type["parameters"] = cyuediff::Json::array({{{"name", parameter}}});
    return {{"type", std::move(type)}, {"value", std::move(value)}};
}
cyuediff::Package host_model() {
    using namespace cyuediff;
    Package p;
    p.complete_input = true;
    p.summary.ue5_version = 1004;
    ObjectExport e;
    e.name = e.path = "Animation.Model";
    e.class_name = "/Script/Engine.AnimDataModel";
    e.properties = {
        {"NumberOfFrames", tag("IntProperty", 1)},
        {"NumberOfKeys", tag("IntProperty", 2)},
        {"FrameRate",
         tag("StructProperty",
             {{"Numerator", tag("IntProperty", 24)}, {"Denominator", tag("IntProperty", 1)}}, "FrameRate")}};
    Json raw = {{"kind", "raw-animation-keys"},
                {"positions", {{0., 0., 0.}, {3., 2., 1.}}},
                {"rotations", {{0., 0., 0., 1.}}},
                {"scales", {{1., 1., 1.}}}};
    e.properties["BoneAnimationTracks"] =
        tag("ArrayProperty",
            Json::array({{{"Name", tag("NameProperty", "root")},
                          {"InternalTrackData", tag("StructProperty", raw, "RawAnimSequenceTrack")}}}),
            "StructProperty");
    p.exports.push_back(std::move(e));
    return p;
}
} // namespace
int main(int argc, char **argv) {
    using namespace cyuediff;
    try {
        if (argc > 3)
            throw Error("arguments", "Expected zero, one or two loose asset paths");
        const auto before = argc > 1 ? Package::open(fs::u8path(argv[1])) : host_model();
        auto after = argc == 3 ? Package::open(fs::u8path(argv[2])) : before;
        if (argc == 1)
            after.exports[0].properties["BoneAnimationTracks"]["value"][0]["InternalTrackData"]["value"]
                                       ["positions"][1][0] = 5.;
        const auto after_snapshot = animation_snapshot(after);
        const auto &models = after_snapshot.at("models");
        const auto after_count = models.size() == 1 ? models.begin()->value("numberOfKeys", 0u) : 0u;
        const auto selected_frame = argc == 1 ? 1u : 0u;
        std::cout << Json{{"snapshot", animation_snapshot(before)},
                          {"frame", animation_frame(before, argc == 1 ? 1 : 0)},
                          {"comparison", compare_animation_models(before, after).to_json()},
                          {"frameComparison",
                           compare_animation_frames(before, after, selected_frame, selected_frame)},
                          {"independentFrameComparison",
                           compare_animation_frames(before, after, 0, after_count > 1 ? 1u : 0u)}}
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
