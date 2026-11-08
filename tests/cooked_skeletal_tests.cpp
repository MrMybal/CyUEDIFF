#include <cyuediff/cooked_skeletal.hpp>
#include "cooked_skeletal_fixture.hpp"
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
        check(e.code == code, "Expected skeletal refusal");
        return;
    }
    throw std::runtime_error("Invalid skeletal data accepted");
}
} // namespace
int main(int argc, char **argv) {
    const auto root = fs::temp_directory_path() /
                      ("cyuediff-cooked-skeletal-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        const auto stem = root / "mesh";
        auto context = skeletal_context();
        CookedSkeletalOptions o;
        o.profile = CookedSkeletalProfile::unreal_5_3_windows;
        o.keep_mobile_min_lod_setting = false;
        o.has_vertex_colors = false;
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto target = fs::u8path(argv[2]);
            fs::create_directories(target);
            auto f = cooked_skeletal_fixture();
            texture_container(target / "inline", f.storage);
            auto changed = f;
            float x = 2;
            std::memcpy(changed.storage.package.data.data() + changed.positions + 12, &x, 4);
            texture_container(target / "changed", changed.storage);
            texture_container(target / "corrupt", f.storage, 2, true);
            auto skin_changed = f;
            skin_changed.storage.package.data[skin_changed.weights + 6] = 129;
            skin_changed.storage.package.data[skin_changed.weights + 7] = 126;
            texture_container(target / "skin-changed", skin_changed.storage);
            auto scripts = skeletal_scripts();
            scripts.save(target / "global.ucas");
            zen_toc(scripts, 5).save(target / "global.utoc");
            collection_schema_fixture().save(target / "native.usmap");
            Json manifest = {
                {"format", "cyuediff.zen-schema/2"},
                {"profile", "ue5.3"},
                {"usmap", "native.usmap"},
                {"sha256", sha256_bytes(collection_schema_fixture().data)},
                {"bindings", {{"/Script/Engine.SkeletalMesh", {{"schema", "Collections"}, {"offset", 0}}}}},
                {"nativeStructs", Json::object()},
                {"reflectedStructs", Json::array()}};
            std::ofstream(target / "context.json") << manifest.dump(2);
            fs::remove_all(root);
            return 0;
        }
        auto read = [&](const SkeletalFixture &f, const CookedSkeletalOptions &opts, bool corrupt = false) {
            texture_container(stem, f.storage, 2, corrupt);
            return read_zen_skeletal_mesh(Container::open(fs::path(stem.string() + ".utoc")), 0, context, {},
                                          opts);
        };
        for (unsigned flags = 0; flags < 128; ++flags) {
            const bool variable = flags & 1, wide_bone = flags & 2, wide_weight = flags & 4,
                       wide_index = flags & 8, high = flags & 16, full = flags & 32, colors = flags & 64;
            auto f =
                cooked_skeletal_fixture(variable, wide_bone, wide_weight, wide_index, high, full, colors);
            auto opts = o;
            opts.has_vertex_colors = colors;
            auto out = read(f, opts);
            const auto &g = *out.lods[0].geometry;
            check(out.bones.size() == 2 && out.bones[1].parent == 0 && out.bones[1].translation[2] == 2,
                  "Reference hierarchy and double poses");
            check(g.vertices.size() == 3 && g.triangles.size() == 1 && g.instances[1].uvs[0][0] == 1,
                  "Cooked skeletal positions, topology and UVs");
            check(g.instances[0].normal[2] == 1 && g.instances[0].tangent[0] == 1 &&
                      g.instances[0].binormal_sign == 1,
                  "Packed skeletal tangent basis");
            check(g.instances[0].color[0] == (colors ? 56.f / 255.f : 1.f), "Optional BGRA skeletal colors");
            const auto &weights = g.skin->profiles.at("default");
            check(weights.at(0).size() == 1 && weights.at(0)[0].bone == 1 &&
                      weights.at(0)[0].raw_weight == 65535,
                  "Section bone map and zero padding semantics");
            check(weights.at(1).size() == 2 && weights.at(1)[0].raw_weight == 128 * 257 &&
                      weights.at(1)[1].bone == 0,
                  "Stored weights preserved without normalization");
            check(out.lods[0].buffers["variableInfluences"] == variable &&
                      out.lods[0].buffers["boneIndexBytes"] == (wide_bone ? 2 : 1) &&
                      out.lods[0].buffers["boneWeightBytes"] == (wide_weight ? 2 : 1),
                  "Skin metadata and variable lookup");
            check(out.native_offset == f.native && out.tail_offset == f.inline_count + 2 &&
                      out.tail_bytes == 1 && out.script_objects_sha256 == context.script_objects->sha256(),
                  "Exact native window and provenance");
            check(out.to_json(true)["lods"][0]["geometry"]["skin"]["bones"].size() == 2,
                  "Reusable cooked skin JSON");
            opts.geometry = false;
            check(!read(f, opts).lods[0].geometry, "Metadata mode still validates buffers");
        }
        const auto original_context = context;
        auto color_context = std::make_shared<ZenSchemaContext>(*context.schema_context);
        color_context->schemas =
            std::make_shared<UnversionedSchemas>(UnversionedSchemas::parse(skeletal_color_schema().data));
        color_context->bindings["/Script/Engine.SkeletalMesh"] = {"SkeletalMesh", 0};
        context.schema_context = color_context;
        for (bool colors : {false, true}) {
            auto stored = cooked_skeletal_fixture(false, false, false, false, false, false, colors);
            auto &bytes = stored.storage.package.data;
            bytes[stored.storage.body + 1] = 3;
            bytes.insert(bytes.begin() + stored.native, colors ? 1 : 0);
            std::uint32_t exp;
            std::memcpy(&exp, bytes.data() + 32, 4);
            const std::uint64_t size = bytes.size() - stored.storage.body;
            std::memcpy(bytes.data() + exp + 8, &size, 8);
            auto settings = o;
            settings.has_vertex_colors.reset();
            auto result = read(stored, settings);
            check(result.native_prefix["hasVertexColors"] == colors &&
                      result.native_prefix["vertexColorSource"] == "stored-property",
                  "Stored color property needs no fallback default");
            settings.has_vertex_colors = !colors;
            refused([&] { read(stored, settings); }, "cooked-skeletal-layout");
        }
        context = original_context;
        auto f = cooked_skeletal_fixture();
        auto bad = f;
        bad.storage.package.patch(static_cast<int>(bad.bone_parent), 1);
        refused([&] { read(bad, o); }, "mesh-skin-hierarchy");
        bad = f;
        bad.storage.package.patch(static_cast<int>(bad.map), 1);
        refused([&] { read(bad, o); }, "cooked-skeletal-layout");
        bad = f;
        bad.storage.package.patch(static_cast<int>(bad.pose),
                                  0x7ff00000); // Also corrupt high word explicitly below.
        bad.storage.package.patch(static_cast<int>(bad.pose + 4), 0x7ff00000);
        refused([&] { read(bad, o); }, "cooked-skeletal-layout");
        bad = f;
        bad.storage.package.data[bad.indices] = 3;
        refused([&] { read(bad, o); }, "cooked-skeletal-layout");
        bad = f;
        bad.storage.package.data[bad.weights] = 2;
        refused([&] { read(bad, o); }, "cooked-skeletal-layout");
        bad = f;
        bad.storage.package.patch(static_cast<int>(bad.positions), 0x7f800000);
        refused([&] { read(bad, o); }, "cooked-skeletal-layout");
        bad = f;
        bad.storage.package.patch(static_cast<int>(bad.size), 0);
        refused([&] { read(bad, o); }, "cooked-skeletal-layout");
        bad = f;
        bad.storage.package.patch(static_cast<int>(bad.profile), 1);
        refused([&] { read(bad, o); }, "cooked-skeletal-feature");
        bad = f;
        bad.storage.package.patch(static_cast<int>(bad.morph), 1);
        refused([&] { read(bad, o); }, "cooked-skeletal-feature");
        bad = f;
        bad.storage.package.patch(static_cast<int>(bad.attributes), 1);
        refused([&] { read(bad, o); }, "cooked-skeletal-feature");
        bad = f;
        bad.storage.package.patch(static_cast<int>(bad.streaming), 0);
        refused([&] { read(bad, o); }, "cooked-skeletal-feature");
        bad = f;
        bad.storage.package.data[bad.section + 1] = 0;
        refused([&] { read(bad, o); }, "cooked-skeletal-feature");
        bad = cooked_skeletal_fixture(true);
        bad.storage.package.patch(static_cast<int>(bad.lookup), 0xffffff01u);
        refused([&] { read(bad, o); }, "cooked-skeletal-layout");
        // Every native prefix/buffer truncation is rejected before a partial result escapes.
        for (std::size_t end = f.native; end < f.inline_count + 2; ++end) {
            bad = f;
            std::uint32_t exp;
            std::memcpy(&exp, bad.storage.package.data.data() + 32, 4);
            const std::uint64_t shortened = end - bad.storage.body;
            std::memcpy(bad.storage.package.data.data() + exp + 8, &shortened, 8);
            bool rejected = false;
            try {
                read(bad, o);
            } catch (const Error &) {
                rejected = true;
            }
            check(rejected, "Truncated skeletal native window rejected");
        }
        auto original = read(f, o);
        bad = f;
        bad.storage.package.data[bad.weights + 6] = 129;
        bad.storage.package.data[bad.weights + 7] = 126;
        auto altered = read(bad, o);
        auto difference = compare_meshes(*original.lods[0].geometry, *altered.lods[0].geometry);
        check(!difference.changes.empty(), "Changed cooked skin weights participate in common geometry diff");
        auto opts = o;
        opts.profile = CookedSkeletalProfile::unspecified;
        refused([&] { read(f, opts); }, "cooked-skeletal-profile");
        opts = o;
        opts.keep_mobile_min_lod_setting.reset();
        refused([&] { read(f, opts); }, "cooked-skeletal-profile");
        opts = o;
        opts.has_vertex_colors.reset();
        refused([&] { read(f, opts); }, "cooked-skeletal-context");
        for (unsigned which = 0; which < 9; ++which) {
            opts = o;
            switch (which) {
            case 0:
                opts.max_bones = 1;
                break;
            case 1:
                opts.max_vertices = 2;
                break;
            case 2:
                opts.max_indices = 2;
                break;
            case 3:
                opts.max_triangles = 0;
                break;
            case 4:
                opts.max_sections = 0;
                break;
            case 5:
                opts.max_output_bytes = 1;
                break;
            case 6:
                opts.max_read_bytes = f.storage.package.data.size() - 1;
                break;
            case 7:
                opts.max_influences = 1;
                break;
            case 8:
                opts.max_uv_channels = 0;
                break;
            }
            refused([&] { read(f, opts); }, "cooked-skeletal-limit");
        }
        opts = o;
        opts.control.cancelled = [] { return true; };
        refused([&] { read(f, opts); }, "cancelled");
        for (const auto phase : {"cooked-skeletal-vertices", "cooked-skeletal-weights"}) {
            opts = o;
            bool cancel = false;
            opts.control.cancelled = [&] { return cancel; };
            opts.control.progress = [&](const auto &p) {
                if (p.phase == phase)
                    cancel = true;
            };
            refused([&] { read(f, opts); }, "cancelled");
        }
        refused([&] { read(f, o, true); }, "container-integrity");
        opts = o;
        opts.keep_mobile_min_lod_setting = true;
        check(read(cooked_skeletal_fixture(false, false, false, false, false, false, false, true), opts)
                      .native_prefix["mobileMinLOD"] == 2,
              "Explicit producer mobile field");
        fs::remove_all(root);
        std::cout << checks << " cooked skeletal checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
