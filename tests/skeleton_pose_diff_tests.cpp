#include <cyuediff/skeleton_pose.hpp>
#include "skeleton_model.hpp"
#include <cmath>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        auto a = skeleton_model(), b = a;
        auto same = compare_reference_poses(a, b);
        check(same.complete && same.status() == "identical" && same.changed_joints == 0,
              "Equal derived poses");
        b.exports[0].native_data["localPoses"][0]["translation"] = {3., 4., 0.};
        const auto changed = compare_reference_poses(a, b);
        check(changed.changed_joints == 3 && changed.joints["arm"]["delta"] == Json({3., 4., 0.}) &&
                  changed.joints["arm"]["distance"] == 5.,
              "Ancestor movement affects unchanged descendants");
        ReferencePoseDiffOptions tolerance;
        tolerance.position_tolerance = 5.;
        check(compare_reference_poses(a, b, "", "", tolerance).status() == "identical",
              "Inclusive display tolerance");
        tolerance.position_tolerance = 4.99;
        check(compare_reference_poses(a, b, "", "", tolerance).status() == "changed",
              "Distance above tolerance");
        b = a;
        b.exports[0].native_data["localPoses"][0]["rotation"] = {0., 0., std::sqrt(.5), std::sqrt(.5)};
        const auto rotation = compare_reference_poses(a, b);
        check(rotation.joints["root"]["kind"] == "unchanged" && rotation.changed_joints == 2,
              "Positions do not claim root rotation changes");
        b = a;
        b.exports[0].native_data["bones"][2]["parentIndex"] = 1;
        b.exports[0].native_data["localPoses"][2]["translation"] = {-10., 20., 0.};
        tolerance.position_tolerance = 100.;
        const auto reparent = compare_reference_poses(a, b, "", "", tolerance);
        check(reparent.joints["tip"]["distance"] == 0. && reparent.joints["tip"]["parentChanged"] &&
                  reparent.status() == "changed",
              "Parent identity remains significant at any tolerance");
        auto left = reference_pose(a), right = left;
        right.joints.erase(right.joints.begin() + 1);
        auto added = right.joints[0];
        added.name = "new~/joint";
        right.joints.push_back(added);
        auto membership = compare_reference_poses(left, right);
        check(membership.joints["arm"]["kind"] == "removed" &&
                  membership.joints["new~/joint"]["kind"] == "added" &&
                  membership.joints["arm"]["distance"].is_null(),
              "Missing bones have no invented displacement");
        right = left;
        right.joints[1].ready = false;
        const auto unavailable = compare_reference_poses(left, right);
        check(unavailable.status() == "incomplete" && unavailable.unavailable_joints == 1 &&
                  unavailable.joints["arm"]["delta"].is_null(),
              "Unavailable joint cannot certify equality");
        right = left;
        std::swap(right.joints[1], right.joints[2]);
        check(compare_reference_poses(left, right).status() == "identical",
              "Pair exact names independently of vector order");
        b = a;
        b.exports[0].path = "Other";
        check(compare_reference_poses(a, b, "Rig.Skeleton", "Other").status() == "identical",
              "Independent object identities");
        b = a;
        b.exports[0].coverage = "partial-properties";
        check(compare_reference_poses(a, b).complete,
              "Reference-only comparison does not certify unrelated native data");
        for (double invalid : std::array<double,3>{-1., INFINITY, NAN}) {
            ReferencePoseDiffOptions options;
            options.position_tolerance = invalid;
            bool rejected = false;
            try {
                compare_reference_poses(a, a, "", "", options);
            } catch (const Error &e) {
                rejected = e.code == "skeleton-pose-tolerance";
            }
            check(rejected, "Invalid position tolerance");
        }
        for (int kind = 0; kind < 3; ++kind) {
            ReferencePoseDiffOptions options;
            if (kind == 0)
                options.max_joint_pairs = 2;
            if (kind == 1)
                options.pose.max_joints = 2;
            if (kind == 2)
                options.pose.skeleton.max_text_bytes = 1;
            bool rejected = false;
            try {
                compare_reference_poses(left, left, options);
            } catch (const Error &e) {
                rejected = e.code == "limit";
            }
            check(rejected, "Prepared pose budgets");
        }
        right = left;
        right.joints.push_back(right.joints[0]);
        bool duplicate = false;
        try {
            compare_reference_poses(left, right);
        } catch (const Error &e) {
            duplicate = e.code == "skeleton-pose-identity";
        }
        check(duplicate, "Duplicate prepared joint identity");
        right = left;
        right.joints[0].position[0] = INFINITY;
        bool finite = false;
        try {
            compare_reference_poses(left, right);
        } catch (const Error &e) {
            finite = e.code == "skeleton-pose-value";
        }
        check(finite, "Nonfinite prepared coordinates rejected");
        left.joints[0].position[0] = -1e308;
        right = left;
        right.joints[0].position[0] = 1e308;
        bool overflow = false;
        try {
            compare_reference_poses(left, right);
        } catch (const Error &e) {
            overflow = e.code == "skeleton-pose-value";
        }
        check(overflow, "Finite positions with overflowing displacement rejected");
        bool stop = false;
        ReferencePoseDiffOptions options;
        options.pose.skeleton.control.cancelled = [&] { return stop; };
        options.pose.skeleton.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "Comparing reference poses")
                stop = true;
        };
        bool cancelled = false;
        try {
            compare_reference_poses(a, a, "", "", options);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Comparison-stage cancellation propagates");
        std::cout << checks << " reference pose comparison checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
