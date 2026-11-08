#include <cyuediff/collision_wireframe.hpp>
#include "cooked_collision_fixture.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool b, const char *s) {
    ++checks;
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void refused(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        if (e.code != code)
            std::cerr << e.code << ": " << e.what() << '\n';
        check(e.code == code, "Expected cooked collision refusal");
        return;
    }
    throw std::runtime_error("Invalid cooked collision accepted");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            write_collision_fixtures(fs::u8path(argv[2]));
            return 0;
        }
        auto f = cooked_collision_fixture();
        auto options = collision_context();
        auto before = ZenPackage::parse(f.storage.package.data, options);
        const auto original = before.to_json();
        auto model = collision_snapshot(before);
        const auto body = "/Game/Texture.Texture";
        const auto &sphere = model["bodies"][body]["shapes"]["sphere:index:0"];
        check(model["complete"] && sphere["geometry"]["center"] == Json::array({1., 2., 3.}) &&
                  sphere["geometry"]["radius"] == 4.25,
              "Qualified mapped fields project stored sphere geometry");
        check(model["provenance"]["packageSha256"] == before.sha256 &&
                  model["provenance"]["scriptObjectsSha256"] == options.script_objects->sha256() &&
                  !model["provenance"]["nativePhysicsDecoded"].get<bool>(),
              "Interpretation provenance does not claim decoded native physics");
        check(model["bodies"][body]["native"]["bytes"] == 24 &&
                  model["bodies"][body]["native"]["coverage"] == "opaque-cooked-native",
              "Native remainder remains scoped opaque data");
        auto wire = collision_wireframe(before);
        check(wire.source_complete && wire.edge_count() == 96 && wire.bounds.valid,
              "Stored sphere wireframe is available");
        check(compare_collisions(before, before).status() == "identical",
              "Stable cooked collision comparison");
        auto after = ZenPackage::parse(cooked_collision_fixture(8.25f).storage.package.data, options);
        auto diff = compare_collisions(before, after);
        bool radius = false;
        for (const auto &c : diff.changes)
            radius = radius || (c["path"].get<std::string>().find("/geometry/radius") != std::string::npos &&
                                c["before"] == 4.25 && c["after"] == 8.25);
        check(diff.status() == "changed" && radius, "Named stored-radius difference retained");
        auto context = before;
        context.sha256 = "different-package";
        context.schema_context_sha256 = "different-context";
        check(compare_collisions(before, context).status() == "identical",
              "Context fingerprints remain separate from interpreted field changes");
        auto missing = before;
        missing.exports[0].property_read = nullptr;
        auto unknown = collision_snapshot(missing);
        check(!unknown["complete"].get<bool>() && unknown["bodies"][body]["shapes"].empty() &&
                  unknown["bodies"][body]["native"]["coverage"] == "opaque-cooked-body",
              "Unbound bodies do not fabricate geometry");
        check(compare_collisions(before, missing).status() == "incomplete",
              "Missing mapped context cannot certify an identical or complete comparison");
        auto template_body = before;
        template_body.exports[0].flags |= 0x10;
        check(collision_snapshot(template_body)["bodies"].empty(), "Template bodies are excluded");
        auto duplicate = before;
        auto &props = duplicate.exports[0].property_read["properties"];
        props["Other.AggGeom"] = props["BodySetup.AggGeom"];
        refused([&] { collision_snapshot(duplicate); }, "collision-identity");
        auto bad_boundary = before;
        bad_boundary.exports[0].property_read["consumedBytes"] = f.storage.package.data.size();
        refused([&] { collision_snapshot(bad_boundary); }, "collision-value");
        auto negative = ZenPackage::parse(cooked_collision_fixture(-1).storage.package.data, options);
        check(
            !collision_snapshot(negative)["bodies"][body]["shapes"]["sphere:index:0"]["complete"].get<bool>(),
            "Invalid sphere radius remains unresolved");
        for (int budget = 0; budget < 5; ++budget) {
            CollisionOptions o;
            if (budget == 0)
                o.max_nodes = 0;
            if (budget == 1)
                o.max_text_bytes = 0;
            if (budget == 2)
                o.max_depth = 0;
            if (budget == 3)
                o.max_shapes = 0;
            if (budget == 4)
                o.max_bodies = 0;
            refused([&] { collision_snapshot(before, o); }, "limit");
        }
        CollisionWireframeOptions bound;
        bound.max_edges = 0;
        refused([&] { collision_wireframe(before, bound); }, "collision-preview-limit");
        CollisionOptions cancelled;
        cancelled.control.cancelled = [] { return true; };
        refused([&] { collision_snapshot(before, cancelled); }, "cancelled");
        check(before.to_json() == original, "Read-only cooked projection preserves input data");
        std::cout << checks << " cooked collision checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
