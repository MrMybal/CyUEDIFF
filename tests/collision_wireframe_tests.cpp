#include <cyuediff/collision_wireframe.hpp>
#include "collision_fixture.hpp"
#include <iostream>
#include <cmath>
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
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-8; };
        auto p = collision_fixture();
        const auto original = p.to_json();
        auto wire = collision_wireframe(p);
        check(wire.shapes.size() == 3 && wire.bounds.valid, "Three body-local primitives");
        for (const auto &shape : wire.shapes)
            check(shape.ready(), "Complete primitive is ready");
        check(wire.shapes[0].lines.size() == 12, "Box has twelve unique edges");
        const auto &box = wire.shapes[0];
        check(near(box.lines[0].a[0], 16) && near(box.lines[0].a[1], -8) && near(box.lines[0].a[2], -17),
              "Yaw rotates full box half-extents around stored center");
        check(wire.shapes[2].lines.size() == 96, "Sphere has three sampled great circles");
        auto &agg = p.exports[0].properties["AggGeom"]["value"];
        agg["SphereElems"]["value"][0].erase("Center");
        auto missing = collision_wireframe(p);
        check(!missing.shapes[2].ready() && !missing.shapes[2].diagnostics.empty(),
              "Missing center never defaults");
        CollisionWireframeOptions options;
        options.max_edges = 1;
        bool limit = false;
        try {
            collision_wireframe(p, options);
        } catch (const Error &e) {
            limit = e.code == "collision-preview-limit";
        }
        check(limit, "Edge exhaustion is explicit");
        options = {};
        options.circle_segments = 7;
        bool bad = false;
        try {
            collision_wireframe(p, options);
        } catch (const Error &e) {
            bad = e.code == "collision-preview-options";
        }
        check(bad, "Invalid tessellation is refused");
        options = {};
        options.collision.control.cancelled = [] { return true; };
        bool cancelled = false;
        try {
            collision_wireframe(p, options);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Cancellation propagates");
        auto rotation = collision_fixture();
        auto &boxfields = rotation.exports[0].properties["AggGeom"]["value"]["BoxElems"]["value"][0];
        boxfields["Rotation"]["value"] = {90., 0., 0.};
        auto pitched = collision_wireframe(rotation);
        check(near(pitched.shapes[0].lines[0].a[0], 21) && near(pitched.shapes[0].lines[0].a[2], -7),
              "Unreal positive pitch raises local X");
        auto tapered = collision_extended_fixture();
        auto &t = tapered.exports[0].properties["AggGeom"]["value"]["TaperedCapsuleElems"]["value"][0];
        t["Length"]["value"] = 0.;
        auto degenerate = collision_wireframe(tapered);
        for (const auto &shape : degenerate.shapes)
            if (shape.kind == "tapered-capsule")
                check(shape.lines.size() == 96, "Containing tapered end reduces to larger sphere");
        auto convex_package = collision_extended_fixture();
        auto &convex = convex_package.exports[0].properties["AggGeom"]["value"]["ConvexElems"]["value"][0];
        auto hull = [](const CollisionWireframe &w) -> const CollisionWireShape & {
            for (const auto &s : w.shapes)
                if (s.kind == "convex")
                    return s;
            throw std::runtime_error("Missing convex");
        };
        auto cw = collision_wireframe(convex_package);
        check(hull(cw).lines.size() == 6, "Tetrahedron has six deduplicated stored edges");
        check(hull(cw).bounds.low == CollisionPoint{2, 3, 4} &&
                  hull(cw).bounds.high == CollisionPoint{3, 4, 5},
              "Explicit convex transform applied");
        convex["Transform"]["value"]["rotation"] = {0., 0., std::sqrt(.5), std::sqrt(.5)};
        convex["Transform"]["value"]["scale"] = {2., -3., 4.};
        cw = collision_wireframe(convex_package);
        check(near(hull(cw).bounds.high[0], 5) && near(hull(cw).bounds.high[1], 5) &&
                  near(hull(cw).bounds.high[2], 8),
              "Signed scale precedes quaternion rotation and translation");
        convex["Transform"]["value"]["rotation"] = {0., 0., 0., 0.};
        auto invalid = collision_wireframe(convex_package);
        check(!hull(invalid).ready() && !hull(invalid).diagnostics.empty(),
              "Zero quaternion remains unavailable");
        convex.erase("Transform");
        auto absent = collision_wireframe(convex_package);
        check(!hull(absent).ready(), "Missing convex transform is never inferred");
        convex["Transform"] = collision_extended_fixture()
                                  .exports[0]
                                  .properties["AggGeom"]["value"]["ConvexElems"]["value"][0]["Transform"];
        convex.erase("IndexData");
        check(!hull(collision_wireframe(convex_package)).ready(), "No guessed convex hull");
        options = {};
        options.collision.control.cancelled = {};
        std::size_t visited = 0;
        options.collision.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "collision-wireframe")
                ++visited;
        };
        collision_wireframe(collision_extended_fixture(), options);
        check(visited == 6, "Progress reports each stored shape");
        auto zero = collision_fixture();
        auto &zeroagg = zero.exports[0].properties["AggGeom"]["value"];
        zeroagg["SphereElems"]["value"][0]["Radius"]["value"] = 0.;
        auto zero_wire = collision_wireframe(zero);
        check(zero_wire.shapes[2].bounds.valid, "Zero dimension remains an explicit degenerate projection");
        const auto analytic = collision_wireframe(collision_extended_fixture());
        for (const auto &shape : analytic.shapes) {
            if (shape.kind == "sphere")
                check(shape.bounds.low == CollisionPoint{-4, -3, -2} &&
                          shape.bounds.high == CollisionPoint{6, 7, 8},
                      "Analytic sphere bounds");
            if (shape.kind == "capsule")
                check(near(shape.bounds.low[2], -7) && near(shape.bounds.high[2], 13),
                      "Capsule segment length excludes caps");
            if (shape.kind == "tapered-capsule")
                check(near(shape.bounds.low[2], -6) && near(shape.bounds.high[2], 10),
                      "Tapered radius0 is the positive Z end");
        }
        options = {};
        options.max_edges = analytic.edge_count();
        check(collision_wireframe(collision_extended_fixture(), options).edge_count() == options.max_edges,
              "Exact edge budget is inclusive");
        --options.max_edges;
        bool exact_limit = false;
        try {
            collision_wireframe(collision_extended_fixture(), options);
        } catch (const Error &e) {
            exact_limit = e.code == "collision-preview-limit";
        }
        check(exact_limit, "One edge below required budget fails atomically");
        check(collision_wireframe(Package{}).to_json()["bounds"].is_null(), "Empty preview has no bounds");
        check(collision_fixture().to_json() == original, "Geometry preparation is read-only");
        std::cout << checks << " collision wireframe checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
