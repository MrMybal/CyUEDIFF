#include "retarget_fixture.hpp"
#include <cyuediff/skeleton_pose.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-retarget-pose-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        fs::create_directories(root);
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        auto a = Package::open(save(root, "a", retarget_file_fixture()));
        auto b = Package::open(save(root, "b", retarget_file_fixture(2)));
        auto raw = reference_pose(a);
        ReferencePoseOptions selected;
        selected.retarget_source = "A~/pose";
        const auto pose = reference_pose(a, "", selected);
        check(pose.complete && pose.to_json()["retargetSource"] == "A~/pose" &&
                  pose.to_json()["joints"]["arm"]["position"] == Json({15., 0., 0.}),
              "Named source replaces local matrices explicitly");
        check(raw.retarget_source.empty() &&
                  raw.to_json()["joints"]["arm"]["position"] == Json({10., 0., 0.}),
              "Default remains stored raw reference");
        ReferencePoseDiffOptions diff;
        diff.pose = selected;
        auto moved = compare_reference_poses(a, b, "", "", diff);
        check(moved.status() == "changed" && moved.joints["arm"]["distance"] == 2. &&
                  moved.to_json()["beforeRetargetSource"] == "A~/pose",
              "Both sides inherit the selected named source");
        diff.after_retarget_source = "B";
        check(compare_reference_poses(a, b, "", "", diff).status() == "identical",
              "Independent after source identity");
        diff.after_retarget_source = std::string{};
        const auto against_raw = compare_reference_poses(a, b, "", "", diff);
        check(against_raw.joints["arm"]["delta"] == Json({-5., 0., 0.}) &&
                  against_raw.to_json()["afterRetargetSource"].is_null(),
              "Explicit empty after source selects raw");
        auto renamed = a;
        renamed.exports[0].native_data["retargeting"]["sources"]["A~/pose"]["poseName"] =
            "Another display label";
        check(reference_pose(renamed, "", selected).retarget_source == "A~/pose",
              "Selection uses map key, never display pose label");
        selected.retarget_source = "DisplayPose";
        bool absent = false;
        try {
            reference_pose(a, "", selected);
        } catch (const Error &e) {
            absent = e.code == "skeleton-retarget-selection";
        }
        check(absent, "Missing named source never falls back to raw");
        selected.retarget_source = "A~/pose";
        auto incomplete = Package::open(save(root, "bad", retarget_file_fixture(0, 1)));
        const auto unavailable = reference_pose(incomplete, "", selected);
        check(!unavailable.complete && unavailable.joints.empty() && !unavailable.diagnostics.empty(),
              "Unread source table cannot produce an invented pose");
        auto mismatch =
            Package::open(save(root, "mismatch", retarget_file_fixture(0, 0, 17, 1013, false, true)));
        check(!reference_pose(mismatch, "", selected).complete &&
                  reference_pose(mismatch, "", selected).joints.empty(),
              "Count mismatch cannot borrow raw transforms");
        auto transformed = a;
        auto &poses = transformed.exports[0].native_data["retargeting"]["sources"]["A~/pose"]["localPoses"];
        poses[0]["rotation"] = {0., 0., std::sqrt(.5), std::sqrt(.5)};
        poses[0]["scale"] = {2., 3., 4.};
        const auto rotated = reference_pose(transformed, "", selected).to_json();
        check(std::abs(rotated["joints"]["arm"]["position"][1].get<double>() - 30.) < 1e-8,
              "Selected source uses affine parent rotation and signed scale");
        poses[0]["rotation"] = {0., 0., 0., 0.};
        const auto invalid = reference_pose(transformed, "", selected);
        check(!invalid.complete && !invalid.joints[1].ready,
              "Invalid selected rotation blocks descendants without defaults");
        const auto unchanged = a.to_json();
        reference_pose(a, "", selected);
        check(a.to_json() == unchanged, "Named pose evaluation does not mutate stored values");
        ReferencePoseOptions budget = selected;
        budget.max_joints = 2;
        bool limited = false;
        try {
            reference_pose(a, "", budget);
        } catch (const Error &e) {
            limited = e.code == "limit";
        }
        check(limited, "Selected poses obey joint allocation limit");
        bool stop = false;
        selected.skeleton.control.cancelled = [&] { return stop; };
        selected.skeleton.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Composing reference pose")
                stop = true;
        };
        bool cancelled = false;
        try {
            reference_pose(a, "", selected);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Selected pose composition cancellation");
        fs::remove_all(root);
        std::cout << checks << " retarget pose checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
