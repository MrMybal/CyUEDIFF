#include <cyuediff/skeleton_pose.hpp>
#include "skeleton_fixture.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    auto root =
        fs::current_path() / (".test-tmp-reference-pose-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-8; };
        auto p = Package::open(save(root, "before", skeleton_file_fixture()));
        const auto original = p.to_json();
        auto pose = reference_pose(p);
        check(p.to_json()==original,"Reference pose preparation does not alter stored data");
        check(pose.complete && pose.joints.size() == 3, "Stored reference affine pose");
        check(pose.to_json()["joints"]["arm"]["position"] == Json({10., 0., 0.}),
              "Child uses parent-local translation");
        auto &native = p.exports[0].native_data;
        native["localPoses"][0]["rotation"] = {0., 0., std::sqrt(.5), std::sqrt(.5)};
        native["localPoses"][0]["translation"] = {1., 2., 3.};
        native["localPoses"][0]["scale"] = {2., 3., 4.};
        auto transformed = reference_pose(p).to_json();
        check(near(transformed["joints"]["arm"]["position"][0].get<double>(), 1.) &&
                  near(transformed["joints"]["arm"]["position"][1].get<double>(), 22.) &&
                  near(transformed["joints"]["arm"]["position"][2].get<double>(), 3.),
              "Parent signed scale and quaternion precede translation");
        native["bones"][2]["parentIndex"] = 1;
        auto nested = reference_pose(p).to_json();
        check(near(nested["joints"]["tip"]["position"][1].get<double>(), 62.),
              "Ancestor affine matrices compose iteratively");
        native["localPoses"][0]["scale"] = {-2., 3., 4.};
        check(near(reference_pose(p).to_json()["joints"]["tip"]["position"][1].get<double>(), -58.),
              "Negative scale is retained");
        native["localPoses"][1]["rotation"] = {0., 0., 0., 0.};
        auto unavailable = reference_pose(p).to_json();
        check(!unavailable["complete"].get<bool>() && !unavailable["joints"]["arm"]["ready"].get<bool>() &&
                  unavailable["joints"]["tip"]["position"].is_null(),
              "Invalid quaternion blocks its descendants without defaults");
        native["localPoses"][1]["rotation"] = {0., 0., 0., 1.};
        ReferencePoseOptions options;
        options.max_hierarchy_depth = 1;
        bool limited = false;
        try {
            reference_pose(p, "", options);
        } catch (const Error &e) {
            limited = e.code == "limit";
        }
        check(limited, "Hierarchy depth limit");
        options = {};
        options.max_joints = 2;
        limited = false;
        try {
            reference_pose(p, "", options);
        } catch (const Error &e) {
            limited = e.code == "limit";
        }
        check(limited, "Joint allocation limit");
        options = {};
        bool cancelled = false;
        options.skeleton.control.cancelled = [] { return true; };
        try {
            reference_pose(p, "", options);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Pose cancellation");
        auto twice = p;
        twice.exports.push_back(p.exports[0]);
        twice.exports[1].path = "OtherSkeleton";
        bool ambiguous = false;
        try {
            reference_pose(twice);
        } catch (const Error &e) {
            ambiguous = e.code == "skeleton-selection";
        }
        check(ambiguous && reference_pose(twice, "OtherSkeleton").object_path == "OtherSkeleton",
              "Explicit object selection");
        auto missing = p;
        missing.exports[0].native_data = Json::object();
        missing.exports[0].native_coverage = "opaque";
        check(!reference_pose(missing).complete && reference_pose(missing).joints.empty(),
              "Unavailable native hierarchy stays empty");
        native["localPoses"][0]["rotation"]={0.,0.,0.,1.};
        native["localPoses"][0]["scale"]={1e308,1.,1.};
        bool overflow=false;
        try {reference_pose(p);} catch(const Error &e) {overflow=e.code=="skeleton-pose-value";}
        check(overflow,"Derived affine coordinate overflow is explicit");
        fs::remove_all(root);
        std::cout << checks << " reference pose checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
