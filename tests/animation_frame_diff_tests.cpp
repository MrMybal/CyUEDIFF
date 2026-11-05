#include "animation_source_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool ok, const char *why) {
            ++checks;
            if (!ok)
                throw std::runtime_error(why);
        };
        auto a = animation_source_model(), b = a;
        const auto original = a.exports[0].properties;
        auto diff = compare_animation_frames(a, a, 1, 1);
        check(diff["status"] == "identical" && diff["comparedTracks"] == 2,
              "Known local frame self comparison");
        b.exports[0].path = "Other.Model";
        auto &tracks = b.exports[0].properties["BoneAnimationTracks"]["value"];
        tracks[1]["InternalTrackData"]["value"]["positions"][1][0] = 13.;
        diff = compare_animation_frames(a, b, 1, 1);
        check(diff["changedTracks"] == 1 &&
                  diff["tracks"]["name:arm~/track"]["metrics"]["positionDistance"] == 3. &&
                  diff["afterObject"] == "Other.Model",
              "Exact names pair independent object contexts");
        AnimationFrameDiffOptions o;
        o.position_tolerance = 3.;
        check(compare_animation_frames(a, b, 1, 1, "", "", o)["status"] == "identical",
              "Position tolerance includes boundary");
        check(compare_animation_frames(a, a, 0, 2)["changedTracks"] == 2, "Independent source frame indices");
        b = a;
        for (auto &track : b.exports[0].properties["BoneAnimationTracks"]["value"])
            track["InternalTrackData"]["value"]["rotations"][0][3] = -1.;
        check(compare_animation_frames(a, b, 0, 0)["status"] == "identical" &&
                  compare_animation_models(a, b).status() == "changed",
              "Orientation equality is distinct from serialized quaternion sign");
        b = a;
        b.exports[0].properties["BoneAnimationTracks"]["value"].erase(1);
        diff = compare_animation_frames(a, b, 0, 0);
        check(diff["tracks"]["name:arm~/track"]["status"] == "removed" && diff["changedTracks"] == 1,
              "Explicit known track removal");
        check(compare_animation_frames(b, a, 0, 0)["tracks"]["name:arm~/track"]["status"] == "added",
              "Explicit known addition");
        b = a;
        b.exports[0].properties["BoneAnimationTracks"]["value"][1]["Name"]["value"] = "root";
        diff = compare_animation_frames(a, b, 0, 0);
        check(diff["status"] == "incomplete" &&
                  diff["tracks"]["name:arm~/track"]["status"] == "unavailable" && diff["changedTracks"] == 0,
              "Ambiguous opposite names never certify removal");
        b = a;
        b.exports[0].properties["BoneAnimationTracks"]["value"][1]["InternalTrackData"]["value"]["scales"] =
            Json::array();
        diff = compare_animation_frames(a, b, 0, 0);
        check(diff["status"] == "incomplete" && diff["comparedTracks"] == 1 && diff["unavailableTracks"] == 1,
              "Other usable tracks survive an unknown channel");
        b = a;
        b.complete_input = false;
        check(compare_animation_frames(a, b, 0, 0)["status"] == "incomplete",
              "Partial input is not equality");
        b = a;
        b.exports[0].properties["FrameRate"]["value"].erase("Denominator");
        diff = compare_animation_frames(a, b, 0, 0);
        check(diff["status"] == "identical" && !diff.contains("afterTimeSeconds"),
              "Local comparisons do not infer missing timing");
        b = a;
        b.exports[0].properties["BoneAnimationTracks"]["value"] = Json::array();
        check(compare_animation_frames(b, b, 0, 0)["status"] == "identical", "Explicit empty track tables");
        for (int bad = 0; bad < 5; ++bad) {
            auto limits = AnimationFrameDiffOptions{};
            if (bad == 0)
                limits.max_track_pairs = 1;
            if (bad == 1)
                limits.position_tolerance = -1.;
            if (bad == 2)
                limits.rotation_tolerance_degrees = 181.;
            bool no = false;
            try {
                compare_animation_frames(a, a, bad == 3 ? 3 : 0, 0, bad == 4 ? "missing" : "", "", limits);
            } catch (const Error &e) {
                no = e.code == "limit" || e.code == "animation-tolerance" || e.code == "animation-frame" ||
                     e.code == "animation-object";
            }
            check(no, "Limits, frames, selectors and tolerance guards");
        }
        o = {};
        bool stop = false;
        o.animation.control.cancelled = [&] { return stop; };
        o.animation.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Comparing animation source frames")
                stop = true;
        };
        bool no = false;
        try {
            compare_animation_frames(a, a, 0, 0, "", "", o);
        } catch (const Error &e) {
            no = e.code == "cancelled";
        }
        check(no, "Cancellation during comparison");
        check(original == a.exports[0].properties, "Input source remains unchanged");
        std::cout << checks << " animation frame comparison checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
