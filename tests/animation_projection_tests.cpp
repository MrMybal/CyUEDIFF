#include "marker_fixture.hpp"
#include <cyuediff/skeleton.hpp>
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-animation-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        fs::create_directories(root);
        auto check = [&](bool v, const char *msg) {
            ++checks;
            if (!v)
                throw std::runtime_error(msg);
        };
        auto p = Package::open(save(root, "base", marker_file_fixture()));
        auto reorder = Package::open(save(root, "order", marker_file_fixture(0, true)));
        const auto snapshot = skeleton_animation_snapshot(p);
        const auto &body = snapshot["skeletons"]["TestAsset"];
        check(snapshot["complete"] && body["syncMarkers"]["names"]["Left"]["occurrences"] == 2,
              "Named marker multiplicity retained");
        check(body["curveMetadata"]["containers"]["AnimCurveMappingName"]["Smile~/curve"]["maxLOD"] == 2,
              "Named curve projection");
        check(compare_skeleton_animation(p, reorder).status() == "identical" &&
                  compare_skeletons(p, reorder).status() == "identical",
              "Marker ordering is irrelevant in named projections");
        check(!compare(p, reorder).changes.empty(),
              "Raw marker order remains visible in serialized snapshot");
        auto changed = Package::open(save(root, "changed", marker_file_fixture(1)));
        const auto diff = compare_skeleton_animation(p, changed);
        check(diff.status() == "changed" && diff.changes.size() == 2,
              "Marker replacement produces named removal and addition");
        auto curve = p;
        curve.exports[0]
            .native_data["curveMetadata"]["containers"]["AnimCurveMappingName"]["Smile~/curve"]["maxLOD"] = 4;
        const auto curve_diff = compare_skeleton_animation(p, curve);
        check(curve_diff.changes.size() == 1 &&
                  curve_diff.changes[0]["path"] == "/skeletons/TestAsset/curveMetadata/containers/"
                                                   "AnimCurveMappingName/Smile~0~1curve/maxLOD",
              "Escaped curve identity and typed LOD path");
        check(compare(p, curve).skeleton_changes.size() == 1,
              "Generic skeleton comparison includes named curve changes");
        auto partial = p;
        partial.exports[0].coverage = "partial";
        partial.exports[0].properties = nullptr;
        check(skeleton_animation_snapshot(partial)["complete"],
              "Scoped metadata independence from unrelated property coverage");
        partial.complete_input = false;
        check(!skeleton_animation_snapshot(partial)["complete"].get<bool>(),
              "Missing input cannot establish metadata equality");
        for (auto bad : {marker_file_fixture(0, false, 15, 1), marker_file_fixture(0, false, 15, 0, 0, 2),
                         marker_file_fixture(0, false, -1)}) {
            const auto unknown = Package::open(save(root, "unknown", bad));
            check(compare_skeleton_animation(unknown, unknown).status() == "incomplete",
                  "Stripped or unread names cannot establish equality");
        }
        auto old = Package::open(save(root, "old", marker_file_fixture(0, false, 1)));
        check(compare_skeleton_animation(old, old).status() == "identical",
              "Known version omission has explicit stored-metadata semantics");
        auto dup = p;
        dup.exports.push_back(dup.exports[0]);
        bool rejected = false;
        try {
            skeleton_animation_snapshot(dup);
        } catch (const Error &e) {
            rejected = e.code == "skeleton-identity";
        }
        check(rejected, "Duplicate Skeleton identity rejected");
        for (int kind = 0; kind < 7; ++kind) {
            auto malformed = p;
            auto &native = malformed.exports[0].native_data;
            auto &c = native["curveMetadata"]["containers"]["AnimCurveMappingName"]["Smile~/curve"];
            if (kind == 0)
                c["material"] = 1;
            if (kind == 1)
                c["linkedBones"][0] = 5;
            if (kind == 2)
                c["maxLOD"] = 256;
            if (kind == 3)
                c["maxLOD"] = std::uint64_t(-1);
            if (kind == 4)
                native["syncMarkers"]["names"] = nullptr;
            if (kind == 5)
                native["syncMarkers"]["stripFlags"]["global"] = 1;
            if (kind == 6)
                native["syncMarkers"]["coverage"] = "unknown";
            rejected = false;
            try {
                skeleton_animation_snapshot(malformed);
            } catch (const Error &e) {
                rejected = e.code == "skeleton-value";
            }
            check(rejected, "Host metadata type/coverage invariants validated");
        }
        for (int kind = 0; kind < 6; ++kind) {
            SkeletonOptions options;
            if (kind == 0)
                options.max_curve_containers = 1;
            if (kind == 1)
                options.max_curves = 1;
            if (kind == 2)
                options.max_curve_links = 3;
            if (kind == 3)
                options.max_markers = 2;
            if (kind == 4)
                options.max_nodes = 1;
            if (kind == 5)
                options.max_text_bytes = 1;
            rejected = false;
            try {
                skeleton_animation_snapshot(p, options);
            } catch (const Error &e) {
                rejected = e.code == "limit";
            }
            check(rejected, "Metadata-specific and shared projection budgets");
        }
        SkeletonOptions options;
        options.control.cancelled = [] { return true; };
        rejected = false;
        try {
            skeleton_animation_snapshot(p, options);
        } catch (const Error &e) {
            rejected = e.code == "cancelled";
        }
        check(rejected, "Metadata projection cancellation");
        fs::remove_all(root);
        std::cout << checks << " animation projection checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
