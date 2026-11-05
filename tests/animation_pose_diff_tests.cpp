#include <cyuediff/animation_pose.hpp>
#include "animation_pose_model.hpp"
#include <iostream>
#include <cmath>
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
        auto a = hierarchy_animation_model(), s = hierarchy_skeleton_model();
        auto before = animation_pose(a, s, 0), after = animation_pose(a, s, 1);
        auto d = compare_animation_poses(before, before);
        check(d["status"] == "identical" && d["changedJoints"] == 0, "Known pose equality");
        d = compare_animation_poses(before, after);
        check(d["status"] == "changed" && d["changedJoints"] == 3 && d["joints"]["tip"]["distance"] == 5.,
              "Ancestor source movement affects descendants");
        check(d["before"]["frame"] == 0 && d["after"]["frame"] == 1, "Independent frame provenance");
        AnimationPoseDiffOptions o;
        o.position_tolerance = 5.;
        check(compare_animation_poses(before, after, o)["status"] == "identical",
              "Inclusive position tolerance");
        o.position_tolerance = 4.999;
        check(compare_animation_poses(before, after, o)["status"] == "changed",
              "Above tolerance remains changed");
        AnimationPoseSelection x, y;
        y.frame = 1;
        check(compare_animation_poses(a, s, a, s, x, y)["joints"] == d["joints"],
              "Package and prepared pose APIs agree");
        auto b = a;
        b.exports[0].properties["BoneAnimationTracks"]["value"][2]["InternalTrackData"]["value"]
                               ["rotations"] = {{1., 0., 0., 0.}};
        check(compare_animation_poses(a, s, b, s)["status"] == "identical" &&
                  compare_animation_frames(a, b, 0, 0)["changedTracks"] == 1,
              "Leaf orientation is distinct from joint-origin difference");
        b = a;
        b.exports[0].properties["BoneAnimationTracks"]["value"].erase(1);
        after = animation_pose(b, s, 0);
        d = compare_animation_poses(before, after);
        check(d["status"] == "incomplete" && d["unavailableJoints"] == 2 &&
                  d["joints"]["tip"]["delta"].is_null(),
              "Unavailable ancestor cannot certify descendant equality");
        auto other = s;
        other.exports[0].native_data["bones"][2]["parentIndex"] = 0;
        o = {};
        o.position_tolerance = 1000.;
        d = compare_animation_poses(a, s, a, other, {}, {}, o);
        check(d["joints"]["tip"]["parentChanged"] && d["status"] == "changed",
              "Reparenting is significant regardless of tolerance");
        after = before;
        after.display_pose.joints.pop_back();
        after.display_pose.complete = false;
        d = compare_animation_poses(before, after);
        check(d["joints"]["tip"]["kind"] == "removed" && d["status"] == "incomplete",
              "Known hierarchy membership independent of source availability");
        after.hierarchy_complete = false;
        d = compare_animation_poses(before, after);
        check(d["joints"]["tip"]["kind"] == "unavailable", "Unknown hierarchy cannot certify removal");
        o = {};
        o.max_joint_pairs = 2;
        bool rejected = false;
        try {
            compare_animation_poses(before, before, o);
        } catch (const Error &e) {
            rejected = e.code == "limit";
        }
        check(rejected, "Pair budget");
        o = {};
        o.pose.control.cancelled = [] { return true; };
        rejected = false;
        try {
            compare_animation_poses(before, before, o);
        } catch (const Error &e) {
            rejected = e.code == "cancelled";
        }
        check(rejected, "Comparison cancellation");
        for (double invalid : std::array<double, 3>{-1., INFINITY, NAN}) {
            o = {};
            o.position_tolerance = invalid;
            rejected = false;
            try {
                compare_animation_poses(before, before, o);
            } catch (const Error &e) {
                rejected = e.code == "animation-pose-tolerance";
            }
            check(rejected, "Invalid tolerance rejected");
        }
        std::cout << checks << " animation hierarchy comparison checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
