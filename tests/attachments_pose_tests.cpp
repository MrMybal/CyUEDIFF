#include "attachments_model.hpp"
#include <cyuediff/attachments_pose.hpp>
#include <iostream>
#include <cmath>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool v, const char *msg) {
            ++checks;
            if (!v)
                throw std::runtime_error(msg);
        };
        auto p = virtual_bones_model();
        const auto pose = socket_pose(p);
        check(pose.complete && pose.to_json()["sockets"]["Rig.Grip"]["position"] == Json({11., 2., 3.}),
              "Stored local origin composed with raw bone affine matrix");
        ReferencePoseOptions selected;
        selected.retarget_source = "A~/pose";
        check(socket_pose(p, "Rig", selected).joints[0].position[0] == 16.,
              "Explicit stored retarget source places socket origins");
        auto after = p;
        after.exports[1].properties["RelativeLocation"]["value"][0] = 4.;
        const auto delta = compare_socket_poses(p, after);
        check(delta.status() == "changed" && delta.joints.at("Rig.Grip").at("delta") == Json({3., 0., 0.}),
              "Named socket displacement");
        ReferencePoseDiffOptions tolerance;
        tolerance.position_tolerance = 3.;
        check(compare_socket_poses(p, after, "", "", tolerance).status() == "identical",
              "Socket point tolerance");
        ReferencePoseDiffOptions independent;
        independent.pose = selected;
        independent.after_retarget_source = "";
        check(compare_socket_poses(p, p, "", "", independent).joints["Rig.Grip"]["delta"] ==
                  Json({-5., 0., 0.}),
              "Independent raw-after selection");
        auto omitted = p;
        omitted.exports[1].properties.erase("RelativeLocation");
        check(!socket_pose(omitted).complete && !socket_pose(omitted).joints[0].ready,
              "Omitted location never becomes implicit zero");
        auto no_orientation = p;
        no_orientation.exports[1].properties.erase("RelativeRotation");
        no_orientation.exports[1].properties.erase("RelativeScale");
        check(socket_pose(no_orientation).joints[0].ready &&
                  socket_pose(no_orientation).joints[0].position == pose.joints[0].position,
              "Local origin does not require unused socket rotation or scale");
        auto rotated = p;
        const double q = std::sqrt(.5);
        rotated.exports[0].native_data["localPoses"][0]["rotation"] = {0., 0., q, q};
        rotated.exports[0].native_data["localPoses"][0]["scale"] = {-2., 3., 1.};
        const auto point = socket_pose(rotated).joints[0].position;
        check(std::abs(point[0] + 6.) < 1e-9 && std::abs(point[1] + 22.) < 1e-9 && point[2] == 3.,
              "Signed scale and rotated bone origin composition");
        auto invalid = p;
        invalid.exports[0].native_data["localPoses"][0]["rotation"] = {0., 0., 0., 0.};
        check(!socket_pose(invalid).joints[0].ready, "Invalid ancestor transform leaves origin unavailable");
        auto virtual_parent = p;
        virtual_parent.exports[1].properties["BoneName"]["value"] = "VB_tip";
        check(!socket_pose(virtual_parent).joints[0].ready, "Virtual pose not rebuilt for socket parent");
        auto cycle = p;
        cycle.exports[0].properties["VirtualBones"]["value"][0]["SourceBoneName"]["value"] = "VB_tip";
        check(!attachment_snapshot(cycle)["complete"].get<bool>() && socket_pose(cycle).complete,
              "Unrelated virtual cycle does not erase checked raw-bone socket positions");
        auto missing = p;
        missing.exports[0].properties["Sockets"]["value"].push_back("External.Socket");
        check(!socket_pose(missing).complete && socket_pose(missing).joints.back().name == "External.Socket",
              "Unresolved external membership visible as unavailable point");
        auto empty = p;
        empty.exports[0].properties["Sockets"]["value"] = Json::array();
        check(socket_pose(empty).complete && socket_pose(empty).joints.empty(),
              "Explicit empty membership has no inferred contained sockets");
        ReferencePoseOptions budget;
        budget.max_joints = 2;
        bool rejected = false;
        try {
            socket_pose(p, "", budget);
        } catch (const Error &e) {
            rejected = e.code == "limit";
        }
        check(rejected, "Shared reference point budget");
        bool stop = false;
        ReferencePoseOptions cancel;
        cancel.skeleton.control.cancelled = [&] { return stop; };
        cancel.skeleton.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "Composing socket origins")
                stop = true;
        };
        rejected = false;
        try {
            socket_pose(p, "", cancel);
        } catch (const Error &e) {
            rejected = e.code == "cancelled";
        }
        check(rejected, "Socket-stage cancellation");
        rejected = false;
        try {
            socket_pose(p, "Missing");
        } catch (const Error &e) {
            rejected = e.code == "socket-selection";
        }
        check(rejected, "Explicit owner selection");
        auto overflow = p;
        overflow.exports[0].native_data["localPoses"][0]["scale"] = {1e308, 1., 1.};
        overflow.exports[0].native_data["localPoses"][1]["translation"] = {0., 0., 0.};
        overflow.exports[0].native_data["localPoses"][2]["translation"] = {0., 0., 0.};
        overflow.exports[1].properties["RelativeLocation"]["value"] = {2., 0., 0.};
        rejected = false;
        try {
            socket_pose(overflow);
        } catch (const Error &e) {
            rejected = e.code == "socket-pose-value";
        }
        check(rejected, "Derived socket overflow rejected");
        std::cout << checks << " socket pose checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
