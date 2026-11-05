#include <cyuediff/animation_pose.hpp>
#include "animation_pose_model.hpp"
#include <cmath>
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
        auto a = hierarchy_animation_model(), s = hierarchy_skeleton_model();
        const auto original_a = a.to_json(), original_s = s.to_json();
        auto pose = animation_pose(a, s, 1);
        auto json = pose.to_json();
        check(pose.display_pose.complete && pose.display_pose.joints.size() == 3,
              "Explicit named hierarchy is complete");
        check(json["joints"]["tip"]["position"] == Json({13., 24., 0.}),
              "Stored locals compose through ancestors");
        check(json["joints"]["tip"]["parent"] == "arm" && json["bindings"]["tip"]["track"] == "name:tip",
              "Named provenance");
        check(json["frame"] == 1 && std::abs(json["timeSeconds"].get<double>() - 1. / 30.) < 1e-12,
              "Source timing retained");
        check(json["space"] == "source-animation-affine" && json["schema"] == "cyuediff.animation-pose/1",
              "Distinct source-space contract");
        check(a.to_json() == original_a && s.to_json() == original_s, "Input packages remain unchanged");
        auto &tracks = a.exports[0].properties["BoneAnimationTracks"]["value"];
        auto &root = tracks[0]["InternalTrackData"]["value"];
        root["rotations"] = {{0., 0., std::sqrt(.5), std::sqrt(.5)}};
        root["scales"] = {{2., 3., 4.}};
        json = animation_pose(a, s, 0).to_json();
        check(std::abs(json["joints"]["arm"]["position"][1].get<double>() - 20) < 1e-8 &&
                  std::abs(json["joints"]["tip"]["position"][0].get<double>() + 60) < 1e-8,
              "Parent rotation and nonuniform scale compose affinely");
        root["scales"] = {{-2., 3., 4.}};
        json = animation_pose(a, s, 0).to_json();
        check(std::abs(json["joints"]["tip"]["position"][1].get<double>() + 20) < 1e-8,
              "Negative scale retained");
        root["scales"] = {{0., 0., 0.}};
        check(animation_pose(a, s, 0).to_json()["joints"]["tip"]["position"] == Json({0., 0., 0.}),
              "Stored zero scale is valid");
        a = hierarchy_animation_model();
        s.exports[0].native_data["localPoses"][0]["translation"] = {900., 900., 900.};
        check(animation_pose(a, s, 1).to_json()["joints"]["root"]["position"] == Json({3., 4., 0.}),
              "Reference transforms are not substituted or added");
        a.exports[0].properties["BoneAnimationTracks"]["value"].erase(1);
        json = animation_pose(a, s, 1).to_json();
        check(!json["complete"].get<bool>() && json["joints"]["arm"]["position"].is_null() &&
                  json["joints"]["tip"]["position"].is_null(),
              "Missing track invalidates descendants without rest fallback");
        check(json["bindings"]["tip"]["localReady"] && json["bindings"]["arm"]["track"].is_null(),
              "Local readiness is distinct from parent availability");
        a = hierarchy_animation_model();
        auto &t = a.exports[0].properties["BoneAnimationTracks"]["value"];
        t[0]["InternalTrackData"]["value"]["rotations"] = {{0., 0., 0., 0.}};
        json = animation_pose(a, s, 0).to_json();
        check(json["joints"]["tip"]["position"].is_null(), "Invalid root quaternion propagates");
        a = hierarchy_animation_model();
        a.exports[0].properties["BoneAnimationTracks"]["value"][1]["Name"]["value"] = "Arm";
        json = animation_pose(a, s, 0).to_json();
        check(!json["complete"].get<bool>() && json["unboundTracks"] == Json({"name:Arm"}),
              "Names are case sensitive; unmatched tracks explicit");
        a = hierarchy_animation_model();
        a.exports[0].properties["BoneAnimationTracks"]["value"][1]["Name"]["value"] = "root";
        check(!animation_pose(a, s, 0).display_pose.complete, "Duplicate track names are never index-bound");
        a = hierarchy_animation_model();
        a.exports[0].properties["BoneAnimationTracks"]["value"][1].erase("Name");
        check(!animation_pose(a, s, 0).display_pose.complete, "BoneTreeIndex is not a name fallback");
        a = hierarchy_animation_model();
        a.exports[0].properties["FrameRate"]["value"].erase("Denominator");
        pose = animation_pose(a, s, 1);
        check(pose.display_pose.complete && !pose.time_seconds,
              "Missing timing does not invalidate source key pose");
        a.complete_input = false;
        check(!animation_pose(a, s, 0).display_pose.complete,
              "Incomplete animation input cannot certify pose");
        a = hierarchy_animation_model();
        s.complete_input = false;
        json = animation_pose(a, s, 0).to_json();
        check(!json["hierarchyComplete"].get<bool>() && json["joints"]["root"]["position"].is_null(),
              "Incomplete Skeleton input stays unknown");
        s = hierarchy_skeleton_model();
        auto missing = s;
        missing.exports[0].native_coverage = "opaque";
        check(animation_pose(a, missing, 0).display_pose.joints.empty() &&
                  !animation_pose(a, missing, 0).display_pose.complete,
              "Unread native hierarchy remains empty");
        AnimationPoseOptions o;
        o.max_joints = 2;
        bool rejected = false;
        try {
            animation_pose(a, s, 0, "", "", o);
        } catch (const Error &e) {
            rejected = e.code == "limit";
        }
        check(rejected, "Joint budget");
        o = {};
        o.max_hierarchy_depth = 1;
        rejected = false;
        try {
            animation_pose(a, s, 0, "", "", o);
        } catch (const Error &e) {
            rejected = e.code == "limit";
        }
        check(rejected, "Depth budget");
        o = {};
        o.control.cancelled = [] { return true; };
        rejected = false;
        try {
            animation_pose(a, s, 0, "", "", o);
        } catch (const Error &e) {
            rejected = e.code == "cancelled";
        }
        check(rejected, "Shared cancellation");
        o = {};
        bool composed = false;
        o.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Composing source animation hierarchy")
                composed = true;
        };
        animation_pose(a, s, 0, "", "", o);
        check(composed, "Composition progress");
        auto twice = s;
        twice.exports.push_back(s.exports[0]);
        twice.exports[1].path = "Second";
        rejected = false;
        try {
            animation_pose(a, twice, 0);
        } catch (const Error &e) {
            rejected = e.code == "skeleton-selection";
        }
        check(rejected && animation_pose(a, twice, 0, "", "Second").skeleton_object == "Second",
              "Explicit Skeleton owner selection");
        rejected = false;
        try {
            animation_pose(a, s, 3);
        } catch (const Error &e) {
            rejected = e.code == "animation-frame";
        }
        check(rejected, "Out of range source frame");
        a.exports[0].properties["BoneAnimationTracks"]["value"][0]["InternalTrackData"]["value"]["scales"] = {
            {1e308, 1., 1.}};
        rejected = false;
        try {
            animation_pose(a, s, 0);
        } catch (const Error &e) {
            rejected = e.code == "skeleton-pose-value" || e.code == "animation-pose-value";
        }
        check(rejected, "Affine overflow is explicit");
        std::cout << checks << " animation hierarchy pose checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

