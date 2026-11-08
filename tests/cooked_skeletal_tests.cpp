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
            auto stream = cooked_skeletal_streaming_fixture();
            texture_container(target / "streamed", stream.storage);
            texture_container(target / "stream-inline", cooked_skeletal_streaming_fixture(false).storage);
            auto external_changed = stream;
            std::memcpy(external_changed.storage.bulk.data.data() + external_changed.positions + 12, &x, 4);
            texture_container(target / "external-changed", external_changed.storage);
            auto missing = stream;
            missing.storage.bulk.data.clear();
            texture_container(target / "missing", missing.storage);
            auto padded = stream;
            padded.storage.bulk.zeros(4);
            texture_container(target / "outside-corrupt", padded.storage, 2, true);
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
        auto read = [&](const SkeletalFixture &f, const CookedSkeletalOptions &opts, bool corrupt = false,
                        unsigned bulk_type = 2) {
            texture_container(stem, f.storage, bulk_type, corrupt);
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
        for (bool external : {false, true})
            for (unsigned flags = 0; flags < 128; ++flags) {
                const bool variable = flags & 1, wide_bone = flags & 2, wide_weight = flags & 4,
                           wide_indices = flags & 8, high = flags & 16, full = flags & 32,
                           colors = flags & 64;
                auto streamed = cooked_skeletal_streaming_fixture(external, variable, wide_bone, wide_weight,
                                                                  wide_indices, high, full, colors);
                auto opts = o;
                opts.has_vertex_colors = colors;
                auto out = read(streamed, opts);
                check(out.lods.size() == 2 && !out.lods[0].inlined && out.lods[1].inlined &&
                          out.lods[0].geometry,
                      "Mixed streamed/inline skeletal LODs");
                check(out.lods[0].geometry->to_json() == out.lods[1].geometry->to_json(),
                      "Storage-independent geometry and raw skin projection");
                check(out.lods[0].bulk["storage"] == (external ? "external" : "inline") &&
                          out.lods[0].bulk["availability"]["vertices"] == 3 &&
                          out.lods[0].bulk.contains("sha256"),
                      "Streamed LOD availability and verified payload provenance");
            }
        auto stream = cooked_skeletal_streaming_fixture();
        // Prefixes ending inside either the availability record or streamed/inline
        // native windows cannot publish a partially decoded mesh.
        for (std::size_t end = stream.native; end < stream.inline_count + 2; ++end) {
            auto partial = stream;
            std::uint32_t exp;
            std::memcpy(&exp, partial.storage.package.data.data() + 32, 4);
            const std::uint64_t shortened = end - partial.storage.body;
            std::memcpy(partial.storage.package.data.data() + exp + 8, &shortened, 8);
            bool rejected = false;
            try {
                read(partial, o);
            } catch (const Error &) {
                rejected = true;
            }
            check(rejected, "Truncated mixed skeletal native window rejected");
        }

        auto stream_bad = stream;
        stream_bad.storage.package.patch(static_cast<int>(stream_bad.availability + 1), 6);
        refused([&] { read(stream_bad, o); }, "cooked-skeletal-layout");
        stream_bad = stream;
        stream_bad.storage.package.patch(static_cast<int>(stream_bad.bulk_index), 1);
        refused([&] { read(stream_bad, o); }, "cooked-skeletal-layout");
        stream_bad = stream;
        stream_bad.storage.bulk.data[stream_bad.weights] = 2;
        refused([&] { read(stream_bad, o); }, "cooked-skeletal-layout");
        auto range_bad = stream;
        const std::int64_t huge_offset = INT64_MAX;
        std::memcpy(range_bad.storage.package.data.data() + range_bad.storage.bulk_row, &huge_offset, 8);
        refused([&] { read(range_bad, o); }, "cooked-skeletal-layout");
        range_bad = stream;
        range_bad.storage.package.patch(static_cast<int>(range_bad.flags), 72);
        refused([&] { read(range_bad, o); }, "cooked-skeletal-layout");
        range_bad = stream;
        range_bad.storage.package.patch(static_cast<int>(range_bad.flags), 0x10501 | (1u << 1));
        refused([&] { read(range_bad, o); }, "cooked-skeletal-profile");
        auto working = stream;
        auto cancelled = o;
        bool stop = false;
        cancelled.control.cancelled = [&] { return stop; };
        cancelled.control.progress = [&](const auto &p) {
            if (p.phase == "cooked-skeletal-weights")
                stop = true;
        };
        refused([&] { read(working, cancelled); }, "cancelled");
        auto opts_stream = o;
        opts_stream.max_read_bytes = stream.storage.package.data.size() + stream.storage.bulk.data.size() - 1;
        refused([&] { read(stream, opts_stream); }, "cooked-skeletal-limit");
        opts_stream = o;
        opts_stream.max_read_bytes = stream.storage.package.data.size() + stream.storage.bulk.data.size();
        check(read(stream, opts_stream).lods[0].geometry != nullptr,
              "Exact aggregate verified chunk read budget");
        // Both streamed LODs refer to the same bulk chunk; it is loaded once.
        auto shared = stream;
        auto &shared_bytes = shared.storage.package;
        Bytes header;
        header.data.assign(shared_bytes.data.begin() + shared.streaming - 6,
                           shared_bytes.data.begin() + shared.second_lod);
        shared_bytes.data.insert(shared_bytes.data.begin() + shared.second_lod, header.data.begin(),
                                 header.data.end());
        shared_bytes.patch(static_cast<int>(shared.streaming - 10), 3);
        shared_bytes.data[shared.inline_count + header.data.size() + 1] = 3;
        skeletal_export_size(shared);
        opts_stream.max_read_bytes = shared.storage.package.data.size() + shared.storage.bulk.data.size();
        check(read(shared, opts_stream).lods.size() == 3,
              "Shared external chunk is not charged or loaded twice");
        auto padded = stream;
        padded.storage.bulk.zeros(4);
        refused([&] { read(padded, o, true); }, "container-integrity");
        for (unsigned type : {2u, 3u, 4u}) {
            auto variant = stream;
            if (type == 3)
                variant.storage.package.patch(static_cast<int>(variant.flags), 0x10501 | (1u << 11));
            if (type == 4)
                variant.storage.package.patch(static_cast<int>(variant.flags), 0x10501 | (1u << 12));
            check(read(variant, o, false, type).lods[0].geometry != nullptr,
                  "Ordinary, optional and memory-mapped streamed chunks");
        }
        auto duplicate = stream;
        duplicate.storage.package.patch(static_cast<int>(duplicate.flags), 0x10501 | (1u << 14));
        const std::int64_t duplicate_offset = 3;
        std::memcpy(duplicate.storage.package.data.data() + duplicate.storage.bulk_row + 8, &duplicate_offset,
                    8);
        check(read(duplicate, o, false, 3).lods[0].bulk["chunkId"].get<std::string>().substr(22) == "03",
              "Duplicate optional chunk selected");
        check(read(duplicate, o).lods[0].bulk["chunkId"].get<std::string>().substr(22) == "02",
              "Duplicate optional chunk falls back to ordinary payload");
        auto missing = stream;
        missing.storage.bulk.data.clear();
        auto unavailable = read(missing, o);
        check(!unavailable.lods[0].geometry && unavailable.lods[0].unavailable == "missing-chunk" &&
                  unavailable.lods[1].geometry,
              "Missing streamed payload remains unavailable beside valid inline LOD");
        auto unused = cooked_skeletal_streaming_fixture(false);
        unused.storage.package.data.erase(unused.storage.package.data.begin() + unused.buffer_start,
                                          unused.storage.package.data.begin() + unused.availability);
        unused.storage.package.patch(static_cast<int>(unused.flags), 72 | 32);
        skeletal_export_size(unused);
        check(read(unused, o).lods[0].unavailable == "unused",
              "Unused nonzero bulk declaration consumes no inline bytes");
        auto empty = cooked_skeletal_streaming_fixture(false);
        empty.storage.package.data.erase(empty.storage.package.data.begin() + empty.buffer_start,
                                         empty.storage.package.data.begin() + empty.second_lod);
        const std::int64_t zero = 0;
        std::memcpy(empty.storage.package.data.data() + empty.storage.bulk_row + 16, &zero, 8);
        skeletal_export_size(empty);
        check(read(empty, o).lods[0].unavailable == "empty-payload",
              "Discarded streamed bulk has no availability block");
        auto empty_external = empty;
        const std::int64_t unknown_offset = -1;
        std::memcpy(empty_external.storage.package.data.data() + empty_external.storage.bulk_row,
                    &unknown_offset, 8);
        empty_external.storage.package.patch(static_cast<int>(empty_external.flags), 0x10501);
        const auto empty_result = read(empty_external, o);
        check(empty_result.lods[0].unavailable == "empty-payload" &&
                  empty_result.lods[0].bulk["offset"].is_null() && empty_result.lods[1].geometry,
              "Empty external declaration retains unknown offset without fabricating a range");

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
        refused([&] { read(bad, o); }, "cooked-skeletal-layout");
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
