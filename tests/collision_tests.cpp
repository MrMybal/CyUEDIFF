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
