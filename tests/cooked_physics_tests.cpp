#include <cyuediff/cooked_physics.hpp>
#include "cooked_physics_fixture.hpp"
#include <iostream>
#include <chrono>
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
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Invalid physics accepted");
}
} // namespace
int main(int argc, char **argv) {
    auto root =
        fs::temp_directory_path() /
        ("cyuediff-physics-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        CookedPhysicsOptions o;
        o.profile = CookedPhysicsProfile::unreal_5_3_windows;
        if (argc == 3 && std::string(argv[1]) == "--decode-stream") {
            std::ifstream in(fs::u8path(argv[2]), std::ios::binary);
            std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(in), {}};
            std::cout << read_chaos_physics(b, o).dump() << '\n';
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto dir = fs::u8path(argv[2]);
            write_collision_fixtures(dir);
            texture_container(dir / "physics", physics_container_fixture().storage);
            texture_container(dir / "external", physics_container_fixture(true).storage);
            auto f = physics_container_fixture(true);
            f.storage.bulk.patch(3 + static_cast<int>(physics_fixture().vertex), 0x3f000000);
            texture_container(dir / "changed", f.storage);
            texture_container(dir / "corrupt", f.storage, 2, true);
            return 0;
        }
        fs::create_directories(root);
        for (unsigned width : {1u, 2u, 3u})
            for (bool wide : {false, true}) {
                const auto f = physics_fixture(width, wide);
                auto d = read_chaos_physics(f.blob.data, o);
                check(d["objects"].size() == 2 && d["convexRefs"] == Json({5, 5, nullptr}),
                      "Shared and null graph identity");
                const auto &c = d["objects"][0], &t = d["objects"][1];
                check(c["vertices"].size() == 4 && c["halfEdges"].size() == 12 &&
                          c["uniqueEdges"].size() == 6,
                      "Convex faces and edge topology");
                check(c["structureIndexType"] == width && c["rotationOfMass"][3] == 1 &&
                          c["volume"].get<double>() > 0,
                      "Topology precision and mass");
                check(t["triangles"][0] == Json({0, 1, 2}) && t["materials"][0] == 2 &&
                          t["indexBits"] == (wide ? 32 : 16),
                      "Triangle index widths and material");
                check(d["uvChannels"][0][1] == Json({1., 0.}) && d["faceRemap"][0] == 7,
                      "UV data and face remap");
            }
        auto f = physics_fixture();
        for (std::size_t cut = 0; cut < f.blob.data.size(); ++cut) {
            auto b = f.blob.data;
            b.resize(cut);
            refused([&] { read_chaos_physics(b, o); }, "cooked-physics-layout");
        }
        auto corrupt = [&](std::size_t at, std::uint32_t value) {
            auto b = f.blob;
            b.patch(static_cast<int>(at), static_cast<int>(value));
            refused([&] { read_chaos_physics(b.data, o); }, "cooked-physics-layout");
        };
        corrupt(0, 8);
        corrupt(4, 0xffffffffu);
        corrupt(8, 2);
        corrupt(12, 0xffffffffu);
        corrupt(f.vertex, 0x7fc00000);
        corrupt(f.alias, 9);
        corrupt(f.bvh, 1);
        corrupt(f.uv + 4, 0x7ff80000);
        auto b = f.blob;
        b.data[f.twin] = 0;
        refused([&] { read_chaos_physics(b.data, o); }, "cooked-physics-layout");
        b = f.blob;
        b.data[f.face] = 1;
        refused([&] { read_chaos_physics(b.data, o); }, "cooked-physics-layout");
        b = f.blob;
        b.data.push_back(0);
        refused([&] { read_chaos_physics(b.data, o); }, "cooked-physics-layout");
        refused([&] { read_chaos_physics(f.blob.data, {}); }, "cooked-physics-profile");
        for (unsigned limit_kind = 0; limit_kind < 8; ++limit_kind) {
            auto low = o;
            switch (limit_kind) {
            case 0:
                low.max_read_bytes = 1;
                break;
            case 1:
                low.max_output_bytes = 1;
                break;
            case 2:
                low.max_vertices = 6;
                break;
            case 3:
                low.max_objects = 1;
                break;
            case 4:
                low.max_references = 3;
                break;
            case 5:
                low.max_topology = 1;
                break;
            case 6:
                low.max_triangles = 0;
                break;
            case 7:
                low.max_bvh_nodes = 0;
                break;
            }
            refused([&] { read_chaos_physics(f.blob.data, low); }, "cooked-physics-limit");
        }
        auto cancelled = o;
        unsigned calls = 0;
        cancelled.control.cancelled = [&] { return ++calls > 3; };
        refused([&] { read_chaos_physics(f.blob.data, cancelled); }, "cancelled");
        auto z = collision_context();
        auto stem = root / "input";
        auto read = [&](const CookedCollisionFixture &fixture, unsigned type = 2, bool corrupt = false) {
            texture_container(stem, fixture.storage, type, corrupt);
            return read_zen_cooked_physics(Container::open(fs::path(stem.string() + ".utoc")), 0, z, {}, o);
        };
        for (bool external : {false, true})
            for (unsigned type : {2u, 3u, 4u}) {
                if (!external && type != 2)
                    continue;
                auto fixture = physics_container_fixture(external, external ? (type == 3   ? 0x801
                                                                               : type == 4 ? 0x1001
                                                                                           : 0x101)
                                                                            : 72);
                auto out = read(fixture, type);
                check(out["complete"] && out["bodies"].size() == 1, "Complete native body");
                auto &fmt = out["bodies"][0]["formats"][0];
                check(fmt["physics"]["objects"].size() == 2 && fmt["status"] == "decoded",
                      "Verified inline/external physics");
                check(fmt["storage"] == (external ? "external" : "inline") &&
                          fmt["chunkSourceHash"].is_string(),
                      "Storage provenance");
            }
        for (unsigned type : {2u, 3u}) {
            auto duplicate = physics_container_fixture(true, 0x4001);
            const std::int64_t duplicate_offset = 3;
            std::memcpy(duplicate.storage.package.data.data() + duplicate.storage.bulk_row + 8,
                        &duplicate_offset, 8);
            auto out = read(duplicate, type);
            check(out["complete"] && out["bodies"][0]["formats"][0]["chunkId"].get<std::string>().substr(
                                         22) == (type == 2 ? "02" : "03"),
                  "Duplicate optional storage selects verified optional or ordinary fallback");
        }
        auto unused = physics_container_fixture(false, 72 | 32);
        unused.storage.package.data.resize(unused.storage.package.data.size() -
                                           physics_fixture().blob.data.size());
        std::uint32_t export_at;
        std::memcpy(&export_at, unused.storage.package.data.data() + 32, 4);
        auto native_end = std::uint64_t(unused.storage.package.data.size() - unused.storage.body);
        std::memcpy(unused.storage.package.data.data() + export_at + 8, &native_end, 8);
        auto absent = read(unused);
        check(!absent["complete"].get<bool>() && absent["bodies"][0]["formats"][0]["status"] == "unused" &&
                  absent["bodies"][0]["formats"][0]["offset"].is_null(),
              "Unused nonempty bulk metadata consumes no inline bytes");
        auto bad_precision = f.blob;
        bad_precision.patch(0, 8);
        try {
            read_chaos_physics(bad_precision.data, o);
            throw std::runtime_error("Invalid precision accepted");
        } catch (const Error &error) {
            check(error.offset == 0, "Physics precision failure reports exact offset");
        }
        auto baseline = read(physics_container_fixture());
        auto review = cooked_physics_review(baseline, o);
        check(review.wire.edge_count() == 15 && review.wire.space == "cooker" &&
                  review.wire.shapes.size() == 4,
              "Native convex polygon edges and triangles retain shared/null references");
        const auto body_path = baseline["bodies"][0]["object"].get<std::string>();
        check(review.model["bodies"][body_path]["shapes"].size() == 4 &&
                  review.model["provenance"]["nativePhysicsDecoded"] == true &&
                  review.wire.to_json()["space"] == "cooker",
              "Native review provenance and coordinate scope");
        check(review.wire.shapes[0].collection == "chaos/PF_DXT1#0/convex" &&
                  review.wire.shapes[0].lines.size() == 6 && review.wire.shapes[3].lines.size() == 3,
              "Convex wire has no triangulation diagonals");
        refused([&] { cooked_physics_review(baseline, o, 14); }, "cooked-physics-limit");
        auto review_limit = o;
        review_limit.max_output_bytes = 1;
        refused([&] { cooked_physics_review(baseline, review_limit); }, "cooked-physics-limit");
        auto review_cancel = o;
        review_cancel.control.cancelled = [] { return true; };
        refused([&] { cooked_physics_review(baseline, review_cancel); }, "cancelled");
        auto invalid_review = baseline;
        invalid_review["bodies"][0]["formats"][0]["physics"]["objects"][1]["triangles"][0][0] = 99;
        refused([&] { cooked_physics_review(invalid_review, o); }, "cooked-physics-review");
        auto same = baseline;
        same["bodies"][0]["bodyGuidBytes"] = "different";
        auto &sp = same["bodies"][0]["formats"][0]["physics"];
        sp["sha256"] = "different";
        sp["objects"][0]["tag"] = 77;
        sp["convexRefs"] = {77, 77, nullptr};
        sp["objects"][0]["structureIndexType"] = 3;
        sp["objects"][0]["offset"] = 12345;
        check(compare_cooked_physics(baseline, same, o)["status"] == "unchanged",
              "Transport, tags and index widths do not change physics");
        same["bodies"][0]["formats"][0]["physics"]["objects"][0]["vertices"][1][0] = .5;
        auto diff = compare_cooked_physics(baseline, same, o);
        check(diff["status"] == "changed" && diff["changes"].size() == 1 &&
                  diff["changes"][0]["before"] == 1 && diff["changes"][0]["after"] == .5,
              "Exact native vertex diff");
        auto low = o;
        low.max_changes = 0;
        refused([&] { compare_cooked_physics(baseline, same, low); }, "cooked-physics-limit");
        auto missing = physics_container_fixture(true);
        missing.storage.bulk.data.clear();
        auto m = read(missing);
        check(!m["complete"].get<bool>() && m["bodies"][0]["formats"][0]["status"] == "missing-chunk",
              "Missing bulk is incomplete");
        auto missing_review = cooked_physics_review(m, o);
        check(missing_review.wire.shapes.empty() && !missing_review.model["complete"].get<bool>() &&
                  missing_review.model["bodies"][body_path]["unavailableFormats"] ==
                      Json({"chaos/PF_DXT1#0/"}),
              "Unavailable native formats retain unknown collection scope");
        auto unknown = compare_cooked_physics(baseline, m, o);
        check(unknown["status"] == "incomplete" && unknown["changes"].empty() &&
                  !unknown["unresolved"].empty(),
              "Missing native physics does not become removed geometry");
        refused([&] { read(physics_container_fixture(true), 2, true); }, "container-integrity");
        auto old = z;
        z.schema_context.reset();
        refused([&] { read(physics_container_fixture()); }, "cooked-physics-context");
        z = old;
        z.profile = ZenProfile::unreal_5_5;
        refused([&] { read(physics_container_fixture()); }, "cooked-physics-profile");
        z = old;
        fs::remove_all(root);
        std::cout << checks << " cooked physics checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Physics test failure after " << checks << ": " << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
