#include <cyuediff/skeleton_pose.hpp>
#include <iostream>
using namespace cyuediff;
int main(int argc, char **argv) {
    try {
        Package before, after;
        if (argc > 1) {
            before = Package::open(fs::u8path(argv[1]));
            after = argc > 2 ? Package::open(fs::u8path(argv[2])) : before;
        } else {
            // An explicit host-populated model demonstrates the public API without engine assets.
            ObjectExport e;
            e.path = "Skeleton";
            e.class_name = "/Script/Engine.Skeleton";
            e.coverage = "properties";
            e.native_coverage = "reference-skeleton";
            Json pose = {
                {"rotation", {0., 0., 0., 1.}}, {"translation", {0., 0., 0.}}, {"scale", {1., 1., 1.}}};
            auto arm = pose;
            arm["translation"] = {10., 0., 0.};
            e.native_data = {{"kind", "reference-skeleton"},
                             {"layout", "host-demo"},
                             {"bones", Json::array({{{"name", "root"}, {"parentIndex", -1}},
                                                    {{"name", "arm"}, {"parentIndex", 0}}})},
                             {"localPoses", Json::array({pose, arm})},
                             {"nameToIndex", {{"root", 0}, {"arm", 1}}}};
            before.exports.push_back(e);
            after = before;
            after.exports[0].native_data["localPoses"][1]["translation"][0] = 12.;
        }
        Json result = {{"snapshot", before.skeleton_snapshot()},
                       {"comparison", compare_skeletons(before, after).to_json()},
                       {"pose", reference_pose(before).to_json()},
                       {"poseComparison", compare_reference_poses(before, after).to_json()}};
        if (argc == 1) {
            // A declared opaque interval without a fingerprint cannot establish equality.
            auto unverified = before;
            unverified.exports[0].opaque["suffix"] = {{"bytes", 4}};
            result["unverifiedNativeTail"] = compare_skeletons(unverified, unverified).to_json();
        }
        std::cout << result.dump(2) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
