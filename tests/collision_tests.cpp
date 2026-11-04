#include "collision_fixture.hpp"
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
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
