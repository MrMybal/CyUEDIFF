#include "collision_file_fixture.hpp"
#include <chrono>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    int checks = 0;
    try {
        auto check = [&](bool good, const char *message) {
            ++checks;
            if (!good)
                throw std::runtime_error(message);
        };
        auto rejects = [&](const Package &p, const CollisionOptions &options, const char *code) {
            bool failed = false;
            try {
                collision_snapshot(p, options);
            } catch (const Error &e) {
                failed = e.code == code;
            }
            check(failed, "Collision operation rejects invalid budget/input");
        };
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto root = fs::u8path(argv[2]);
            fs::create_directories(root);
            for (int version : {1009, 1013}) {
                auto before = collision_extended_fixture(), after = before, reordered = before,
                     incomplete = before;
                after.exports[0]
                    .properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Radius"]["value"] = 7.;
                auto &array = reordered.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"];
                std::swap(array[0], array[1]);
                incomplete.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0].erase(
                    "Radius");
                const auto suffix = version == 1009 ? "53" : "55";
                save(root, std::string("before") + suffix, collision_file_fixture(before, version));
                save(root, std::string("after") + suffix, collision_file_fixture(after, version));
                save(root, std::string("reordered") + suffix, collision_file_fixture(reordered, version));
                save(root, std::string("incomplete") + suffix, collision_file_fixture(incomplete, version));
            }
            save(root, "no-collision", fixture(1013));
            return 0;
        }
        const auto temporary = fs::current_path() /
                               ("build-collision-roundtrip-" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temporary);
        for (int version : {1009, 1013}) {
            auto source = collision_extended_fixture();
            auto read = Package::open(save(temporary, "roundtrip", collision_file_fixture(source, version)));
            auto projection = read.collision_snapshot();
            check(read.exports[0].class_name == "/Script/Engine.BodySetup" && projection["complete"],
                  "Classic reflected collision package round trip");
            check(projection["bodies"]["TestAsset"]["shapes"].size() == 6,
                  "All five collision kinds read from tagged bytes");
            check(projection["bodies"]["TestAsset"]["shapes"]["convex:name:Hull"]["geometry"]["indices"]
                          .size() == 12,
                  "Convex triangle indices survive the package reader");
        }
        fs::remove_all(temporary);
        auto package = collision_fixture();
        const auto original = package.to_json();
        auto snapshot = package.collision_snapshot();
        auto &body = snapshot["bodies"]["SM.Body"];
        check(snapshot["complete"] && body["shapes"].size() == 3, "All simple stored shapes projected");
        check(body["shapes"]["sphere:name:Ball"]["geometry"]["radius"] == 5., "Sphere radius retained");
        check(body["shapes"]["box:name:Case"]["geometry"]["x"] == 20., "Box full dimension is not halved");
        check(body["shapes"]["capsule:name:Stem"]["geometry"]["length"] == 10.,
              "Capsule stored segment length retained");
        check(body["shapes"]["box:name:Case"]["geometry"]["rotation"] == Json::array({0., 90., 0.}),
              "Rotator order retained");
        for (const char *field : {"Center", "Radius"}) {
            auto missing = package;
            missing.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0].erase(field);
            auto model = missing.collision_snapshot();
            check(!model["complete"] && !model["bodies"]["SM.Body"]["shapes"]["sphere:name:Ball"]["complete"],
                  "Missing geometry is never filled with engine defaults");
        }
        auto negative = package;
        negative.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Radius"]["value"] =
            -1.;
        auto bad = negative.collision_snapshot();
        check(!bad["complete"] &&
                  bad["bodies"]["SM.Body"]["shapes"]["sphere:name:Ball"]["unresolvedProperties"].contains(
                      "Radius"),
              "Invalid dimensions retain the original property");
        auto opaque = package;
        opaque.exports[0].properties["AggGeom"].erase("value");
        opaque.exports[0].properties["AggGeom"]["opaque"] = {{"bytes", 48},
                                                             {"sha256", "opaque-shape-payload"}};
        check(!opaque.collision_snapshot()["complete"], "Opaque aggregate is not an empty collision set");
        for (const char *key : {"Center", "Rotation"}) {
            auto reflected = package;
            auto &v =
                reflected.exports[0].properties["AggGeom"]["value"]["BoxElems"]["value"][0][key]["value"];
            v = std::string(key) == "Center" ? Json{{"X", collision_property(1.)},
                                                    {"Y", collision_property(2.)},
                                                    {"Z", collision_property(3.)}}
                                             : Json{{"Pitch", collision_property(0.)},
                                                    {"Yaw", collision_property(90.)},
                                                    {"Roll", collision_property(0.)}};
            check(reflected.collision_snapshot()["bodies"]["SM.Body"]["shapes"]["box:name:Case"]["complete"],
                  "Reflected vector and rotator fields are supported");
        }
        for (int budget = 0; budget < 4; ++budget) {
            CollisionOptions options;
            if (budget == 0)
                options.max_bodies = 0;
            if (budget == 1)
                options.max_shapes = 2;
            if (budget == 2)
                options.max_nodes = 2;
            if (budget == 3)
                options.max_text_bytes = 1;
            rejects(package, options, "limit");
        }
        CollisionOptions cancelled;
        cancelled.control.cancelled = [] { return true; };
        rejects(package, cancelled, "cancelled");
        auto repeated = package;
        repeated.exports.push_back(repeated.exports.front());
        rejects(repeated, {}, "collision-identity");
        for (const char *collection : {"SphereElems", "BoxElems", "SphylElems"})
            for (const auto &invalid : {Json(nullptr), Json("opaque"), Json::array({1, 2})}) {
                auto damaged = package;
                damaged.exports[0].properties["AggGeom"]["value"][collection]["value"][0]["Center"]["value"] =
                    invalid;
                auto inspected = damaged.collision_snapshot();
                check(!inspected["complete"], "Malformed geometric components are unresolved");
            }
        auto unresolved_name = package;
        unresolved_name.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Name"] = {
            {"opaque", {{"bytes", 8}, {"sha256", "undecoded-name"}}}};
        auto named = unresolved_name.collision_snapshot();
        check(named["bodies"]["SM.Body"]["shapes"]["sphere:index:0"]["unresolvedProperties"].contains("Name"),
              "An opaque shape name is retained");
        CollisionOptions shallow;
        shallow.max_depth = 0;
        rejects(package, shallow, "limit");
        auto extended = package;
        Json tapered = collision_shape("capsule", "Taper");
        tapered.erase("Radius");
        tapered["Radius0"] = collision_property(2.);
        tapered["Radius1"] = collision_property(4.);
        Json convex = {{"Name", collision_property("Hull", "NameProperty")},
                       {"VertexData",
                        collision_property(Json::array({Json::array({0., 0., 0.}), Json::array({1., 0., 0.}),
                                                        Json::array({0., 1., 0.})}),
                                           "ArrayProperty")},
                       {"IndexData", collision_property(Json::array({0, 1, 2}), "ArrayProperty")},
                       {"Transform", collision_property({{"rotation", {0., 0., 0., 1.}},
                                                         {"translation", {2., 3., 4.}},
                                                         {"scale", {1., 2., 3.}}},
                                                        "StructProperty")}};
        auto &agg = extended.exports[0].properties["AggGeom"]["value"];
        agg["TaperedCapsuleElems"] = collision_property(Json::array({tapered}), "ArrayProperty");
        agg["ConvexElems"] = collision_property(Json::array({convex}), "ArrayProperty");
        auto extended_model = extended.collision_snapshot();
        const auto &extra = extended_model["bodies"]["SM.Body"]["shapes"];
        check(extended_model["complete"] && extra.size() == 5,
              "Tapered capsules and convex source data projected");
        check(extra["tapered-capsule:name:Taper"]["geometry"]["radius1"] == 4., "Unequal radii preserved");
        check(extra["convex:name:Hull"]["geometry"]["indices"] == Json::array({0, 1, 2}),
              "Stored topology preserved");
        for (const auto &invalid : {Json::array({0, 1}), Json::array({0, -1, 2}), Json::array({0, 1, 3}),
                                    Json::array({0, 1, 1.5}), Json::array({0, 1, std::uint64_t(-1)})}) {
            auto wrong = extended;
            wrong.exports[0].properties["AggGeom"]["value"]["ConvexElems"]["value"][0]["IndexData"]["value"] =
                invalid;
            auto model = wrong.collision_snapshot();
            check(
                !model["complete"] &&
                    model["bodies"]["SM.Body"]["shapes"]["convex:name:Hull"]["unresolvedProperties"].contains(
                        "IndexData"),
                "Invalid convex topology is retained without a guessed hull");
        }
        for (bool index_budget : {false, true}) {
            CollisionOptions bound;
            if (index_budget)
                bound.max_indices = 2;
            else
                bound.max_vertices = 2;
            rejects(extended, bound, "limit");
        }
        ObjectExport owner;
        owner.path = "SM";
        owner.class_name = "/Script/Engine.StaticMesh";
        owner.properties = {{"BodySetup", collision_property("SM.Body", "ObjectProperty")}};
        extended.exports.push_back(owner);
        auto associated = extended.collision_snapshot();
        check(associated["owners"]["SM"]["bodyReferences"][0]["resolved"],
              "Static mesh body reference resolves locally");
        extended.exports.back().properties["BodySetup"]["value"] = "/Other/External.Body";
        check(!extended.collision_snapshot()["complete"],
              "External body is reported without reading another asset");
        extended.exports.back().class_name = "/Script/Engine.PhysicsAsset";
        extended.exports.back().properties = {
            {"SkeletalBodySetups", collision_property(Json::array({"SM.Body", nullptr}), "ArrayProperty")}};
        check(extended.collision_snapshot()["owners"]["SM"]["bodyReferences"].size() == 2,
              "Physics-asset body references retained");
        extended.exports[0].properties["AggGeom"]["value"]["LevelSetElems"] = {
            {"opaque", {{"bytes", 64}, {"sha256", "opaque-native-levelset"}}}};
        check(!extended.collision_snapshot()["complete"], "Native level sets stay explicitly unsupported");
        auto reordered = package;
        auto &balls = reordered.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"];
        balls.push_back(collision_shape("sphere", "Second"));
        const auto ordered = reordered;
        std::swap(balls[0], balls[1]);
        auto order_diff = compare_collisions(ordered, reordered);
        check(order_diff.complete && order_diff.changes.empty(),
              "Unique named shapes retain identity after reordering");
        check(!compare(ordered, reordered).changes.empty() &&
                  compare(ordered, reordered).collision_changes.empty(),
              "Generic serialized order changes remain distinct from collision correspondence");
        balls[1]["Radius"]["value"] = 7.;
        const auto radius_diff = compare_collisions(ordered, reordered);
        check(radius_diff.status() == "changed" && radius_diff.changes.size() == 1 &&
                  radius_diff.changes[0]["path"] == "/bodies/SM.Body/shapes/sphere:name:Ball/geometry/radius",
              "Reordered named sphere reports only its actual radius change");
        check(!compare(ordered, reordered).collision_changes.empty(),
              "General package diff includes specialized collision changes");
        auto anonymous = ordered;
        auto &anon = anonymous.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"];
        anon[0].erase("Name");
        anon[1].erase("Name");
        anon[1]["Radius"]["value"] = 9.;
        auto flipped = anonymous;
        std::swap(flipped.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0],
                  flipped.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][1]);
        check(!compare_collisions(anonymous, flipped).changes.empty(),
              "Unnamed shape indices remain order-sensitive");
        auto duplicates = ordered;
        duplicates.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][1]["Name"]["value"] =
            "Ball";
        auto duplicate_snapshot = duplicates.collision_snapshot();
        check(duplicate_snapshot["bodies"]["SM.Body"]["shapes"].contains("sphere:index:0") &&
                  duplicate_snapshot["bodies"]["SM.Body"]["shapes"].contains("sphere:index:1"),
              "Duplicate names cannot overwrite either shape");
        check(compare_collisions(package, package).status() == "identical",
              "Complete matching collision projection");
        check(compare_collisions(Package{}, Package{}).status() == "incomplete",
              "Absent collision data cannot establish equality");
        auto unusual = package;
        unusual.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Name"]["value"] =
            "A/B~C";
        auto unusual_after = unusual;
        unusual_after.exports[0]
            .properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Radius"]["value"] = 6.;
        auto escaped = compare_collisions(unusual, unusual_after);
        check(escaped.changes[0]["path"] == "/bodies/SM.Body/shapes/sphere:name:A~1B~0C/geometry/radius",
              "Collision diff paths escape names without ambiguity");
        bool diff_cancelled = false;
        try {
            compare_collisions(package, package, cancelled);
        } catch (const Error &e) {
            diff_cancelled = e.code == "cancelled";
        }
        check(diff_cancelled, "Collision comparison cooperates with cancellation");
        auto empty_levelsets = package;
        empty_levelsets.exports[0].properties["AggGeom"]["value"]["LevelSetElems"] =
            collision_property(Json::array(), "ArrayProperty");
        check(empty_levelsets.collision_snapshot()["complete"],
              "A known stored empty level-set collection needs no native element decoding");
        auto contained = package;
        ObjectExport static_mesh;
        static_mesh.path = static_mesh.name = "SM";
        static_mesh.class_name = "/Script/Engine.StaticMesh";
        contained.exports.push_back(static_mesh);
        contained.exports[0].outer_index = 2;
        check(contained.collision_snapshot()["owners"]["SM"]["containedBodies"] == Json::array({"SM.Body"}),
              "Export containment stays separate from unavailable native BodySetup references");
        auto long_owner = Package{};
        ObjectExport named_owner;
        named_owner.path = std::string(1000, 'x');
        named_owner.class_name = "/Script/Engine.StaticMesh";
        long_owner.exports.push_back(named_owner);
        CollisionOptions bounded_owner;
        bounded_owner.max_text_bytes = 100;
        rejects(long_owner, bounded_owner, "limit");
        auto many_references = long_owner;
        many_references.exports[0].path = "Mesh";
        many_references.exports[0].class_name = "/Script/Engine.PhysicsAsset";
        many_references.exports[0].properties = {
            {"SkeletalBodySetups",
             collision_property(Json::array({"A", "B", "C", "D", "E"}), "ArrayProperty")}};
        bounded_owner = CollisionOptions{};
        bounded_owner.max_bodies = 4;
        bounded_owner.max_nodes = 1;
        bool preflight = false;
        try {
            collision_snapshot(many_references, bounded_owner);
        } catch (const Error &e) {
            preflight = std::string(e.what()).find("reference budget") != std::string::npos;
        }
        check(preflight, "Owner reference budget is checked before copying a large property tree");
        check(package.to_json() == original, "Projection never mutates source package");
        check(!Package{}.collision_snapshot()["complete"],
              "Absent bodies cannot establish collision equality");
        std::cout << checks << " collision checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
