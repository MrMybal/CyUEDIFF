#include <cyuediff/cooked_mesh.hpp>
#include "cooked_mesh_fixture.hpp"
#include <chrono>
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
        check(e.code == code, "Expected cooked mesh refusal");
        return;
    }
    throw std::runtime_error("Invalid cooked mesh accepted");
}
} // namespace
int main(int argc, char **argv) {
    auto root = fs::temp_directory_path() /
                ("cyuediff-cooked-mesh-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        const auto stem = root / "mesh";
        auto context = mesh_context();
        CookedMeshOptions options;
        options.profile = CookedMeshProfile::unreal_5_3_windows;
        options.keep_mobile_min_lod_setting = false;
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto target = fs::u8path(argv[2]);
            fs::create_directories(target);
            for (bool external : {false, true})
                texture_container(target / (external ? "external" : "inline"),
                                  cooked_mesh_fixture(external, external).storage);
            auto changed = cooked_mesh_fixture(true, true);
            const float x = 2;
            std::memcpy(changed.storage.bulk.data.data() + changed.positions + 12, &x, 4);
            texture_container(target / "changed", changed.storage);
            texture_container(target / "corrupt", cooked_mesh_fixture(true, true).storage, 2, true);
            collection_schema_fixture().save(target / "native.usmap");
            auto scripts = mesh_scripts();
            scripts.save(target / "global.ucas");
            zen_toc(scripts, 5).save(target / "global.utoc");
            Json manifest = {
                {"format", "cyuediff.zen-schema/2"},
                {"profile", "ue5.3"},
                {"usmap", "native.usmap"},
                {"sha256", sha256_bytes(collection_schema_fixture().data)},
                {"bindings", {{"/Script/Engine.StaticMesh", {{"schema", "Collections"}, {"offset", 0}}}}},
                {"nativeStructs", Json::object()},
                {"reflectedStructs", Json::array()}};
            std::ofstream(target / "context.json") << manifest.dump(2);
            fs::remove_all(root);
            return 0;
        }
        auto read = [&](const MeshFixture &f, const CookedMeshOptions &o, unsigned type = 2,
                        bool corrupt = false) {
            texture_container(stem, f.storage, type, corrupt);
            return read_zen_static_mesh(Container::open(fs::path(stem.string() + ".utoc")), 0, context, {},
                                        o);
        };
        for (bool streaming : {false, true})
            for (bool external : {false, true}) {
                if (external && !streaming)
                    continue;
                for (bool wide : {false, true})
                    for (bool precision : {false, true})
                        for (bool colors : {false, true}) {
                            auto f =
                                cooked_mesh_fixture(streaming, external, wide, precision, precision, colors);
                            auto out = read(f, options);
                            check(out.lods.size() == 1 && out.lods[0].geometry,
                                  "Cooked mesh geometry available");
                            auto &g = *out.lods[0].geometry;
                            check(g.vertices.size() == 3 && g.triangles.size() == 1 &&
                                      g.instances[1].uvs[0][0] == 1,
                                  "Positions, triangles and UVs decoded");
                            check(g.instances[0].normal[2] == 1 && g.instances[0].tangent[0] == 1 &&
                                      g.instances[0].binormal_sign == 1,
                                  "Signed packed tangent basis decoded");
                            check(g.instances[0].color[0] == (colors ? 56.f / 255.f : 1.f) &&
                                      g.attributes["colorsStored"] == colors,
                                  "BGRA channels and unstored color provenance");
                            check(out.native_offset == f.storage.body + 2 &&
                                      out.tail_offset == f.inline_count + 1 && out.tail_bytes == 1,
                                  "Exact native prefix and opaque tail boundaries");
                            check(out.script_objects_sha256 == context.script_objects->sha256() &&
                                      out.export_index == 0 &&
                                      out.to_json()["scriptObjectsSha256"] == out.script_objects_sha256,
                                  "Native projection retains global table and selected export provenance");
                            check(out.to_json(true)["lods"][0]["geometry"]["triangles"].size() == 1,
                                  "Reusable geometry JSON");
                        }
                auto f = cooked_mesh_fixture(streaming, external);
                auto bad = f;
                auto &payload = external ? bad.storage.bulk : bad.storage.package;
                payload.patch(static_cast<int>(bad.stride), 16);
                refused([&] { read(bad, options); }, "cooked-mesh-layout");
                bad = f;
                auto &raw = external ? bad.storage.bulk : bad.storage.package;
                raw.data[bad.indices] = 3;
                refused([&] { read(bad, options); }, "cooked-mesh-layout");
                bad = f;
                bad.storage.package.patch(static_cast<int>(bad.section + 8), 2);
                refused([&] { read(bad, options); }, "cooked-mesh-layout");
                bad = f;
                auto &pos = external ? bad.storage.bulk : bad.storage.package;
                pos.patch(static_cast<int>(bad.positions), 0x7f800000);
                refused([&] { read(bad, options); }, "cooked-mesh-layout");
                bad = f;
                bad.storage.package.patch(static_cast<int>(bad.inline_count - 12), 0);
                refused([&] { read(bad, options); }, "cooked-mesh-layout");
                auto limited = options;
                limited.max_vertices = 2;
                refused([&] { read(f, limited); }, "cooked-mesh-limit");
                limited = options;
                limited.max_indices = 11;
                refused([&] { read(f, limited); }, "cooked-mesh-limit");
                limited = options;
                limited.max_sections = 0;
                refused([&] { read(f, limited); }, "cooked-mesh-limit");
                limited = options;
                limited.max_triangles = 0;
                refused([&] { read(f, limited); }, "cooked-mesh-limit");
                limited = options;
                limited.max_output_bytes = 1;
                refused([&] { read(f, limited); }, "cooked-mesh-limit");
                limited = options;
                limited.max_read_bytes =
                    f.storage.package.data.size() + (external ? f.storage.bulk.data.size() - 1 : 0);
                if (external)
                    refused([&] { read(f, limited); }, "cooked-mesh-limit");
                else
                    check(read(f, limited).lods.size() == 1, "Exact read budget");
                limited = options;
                limited.control.cancelled = [] { return true; };
                refused([&] { read(f, limited); }, "cancelled");
                limited = options;
                bool cancel = false;
                limited.control.cancelled = [&] { return cancel; };
                limited.control.progress = [&](const auto &p) {
                    if (p.phase == "cooked-mesh-vertices")
                        cancel = true;
                };
                refused([&] { read(f, limited); }, "cancelled");
                limited = options;
                limited.geometry = false;
                check(!read(f, limited).lods[0].geometry,
                      "Inventory validates buffers without publishing geometry");
                if (external) {
                    refused([&] { read(f, options, 2, true); }, "container-integrity");
                    bad = f;
                    bad.storage.bulk.data.clear();
                    check(read(bad, options).lods[0].unavailable == "missing-chunk",
                          "Missing streamed LOD explicit");
                    for (unsigned type : {3, 4}) {
                        bad = f;
                        bad.storage.package.patch(static_cast<int>(bad.flags), type == 3 ? 0x10d01 : 0x11501);
                        check(read(bad, options, type).lods[0].geometry != nullptr,
                              "Optional and mapped streams verified");
                    }
                }
            }
        for (bool external : {false, true}) {
            auto unused = cooked_mesh_fixture(true, external);
            auto &b = unused.storage.package;
            if (!external) {
                const auto from = unused.positions - 18;
                // Drop only the stored buffer window: the remaining availability metadata follows the bulk
                // index.
                const auto to = unused.inline_count - 12 - 80;
                b.data.erase(b.data.begin() + static_cast<std::ptrdiff_t>(from),
                             b.data.begin() + static_cast<std::ptrdiff_t>(to));
                std::uint32_t exp;
                std::memcpy(&exp, b.data.data() + 32, 4);
                const std::uint64_t size = b.data.size() - unused.storage.body;
                std::memcpy(b.data.data() + exp + 8, &size, 8);
            }
            b.patch(static_cast<int>(unused.flags), external ? 0x10521 : 104);
            const std::int64_t invalid = -1;
            std::memcpy(b.data.data() + unused.storage.bulk_row, &invalid, 8);
            unused.storage.bulk.data.clear();
            auto decoded = read(unused, options);
            check(decoded.lods[0].unavailable == "unused" && !decoded.lods[0].geometry &&
                      decoded.lods[0].bulk["offset"].is_null() &&
                      decoded.lods[0].bulk["declaredOffset"] == -1,
                  "Unused payload has no fabricated bytes or resolved offset");
        }
        auto f = cooked_mesh_fixture();
        auto absent = options;
        absent.keep_mobile_min_lod_setting.reset();
        refused([&] { read(f, absent); }, "cooked-mesh-profile");
        absent = options;
        absent.profile = CookedMeshProfile::unspecified;
        refused([&] { read(f, absent); }, "cooked-mesh-profile");
        auto mobile = cooked_mesh_fixture(false, false, false, false, false, false, true);
        auto chosen = options;
        chosen.keep_mobile_min_lod_setting = true;
        check(read(mobile, chosen).native_prefix["mobileMinLOD"] == 2,
              "Explicit mobile-LOD producer setting");
        for (std::size_t n = f.storage.body + 2; n <= f.inline_count; ++n) {
            auto cut = f;
            cut.storage.package.data.resize(n);
            std::uint32_t exp;
            std::memcpy(&exp, cut.storage.package.data.data() + 32, 4);
            const std::uint64_t size = n - cut.storage.body;
            std::memcpy(cut.storage.package.data.data() + exp + 8, &size, 8);
            try {
                read(cut, options);
                throw std::runtime_error("Truncated cooked mesh prefix accepted");
            } catch (const Error &e) {
                check(e.code == "cooked-mesh-layout" || e.code == "cooked-mesh-limit",
                      "Truncated native prefix refused");
            }
        }
        fs::remove_all(root);
        std::cout << checks << " cooked mesh checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
