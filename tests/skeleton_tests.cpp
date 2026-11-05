#include <cyuediff/skeleton.hpp>
#include "skeleton_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    auto root =
        fs::current_path() / (".test-tmp-skeleton-projection-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        auto before = Package::open(save(root, "before", skeleton_file_fixture()));
        auto after = Package::open(save(root, "after", skeleton_file_fixture(false, false, 1)));
        auto reordered = Package::open(save(root, "reordered", skeleton_file_fixture(false, true)));
        auto snapshot = before.skeleton_snapshot();
        check(snapshot["complete"] && snapshot["skeletons"]["TestAsset"]["bones"]["arm"]["parent"] == "root",
              "Named raw hierarchy projection");
        check(snapshot["skeletons"]["TestAsset"]["bones"]["root"]["parent"].is_null(),
              "Stored root has no parent");
        check(snapshot["skeletons"]["TestAsset"]["bones"]["arm"]["sourceIndex"] == 1,
              "Inspection retains source index");
        auto comparison = compare_skeletons(before, after);
        check(comparison.status() == "changed" && comparison.changes.size() == 1 &&
                  comparison.changes[0]["path"] == "/skeletons/TestAsset/bones/arm/localPose/translation/0",
              "Changed local pose is keyed by exact bone name");
        check(compare_skeletons(before, reordered).status() == "identical",
              "Sibling source reorder does not change named projection");
        check(!compare(before, reordered).changes.empty() &&
                  compare(before, reordered).skeleton_changes.empty(),
              "Generic serialized ordering stays visible independently");
        check(!compare(before, after).to_json()["skeletonChanges"].empty(),
              "General diff includes named skeleton changes");
        ReadOptions without_hashes;
        without_hashes.fingerprints = false;
        const auto unhashed = Package::open(save(root, "unhashed", skeleton_file_fixture()), without_hashes);
        check(compare_skeletons(unhashed, unhashed).status() == "incomplete",
              "Unhashed opaque native tails cannot establish skeleton equality");
        auto missing = before;
        missing.exports[0].native_coverage = "opaque";
        missing.exports[0].native_data = Json::object();
        check(compare_skeletons(before, missing).status() == "incomplete",
              "Missing native hierarchy is incomplete");
        check(!Package{}.skeleton_snapshot()["complete"].get<bool>(),
              "No skeleton is not a complete skeleton comparison");
        auto renamed = before;
        renamed.exports[0].path = "Rig~/Body";
        check(compare_skeletons(before, renamed).changes.dump().find("Rig~0~1Body") != std::string::npos,
              "Object identities use escaped JSON pointer tokens");
        auto invalid = before;
        invalid.exports[0].native_data["localPoses"][0]["rotation"] = {0., 0.};
        bool rejected = false;
        try {
            invalid.skeleton_snapshot();
        } catch (const Error &e) {
            rejected = e.code == "skeleton-value";
        }
        check(rejected, "Malformed host-populated skeleton is rejected");
        invalid = before;
        invalid.exports[0].native_data["bones"][0]["parentIndex"] = std::uint64_t(-1);
        rejected = false;
        try {
            invalid.skeleton_snapshot();
        } catch (const Error &e) {
            rejected = e.code == "skeleton-value";
        }
        check(rejected, "Unsigned parent overflow cannot become a root");
        for (unsigned kind = 0; kind < 5; ++kind) {
            SkeletonOptions options;
            if (kind == 0)
                options.max_skeletons = 0;
            if (kind == 1)
                options.max_bones = 2;
            if (kind == 2)
                options.max_nodes = 1;
            if (kind == 3)
                options.max_text_bytes = 1;
            if (kind == 4)
                options.max_depth = 0;
            bool limited = false;
            try {
                skeleton_snapshot(before, options);
            } catch (const Error &e) {
                limited = e.code == "limit";
            }
            check(limited, "Projection budget is explicit");
        }
        SkeletonOptions options;
        options.control.cancelled = [] { return true; };
        bool cancelled = false;
        try {
            compare_skeletons(before, after, options);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Comparison cancellation propagates");
        fs::remove_all(root);
        std::cout << checks << " skeleton projection checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
