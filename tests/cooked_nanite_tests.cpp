#include <cyuediff/cooked_mesh.hpp>
#include "cooked_nanite_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool v, const char *m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void refused(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid whole Nanite input accepted");
}
} // namespace
int main(int argc, char **argv) {
    const auto root = fs::temp_directory_path() /
                      ("cyuediff-whole-nanite-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        const auto stem = root / "mesh";
        auto z = mesh_context();
        CookedMeshOptions options;
        options.profile = CookedMeshProfile::unreal_5_3_windows;
        options.keep_mobile_min_lod_setting = false;
        options.nanite.profile = NaniteProfile::unreal_5_3_windows;
        auto read = [&](const CookedNaniteFixture &f, const CookedMeshOptions &o, unsigned type = 2,
                        bool corrupt = false) {
            texture_container(stem, f.mesh.storage, type, corrupt);
            return read_zen_static_mesh(Container::open(fs::path(stem.string() + ".utoc")), 0, z, {}, o);
        };
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto target = fs::u8path(argv[2]);
            fs::create_directories(target);
            for (const auto *name : {"resident", "inline", "external", "missing", "empty", "unused"}) {
                const std::string n = name;
                auto f =
                    cooked_nanite_fixture(n != "resident" && n != "empty", n == "external" || n == "missing",
                                          n == "empty", n == "unused");
                if (n == "missing")
                    f.mesh.storage.bulk.data.clear();
                texture_container(target / n, f.mesh.storage);
            }
            auto altered = cooked_nanite_fixture();
            altered.mesh.storage.package.data[altered.root + 24 + 272] = 1;
            texture_container(target / "changed", altered.mesh.storage);
            collection_schema_fixture().save(target / "native.usmap");
            mesh_scripts().save(target / "global.ucas");
            zen_toc(mesh_scripts(), 5).save(target / "global.utoc");
            Json ctx = {
                {"format", "cyuediff.zen-schema/2"},
                {"profile", "ue5.3"},
                {"usmap", "native.usmap"},
                {"sha256", sha256_bytes(collection_schema_fixture().data)},
                {"bindings", {{"/Script/Engine.StaticMesh", {{"schema", "Collections"}, {"offset", 0}}}}},
                {"nativeStructs", Json::object()},
                {"reflectedStructs", Json::array()}};
            std::ofstream(target / "context.json") << ctx.dump(2);
            fs::remove_all(root);
            return 0;
        }
        for (bool streaming : {false, true})
            for (bool external : {false, true}) {
                if (external && !streaming)
                    continue;
                auto f = cooked_nanite_fixture(streaming, external);
                auto mesh = read(f, options);
                check(bool(mesh.nanite) && mesh.nanite->geometry_known(), "Whole native resource is bound");
                const auto n = mesh.nanite->catalog();
                check(n["status"] == "decoded" && n["geometry"]["clusters"] == (streaming ? 2 : 1),
                      "Resident/streamed cluster totals validated");
                check(mesh.tail_offset == f.end && mesh.tail_bytes == 1 && n["nativeOffset"] == f.native,
                      "Nanite native/end boundaries exact");
                check(mesh.nanite->geometry()->position(0, 0, 2) == std::array<float, 3>{-2, 0, 0},
                      "Whole-package position queries");
                check(mesh.nanite->hierarchy_node(0)["children"][0]["kind"] == "leaf",
                      "Focused typed hierarchy query");
                check(n["source"]["packageSha256"] == mesh.package_sha256 &&
                          !mesh.nanite->encoding_sha256().empty(),
                      "Native identity and producer context preserved");
                auto snapshot = n;
                auto changed = f;
                changed.mesh.storage.package.data[changed.root + 24 + 272] ^= 1;
                read(changed, options);
                check(mesh.nanite->catalog() == snapshot, "Immutable whole model survives source changes");
                if (streaming && external) {
                    auto limited = options;
                    limited.max_read_bytes =
                        f.mesh.storage.package.data.size() + f.mesh.storage.bulk.data.size() - 1;
                    refused([&] { read(f, limited); });
                    refused([&] { read(f, options, 2, true); });
                    for (unsigned type : {3u, 4u}) {
                        auto alternate = f;
                        alternate.mesh.storage.package.patch(int(alternate.mesh.storage.bulk_row + 24),
                                                             0x10501 | (type == 3 ? 1u << 11 : 1u << 12));
                        check(read(alternate, options, type).nanite->geometry_known(),
                              "Optional/mapped streaming storage supported");
                    }
                }
            }
        auto shared_fixture = cooked_nanite_shared_fixture();
        auto shared_options = options;
        shared_options.max_read_bytes =
            shared_fixture.mesh.storage.package.data.size() + shared_fixture.mesh.storage.bulk.data.size();
        auto shared = read(shared_fixture, shared_options);
        check(shared.lods[0].geometry && shared.nanite->geometry_known() &&
                  shared.nanite->geometry()->page_count() == 2,
              "LOD and Nanite windows share one verified chunk and aggregate read budget");
        --shared_options.max_read_bytes;
        refused([&] { read(shared_fixture, shared_options); });
        auto resident = cooked_nanite_fixture();
        auto known = read(resident, options);
        auto empty = read(cooked_nanite_fixture(false, false, true), options);
        check(empty.nanite->geometry_known() && !empty.nanite->geometry() &&
                  empty.nanite->catalog()["status"] == "empty",
              "Confirmed empty resource is known absence");
        auto missing = cooked_nanite_fixture(true, true);
        missing.mesh.storage.bulk.data.clear();
        auto absent = read(missing, options);
        check(!absent.nanite->geometry_known() && !absent.nanite->geometry() &&
                  absent.nanite->catalog()["pages"][1]["unavailable"] == "missing-chunk",
              "Missing stream is unavailable, not empty");
        auto unused = read(cooked_nanite_fixture(true, false, false, true), options);
        check(!unused.nanite->geometry_known() &&
                  unused.nanite->catalog()["pages"][1]["unavailable"] == "unused",
              "Unused stream cannot become decoded geometry");
        auto disabled = options;
        disabled.nanite = {};
        auto legacy = read(resident, disabled);
        check(!legacy.nanite && legacy.tail_offset == resident.native,
              "Default reader preserves opaque native tail");
        for (std::size_t n = resident.native; n < resident.end; ++n) {
            auto prefix = resident;
            prefix.mesh.storage.package.data.resize(n);
            nanite_export_size(prefix);
            refused([&] { read(prefix, options); });
        }
        for (auto offset : {resident.native + 2, resident.states + 4, resident.states + 8,
                            resident.states + 12, resident.hierarchy + 48, resident.roots, resident.facts,
                            resident.facts + 4, resident.facts + 8, resident.facts + 24}) {
            auto corrupt = resident;
            corrupt.mesh.storage.package.patch(int(offset), 0xffffffffu);
            refused([&] { read(corrupt, options); });
        }
        for (const auto change :
             std::vector<std::pair<std::size_t, std::uint32_t>>{{resident.hierarchy + 44, 0},
                                                                {resident.hierarchy + 48, 2},
                                                                {resident.root + 8, 1},
                                                                {resident.root + 12, 4},
                                                                {resident.root + 12, 64},
                                                                {resident.root + 16, 1},
                                                                {resident.root + 20, 9},
                                                                {resident.hierarchy, 0x7fc00000u}}) {
            auto corrupt = resident;
            corrupt.mesh.storage.package.patch(int(change.first), change.second);
            refused([&] { read(corrupt, options); });
        }
        auto cyclic_hierarchy = resident;
        cyclic_hierarchy.mesh.storage.package.patch(int(cyclic_hierarchy.hierarchy + 44), 0);
        cyclic_hierarchy.mesh.storage.package.patch(int(cyclic_hierarchy.hierarchy + 48), 0xffffffffu);
        refused([&] { read(cyclic_hierarchy, options); });
        auto cyclic_missing = missing;
        cyclic_missing.mesh.storage.package.patch(int(cyclic_missing.dependencies), 1);
        refused([&] { read(cyclic_missing, options); });
        auto inactive_nonfinite = resident;
        inactive_nonfinite.mesh.storage.package.patch(int(inactive_nonfinite.hierarchy + 52), 0x7fc00000u);
        check(read(inactive_nonfinite, options)
                      .nanite->hierarchy_node(0)["children"][1]["lodBounds"][0]["float32Bits"] == 0x7fc00000u,
              "Inactive nonfinite words retain raw identity without fabricated bounds");
        auto stripped_fixture = resident;
        stripped_fixture.mesh.storage.package.data.resize(stripped_fixture.native + 2);
        stripped_fixture.mesh.storage.package.data[stripped_fixture.native] = 3;
        stripped_fixture.mesh.storage.package.put<std::uint8_t>(0xca);
        nanite_export_size(stripped_fixture);
        auto stripped = read(stripped_fixture, options);
        check(stripped.nanite->information()["status"] == "stripped" && !stripped.nanite->geometry_known() &&
                  stripped.tail_bytes == 1,
              "Server stripping remains unknown and consumes only native strip flags");
        check(!compare_nanite_surfaces(*known.nanite, *stripped.nanite).summary["complete"].get<bool>(),
              "Stripped peer cannot prove surface removal");
        auto changed_surface = resident;
        changed_surface.mesh.storage.package.data[changed_surface.root + 24 + 272] = 1;
        auto changed_model = read(changed_surface, options);
        auto diff = compare_nanite_surfaces(*known.nanite, *changed_model.nanite);
        check(diff.summary["surfaceStatus"] == "changed" && diff.removed.size() == 1 &&
                  diff.added.size() == 1,
              "Moved geometry produces exact oriented surface differences");
        auto reencoded = resident;
        reencoded.mesh.storage.package.patch(int(reencoded.root + 24 + 96), 0xfffffffcu);
        reencoded.mesh.storage.package.patch(int(reencoded.root + 24 + 100), 0xfffffffcu);
        reencoded.mesh.storage.package.patch(int(reencoded.root + 24 + 108),
                                             2 | (9u << 4) | (3u << 9) | (3u << 14) | (1u << 24));
        reencoded.mesh.storage.package.data[reencoded.root + 24 + 273] = 4;
        reencoded.mesh.storage.package.data[reencoded.root + 24 + 274] = 32;
        reencoded.mesh.storage.package.patch(int(reencoded.facts + 4), 1);
        auto requantized = read(reencoded, options);
        diff = compare_nanite_surfaces(*known.nanite, *requantized.nanite);
        check(diff.summary["surfaceStatus"] == "identical" && diff.summary["encodingStatus"] == "changed" &&
                  diff.removed.empty(),
              "Different quantization can preserve the native-float surface");
        diff = compare_nanite_surfaces(*known.nanite, *empty.nanite);
        check(diff.removed.size() == 1 && diff.added.empty() && diff.summary["complete"] == true,
              "Verified empty peer proves removal");
        diff = compare_nanite_surfaces(*known.nanite, *absent.nanite);
        check(diff.summary["status"] == "incomplete" && diff.added.empty() && diff.removed.empty(),
              "Unavailable stream is not a removal");
        auto rotated = resident;
        rotated.mesh.storage.package.data[rotated.root + 24 + 272] = 2;
        rotated.mesh.storage.package.data[rotated.root + 24 + 273] = 8;
        rotated.mesh.storage.package.data[rotated.root + 24 + 274] = 0;
        auto rotation = read(rotated, options);
        check(compare_nanite_surfaces(*known.nanite, *rotation.nanite).summary["surfaceStatus"] ==
                  "identical",
              "Cyclic vertex order does not change oriented surface");
        std::swap(rotated.mesh.storage.package.data[rotated.root + 24 + 273],
                  rotated.mesh.storage.package.data[rotated.root + 24 + 274]);
        auto winding = read(rotated, options);
        check(compare_nanite_surfaces(*known.nanite, *winding.nanite).removed.size() == 1,
              "Reversed triangle winding changes surface");
        auto duplicate = read(cooked_nanite_fixture(true, true), options);
        diff = compare_nanite_surfaces(*known.nanite, *duplicate.nanite);
        check(diff.summary["matchedTriangles"] == 1 && diff.added.size() == 1,
              "Surface triangle multiplicity is retained");
        NaniteSurfaceDiffOptions bounded;
        bounded.max_output_bytes = 1;
        refused([&] { compare_nanite_surfaces(*known.nanite, *known.nanite, bounded); });
        bounded = {};
        bounded.max_triangles = 1;
        refused([&] { compare_nanite_surfaces(*known.nanite, *known.nanite, bounded); });
        bounded = {};
        bounded.control.cancelled = [] { return true; };
        refused([&] { compare_nanite_surfaces(*known.nanite, *known.nanite, bounded); });
        auto limited = options;
        limited.nanite.max_pages = 0;
        refused([&] { read(resident, limited); });
        limited = options;
        limited.nanite.max_hierarchy_nodes = 0;
        refused([&] { read(resident, limited); });
        limited = options;
        limited.nanite.max_output_bytes = 1;
        refused([&] { read(resident, limited); });
        limited = options;
        limited.nanite.max_read_bytes = 1;
        refused([&] { read(resident, limited); });
        limited = options;
        limited.nanite.control.cancelled = [] { return true; };
        refused([&] { read(resident, limited); });
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        refused([&] { known.nanite->catalog(cancel); });
        refused([&] { known.nanite->hierarchy_node(1); });
        const auto resolved = fs::weakly_canonical(root);
        check(resolved.parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
              "Cleanup remains in temporary fixture directory");
        fs::remove_all(resolved);
        std::cout << checks << " whole cooked Nanite checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << " fixtures " << root << '\n';
        return 1;
    }
}
