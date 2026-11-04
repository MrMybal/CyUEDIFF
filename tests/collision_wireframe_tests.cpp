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
        check(collision_wireframe(Package{}).to_json()["bounds"].is_null(), "Empty preview has no bounds");
        check(collision_fixture().to_json() == original, "Geometry preparation is read-only");
        std::cout << checks << " collision wireframe checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
