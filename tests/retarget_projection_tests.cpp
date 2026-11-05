#include "retarget_fixture.hpp"
#include <cyuediff/skeleton.hpp>
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-retarget-projection-" +
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
        auto reversed = Package::open(save(root, "reverse", retarget_file_fixture(0, 0, 17, 1013, true)));
        const auto snapshot = a.skeleton_snapshot();
        const auto &table = snapshot.at("skeletons").at("TestAsset").at("retargeting");
        check(table["coverage"] == "stored" && table["sources"]["A~/pose"]["complete"],
              "Named bound retarget projection");
        check(table["sources"]["A~/pose"]["bones"]["arm"]["localPose"]["translation"][0] == 15.,
              "Stored bone association uses raw source order");
        check(table["sources"]["A~/pose"]["bones"]["arm"]["sourceIndex"] == 1,
              "Inspection keeps retarget source index");
        auto diff = compare_skeletons(a, b);
        check(diff.status() == "changed" && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] ==
                      "/skeletons/TestAsset/retargeting/sources/A~0~1pose/bones/arm/localPose/translation/0",
              "Retarget diff uses escaped map key and exact bone name");
        check(compare_skeletons(a, reversed).status() == "identical",
              "Retarget source-map and matching sibling bone reorder preserve equality");
        auto mismatch =
            Package::open(save(root, "mismatch", retarget_file_fixture(0, 0, 17, 1013, false, true)));
        const auto mismatched = mismatch.skeleton_snapshot();
        const auto &unbound = mismatched["skeletons"]["TestAsset"]["retargeting"]["sources"]["B"];
        check(!unbound["complete"].get<bool>() && unbound["bones"].empty() &&
                  unbound["unboundLocalPoses"].size() == 2,
              "Count mismatch stays ordered and explicitly unbound");
        auto label = a;
        label.exports[0].native_data["retargeting"]["sources"]["A~/pose"]["poseName"] = "Other label";
        check(compare_skeletons(a, label).changes[0]["path"] ==
                  "/skeletons/TestAsset/retargeting/sources/A~0~1pose/poseName",
              "Pose label does not replace source identity");
        auto incomplete = Package::open(save(root, "invalid", retarget_file_fixture(0, 1)));
        check(incomplete.skeleton_snapshot()["skeletons"]["TestAsset"]["retargeting"]["coverage"] == "opaque",
              "Unread details stay explicit");
        ReadOptions read;
        read.fingerprints = false;
        auto unhashed = Package::open(save(root, "unhashed", retarget_file_fixture()), read);
        check(compare_skeletons(unhashed, unhashed).status() == "incomplete",
              "Opaque trailing data still requires a fingerprint");
        for (int kind = 0; kind < 4; ++kind) {
            SkeletonOptions options;
            if (kind == 0)
                options.max_retarget_sources = 1;
            if (kind == 1)
                options.max_retarget_transforms = 5;
            if (kind == 2)
                options.max_text_bytes = 100;
            if (kind == 3)
                options.max_nodes = 2;
            bool limited = false;
            try {
                skeleton_snapshot(a, options);
            } catch (const Error &e) {
                limited = e.code == "limit";
            }
            check(limited, "Projection budgets apply to all retarget records");
        }
        auto malformed = a;
        malformed.exports[0].native_data["retargeting"]["sources"]["B"]["localPoses"][0]["rotation"] = {0.,
                                                                                                        1.};
        bool rejected = false;
        try {
            malformed.skeleton_snapshot();
        } catch (const Error &e) {
            rejected = e.code == "skeleton-value";
        }
        check(rejected, "Malformed host retarget transforms rejected");
        malformed = a;
        malformed.exports[0].native_data["retargeting"]["skeletonGuid"] = "invalid";
        rejected = false;
        try {
            malformed.skeleton_snapshot();
        } catch (const Error &e) {
            rejected = e.code == "skeleton-value";
        }
        check(rejected, "Malformed host Skeleton GUID rejected");
        bool stop = false;
        SkeletonOptions options;
        options.control.cancelled = [&] { return stop; };
        options.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Projecting retarget sources")
                stop = true;
        };
        bool cancelled = false;
        try {
            skeleton_snapshot(a, options);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Retarget projection cancellation propagates");
        fs::remove_all(root);
        std::cout << checks << " retarget projection checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
