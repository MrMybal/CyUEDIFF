#include <cyuediff/cooked_skeletal.hpp>
#include "cooked_overlaps_fixture.hpp"
#ifdef CYUEDIFF_TEST_OVERLAPS_UI
#include <cyuediff/mesh_view.hpp>
#endif
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    unsigned checks = 0;
    const auto check = [&](bool v, const char *message) {
        ++checks;
        if (!v)
            throw std::runtime_error(message);
    };
    const auto root =
        fs::temp_directory_path() /
        ("cyuediff-overlaps-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto z = skeletal_context();
        CookedSkeletalOptions o;
        o.profile = CookedSkeletalProfile::unreal_5_3_windows;
        o.keep_mobile_min_lod_setting = false;
        o.has_vertex_colors = false;
        const auto read = [&](SkeletalFixture f, const CookedSkeletalOptions &options) {
            texture_container(root / "mesh", f.storage);
            return read_zen_skeletal_mesh(Container::open(root / "mesh.utoc"), 0, z, {}, options);
        };
        const auto refused = [&](auto callback, const std::string &code = std::string()) {
            try {
                callback();
            } catch (const Error &e) {
                check(code.empty() || code == e.code, e.what());
                return;
            }
            throw std::runtime_error("Invalid overlap accepted");
        };
        auto f = cooked_overlaps_fixture();
        const auto at = f.section_bones + 34;
        auto changed = f;
        changed.storage.package.patch(static_cast<int>(at + 4), 2);
        auto repacked = f;
        repacked.storage.package.patch(static_cast<int>(at + 4), 0);
        repacked.storage.package.patch(static_cast<int>(at + 8), 1);
        repacked.storage.package.patch(static_cast<int>(at + 20), 1);
        repacked.storage.package.patch(static_cast<int>(at + 28), 0);
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            for (const auto &pair : std::vector<std::pair<std::string, SkeletalFixture>>{
                     {"overlaps", f},
                     {"overlaps-changed", changed},
                     {"overlaps-repacked", repacked},
                     {"overlaps-empty", cooked_overlaps_fixture(true)},
                     {"overlaps-stripped", cooked_skeletal_fixture()}})
                texture_container(out / pair.first, pair.second.storage);
            fs::remove_all(root);
            return 0;
        }
        auto a = read(f, o), b = read(changed, o), packed = read(repacked, o);
        const auto &s = *a.lods[0].sections[0].overlaps;
        check(s.indices == std::vector<std::uint32_t>{1, 0} && s.windows.size() == 3, "Exact native arrays");
        check(s.neighbors(0) == std::vector<std::uint32_t>{1} &&
                  s.neighbors(1) == std::vector<std::uint32_t>{0} && s.neighbors(2).empty(),
              "Focused section-local to render correspondence");
        check(s.serialized_sha256.size() == 64 &&
                  a.lods[0].geometry->vertex_overlaps->sections[0] == a.lods[0].sections[0].overlaps,
              "Shared immutable section data/hash");
        check(a.to_json(true)["lods"][0]["sections"][0]["vertexOverlaps"]["windows"][1] == Json({1, 1}),
              "Raw native length/offset order");
        auto metadata = o;
        metadata.geometry = false;
        auto m = read(f, metadata);
        check(!m.lods[0].geometry && m.lods[0].sections[0].overlaps->neighbors(0) == s.neighbors(0),
              "Metadata-only typed query");
        auto diff = compare_meshes(*a.lods[0].geometry, *b.lods[0].geometry);
        check(diff.complete && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/vertexOverlaps/sections/0/vertices/0/0",
              "Exact resolved overlap difference");
        check(compare_meshes(*a.lods[0].geometry, *packed.lods[0].geometry).changes.empty() &&
                  s.serialized_sha256 != packed.lods[0].sections[0].overlaps->serialized_sha256,
              "Repacked buffers retain semantic identity/raw difference");
        auto empty = read(cooked_overlaps_fixture(true), o);
        auto stripped = read(cooked_skeletal_fixture(), o);
        auto unknown = compare_meshes(*a.lods[0].geometry, *stripped.lods[0].geometry);
        check(!unknown.complete && unknown.changes.empty() && !unknown.diagnostics.empty(),
              "Stripped is unknown rather than removed");
        auto removed = compare_meshes(*a.lods[0].geometry, *empty.lods[0].geometry);
        check(removed.complete && !removed.changes.empty(), "Known empty table proves removed relations");
        auto padding = cooked_overlaps_fixture(true);
        padding.storage.package.patch(static_cast<int>(padding.section_bones + 38), 777);
        check(compare_meshes(*empty.lods[0].geometry, *read(padding, o).lods[0].geometry).changes.empty(),
              "Unused allocation dummy has no semantic neighbors");
        auto ordered = s;
        ordered.indices = {1, 2};
        ordered.windows = {{2, 0}, {0, 2}, {0, 2}};
        auto permuted = ordered;
        std::reverse(permuted.indices.begin(), permuted.indices.end());
        check(ordered.semantic_snapshot() == permuted.semantic_snapshot(),
              "Neighbor order is excluded from relation identity");
        permuted.indices = {1, 1};
        check(ordered.semantic_snapshot() != permuted.semantic_snapshot(),
              "Neighbor multiplicity is preserved without inferred deduplication");
        MeshSectionOverlaps zero;
        zero.indices = {777};
        zero.windows = {{UINT32_MAX, UINT32_MAX}};
        zero.validate(0);
        check(zero.semantic_snapshot()["vertices"].empty(),
              "Zero-vertex native allocation dummy is not a resolved relation");
        auto shifted = s;
        shifted.first_vertex = 10;
        shifted.validate(13);
        check(shifted.neighbors(10) == std::vector<std::uint32_t>{11}, "Nonzero section base maps exactly");
        refused([&] { s.neighbors(3); }, "mesh-overlaps-selection");
        refused([&] { shifted.validate(12); }, "mesh-overlaps-layout");
        for (const auto &pair : std::vector<std::pair<unsigned, std::uint32_t>>{
                 {0, 0xffffffff}, {4, 3}, {12, 2}, {16, 3}, {20, 3}, {24, 0xffffffff}, {28, UINT32_MAX}}) {
            auto bad = f;
            bad.storage.package.patch(static_cast<int>(at + pair.first), pair.second);
            refused([&] { read(bad, o); });
        }
        auto budget = o;
        budget.max_duplicate_indices = 1;
        refused([&] { read(f, budget); }, "cooked-skeletal-limit");
        auto repeated = f;
        repeated.storage.package.patch(static_cast<int>(at + 24), 2);
        budget = o;
        budget.max_duplicate_indices = 2;
        refused([&] { read(repeated, budget); }, "cooked-skeletal-limit");
        auto expanded = s;
        expanded.windows[1] = {2, 0};
        refused([&] { expanded.validate(3, {}, 2); }, "mesh-overlaps-limit");
        auto cancel = o;
        cancel.control.cancelled = [] { return true; };
        refused([&] { read(f, cancel); }, "cancelled");
        OperationControl control;
        control.cancelled = [] { return true; };
        refused([&] { s.neighbors(0, control); }, "cancelled");
        for (std::size_t end = f.native; end < a.tail_offset; ++end) {
            auto truncated = f;
            truncated.storage.package.data.resize(end);
            skeletal_export_size(truncated);
            refused([&] { read(truncated, o); });
        }
#ifdef CYUEDIFF_TEST_OVERLAPS_UI
        MeshView wire;
        wire.set_mesh(a.lods[0].geometry, b.lods[0].geometry);
        check(wire.changes().modified == 2 && wire.changes().unchanged == 1,
              "Changed overlap vertex marks incident edges");
        wire.set_mesh(a.lods[0].geometry, packed.lods[0].geometry);
        check(wire.changes().modified == 0, "Repacked relation does not color mesh");
        wire.set_mesh(a.lods[0].geometry, stripped.lods[0].geometry);
        check(wire.changes().modified == 0, "Unknown peer table does not color false changes");
#endif
        fs::remove_all(root);
        std::cout << checks << " native overlap checks\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
