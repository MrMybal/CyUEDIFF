#include <cyuediff/animation_socket_pose.hpp>
#include "animation_pose_model.hpp"
#include "attachments_model.hpp"
#include <cmath>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool v, const char *why) {
            ++checks;
            if (!v)
                throw std::runtime_error(why);
        };
        auto rejects = [&](auto fn, const std::string &code) {
            try {
                fn();
            } catch (const Error &e) {
                check(e.code == code, "Expected source socket error");
                return;
            }
            throw std::runtime_error("Expected rejection");
        };
        auto a = hierarchy_animation_model(), s = attachments_model();
        const auto source_a = a.to_json(), source_s = s.to_json();
        auto pose = animation_pose(a, s, 1);
        auto sockets = animation_socket_pose(pose, s);
        auto j = sockets.to_json();
        check(j["complete"] && j["membershipComplete"] &&
                  j["sockets"]["Rig.Grip"]["position"] == Json({14., 6., 3.}),
              "Ready source affine transforms stored offset");
        check(j["frame"] == 1 && j["space"] == "source-animation-affine" && !j.contains("joints") &&
                  !j.contains("affines"),
              "Dedicated source socket report");
        check(j["bindings"]["Rig.Grip"]["location"] == Json({1., 2., 3.}) &&
                  j["bindings"]["Rig.Grip"]["affineReady"],
              "Socket offset and parent provenance");
        check(animation_socket_pose(a, s, {1}).to_json() == j, "Prepared and package overload parity");
        auto later = animation_socket_pose(a, s, {2});
        auto diff = compare_animation_socket_poses(sockets, later);
        check(diff["changedSockets"] == 2 && diff["sockets"]["Rig.Grip"]["distance"] == 5.,
              "Independent source frames move socket origins");
        AnimationPoseDiffOptions tol;
        tol.position_tolerance = 5.;
        check(compare_animation_socket_poses(sockets, later, tol)["status"] == "identical",
              "Finite source socket tolerance");
        auto moved = s;
        moved.exports[1].properties["RelativeLocation"]["value"][0] = 4.;
        check(compare_animation_socket_poses(
                  sockets, animation_socket_pose(pose, moved))["sockets"]["Rig.Grip"]["delta"] ==
                  Json({3., 0., 0.}),
              "Socket edit can be compared against same prepared pose");
        auto &root = a.exports[0].properties["BoneAnimationTracks"]["value"][0]["InternalTrackData"]["value"];
        root["rotations"] = {{0., 0., std::sqrt(.5), std::sqrt(.5)}};
        root["scales"] = {{-2., 3., 4.}};
        auto transformed = animation_socket_pose(a, s, {0}).display_pose.joints[0].position;
        check(std::abs(transformed[0] + 6.) < 1e-8 && std::abs(transformed[1] + 22.) < 1e-8 &&
                  std::abs(transformed[2] - 12.) < 1e-8,
              "Independent rotated signed-scale socket origin");
        a = hierarchy_animation_model();
        auto missing = s;
        missing.exports[1].properties.erase("RelativeLocation");
        auto unknown = animation_socket_pose(pose, missing);
        check(unknown.membership_complete && !unknown.display_pose.complete &&
                  !unknown.display_pose.joints[0].ready && unknown.display_pose.joints[1].ready,
              "Omitted socket offset leaves only that point unavailable");
        missing = s;
        missing.exports[1].properties.erase("RelativeRotation");
        missing.exports[1].properties.erase("RelativeScale");
        check(animation_socket_pose(pose, missing).to_json()["sockets"] == j["sockets"],
              "Unused socket orientation and scale are not inferred or required");
        missing = s;
        missing.exports[0].properties.erase("Sockets");
        unknown = animation_socket_pose(pose, missing);
        check(!unknown.membership_complete && unknown.display_pose.joints.empty(),
              "Contained sockets cannot substitute missing membership");
        check(compare_animation_socket_poses(sockets, unknown)["sockets"]["Rig.Grip"]["kind"] ==
                  "unavailable",
              "Unknown membership cannot certify removals");
        missing = s;
        missing.exports[0].properties["Sockets"]["value"] = Json::array();
        unknown = animation_socket_pose(pose, missing);
        check(unknown.display_pose.complete &&
                  compare_animation_socket_poses(sockets, unknown)["sockets"]["Rig.Grip"]["kind"] ==
                      "removed",
              "Stored empty membership certifies removals");
        missing = s;
        missing.exports[0].properties["Sockets"]["value"].push_back("External.Socket");
        unknown = animation_socket_pose(pose, missing);
        check(unknown.membership_complete && !unknown.display_pose.complete &&
                  !unknown.display_pose.joints.back().ready,
              "External named socket stays explicit and unavailable");
        missing = s;
        missing.exports[1].properties["BoneName"]["value"] = "VB_tip";
        check(!animation_socket_pose(pose, missing).display_pose.joints[0].ready,
              "Virtual parent is never reconstructed");
        auto partial = pose;
        partial.source_affines.erase("arm");
        check(!animation_socket_pose(partial, s).display_pose.joints[0].ready,
              "Joint origin alone cannot reconstruct affine offset");
        partial = pose;
        partial.source_affines["arm"][3] += 1;
        check(!animation_socket_pose(partial, s).display_pose.complete,
              "Inconsistent prepared affine cannot silently bind");
        partial = pose;
        partial.source_affines["arm"][0] = std::numeric_limits<double>::infinity();
        check(!animation_socket_pose(partial, s).display_pose.complete,
              "Nonfinite prepared affine unavailable");
        partial = pose;
        partial.display_pose.complete = false;
        partial.display_pose.joints.back().ready = false;
        check(animation_socket_pose(partial, s).display_pose.complete,
              "Unrelated unavailable branch does not erase ready socket scope");
        partial = pose;
        partial.skeleton_object = "Missing";
        rejects([&] { animation_socket_pose(partial, s); }, "animation-socket-selection");
        AnimationSocketOptions o;
        o.max_sockets = 1;
        rejects([&] { animation_socket_pose(pose, s, o); }, "limit");
        o = {};
        o.max_parent_joints = 2;
        rejects([&] { animation_socket_pose(pose, s, o); }, "limit");
        o = {};
        o.max_sockets = 0;
        rejects([&] { animation_socket_pose(pose, s, o); }, "animation-socket-options");
        o = {};
        bool stop = false;
        o.control.cancelled = [&] { return stop; };
        o.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Composing source animation sockets")
                stop = true;
        };
        rejects([&] { animation_socket_pose(pose, s, o); }, "cancelled");
        o = {};
        o.control.cancelled = [] { return true; };
        rejects([&] { animation_socket_pose(a, s, {1}, {}, o); }, "cancelled");
        o = {};
        o.attachments.max_text_bytes = 1;
        rejects([&] { animation_socket_pose(pose, s, o); }, "limit");
        missing = s;
        missing.exports[0].properties["Sockets"]["value"].push_back("");
        check(!animation_socket_pose(pose, missing).membership_complete,
              "Empty reference cannot certify named membership");
        tol.position_tolerance = -1.;
        rejects([&] { compare_animation_socket_poses(sockets, later, tol); }, "animation-pose-tolerance");
        partial = pose;
        partial.source_affines["arm"][0] = 1e308;
        missing = s;
        missing.exports[1].properties["RelativeLocation"]["value"][0] = 2.;
        rejects([&] { animation_socket_pose(partial, missing); }, "animation-socket-value");
        check(a.to_json() == source_a && s.to_json() == source_s,
              "Caller package bytes/models remain untouched");
        std::cout << checks << " source animation socket checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
