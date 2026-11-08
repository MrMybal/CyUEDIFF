#include <cyuediff/cooked_skeletal.hpp>
#include "cooked_morph_fixture.hpp"
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
template <class F> void refused(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, (std::string(e.code) + ": " + e.what()).c_str());
        return;
    }
    throw std::runtime_error("Invalid morph accepted");
}
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::temp_directory_path() /
        ("cyuediff-morph-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto z = morph_context();
        CookedSkeletalOptions o;
        o.profile = CookedSkeletalProfile::unreal_5_3_windows;
        o.keep_mobile_min_lod_setting = false;
        o.has_vertex_colors = false;
        std::vector<std::vector<MorphFixtureDelta>> targets = {
            {{0, {8, -3, 1}, {4, -2, 1}}, {2, {3, 1, 2}, {}}}, {}};
        auto changed = targets;
        changed[0][0].position[0] = 9;
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            for (const auto &name : {"morphs", "changed", "unbound", "absent", "streamed", "corrupt"}) {
                auto f = cooked_morph_fixture(std::string(name) == "changed" ? changed : targets,
                                              std::string(name) != "unbound", std::string(name) == "absent",
                                              false, false, false, std::string(name) == "streamed");
                texture_container(out / name, f, 2, std::string(name) == "corrupt");
            }
            auto scripts = morph_scripts();
            scripts.save(out / "global.ucas");
            zen_toc(scripts, 5).save(out / "global.utoc");
            morph_schema().save(out / "native.usmap");
            std::ofstream context(out / "context.json");
            context << Json{{"format", "cyuediff.zen-schema/2"},
                            {"profile", "ue5.3"},
                            {"usmap", "native.usmap"},
                            {"nativeStructs", Json::object()},
                            {"sha256", sha256_bytes(morph_schema().data)},
                            {"bindings",
                             {{"/Script/Engine.SkeletalMesh",
                               {{"schema", "/Script/Engine.SkeletalMesh"}, {"offset", 0u}}}}}}
                           .dump(2);
            fs::remove_all(root);
            return 0;
        }
        const auto read = [&](TextureFixture f, const CookedSkeletalOptions &options) {
            texture_container(root / "mesh", f);
            return read_zen_skeletal_mesh(Container::open(root / "mesh.utoc"), 0, z, {}, options, 0);
        };
        const auto a = read(cooked_morph_fixture(targets), o);
        const auto b = read(cooked_morph_fixture(changed), o);
        const auto &m = *a.lods[0].morphs;
        check(m.ready() && m.names() == std::vector<std::string>({"Morph/0", "Morph/1"}),
              "Names bind explicit reference order, not export order");
        check(m.buffer().delta_count(0) == 2 && m.buffer().delta_count(1) == 0,
              "Sparse and empty targets retained");
        check(a.lods[0].geometry->render_morphs == a.lods[0].morphs,
              "Core geometry shares immutable native words");
        auto difference = compare_meshes(*a.lods[0].geometry, *b.lods[0].geometry);
        check(difference.complete && difference.changes.size() == 1 &&
                  difference.changes[0]["path"].get<std::string>().find("Morph~10") != std::string::npos,
              "Single scalar position change with escaped stable morph name");
        check(compare_meshes(*a.lods[0].geometry, *a.lods[0].geometry).changes.empty(),
              "Identical native morphs compare identically");
        auto metadata = o;
        metadata.geometry = false;
        auto info = read(cooked_morph_fixture(targets), metadata);
        check(!info.lods[0].geometry && info.lods[0].morphs->buffer().delta(0, 1).vertex == 2,
              "Metadata reader retains queryable native model");
        const auto absent = read(cooked_morph_fixture(targets, true, true), o);
        check(!absent.lods[0].morphs &&
                  !compare_meshes(*a.lods[0].geometry, *absent.lods[0].geometry).changes.empty(),
              "Absent serialized buffer is a known absent stored-render scope");
        for (int mode = 0; mode < 3; ++mode) {
            auto unknown = read(cooked_morph_fixture(targets, mode != 0, false, mode == 1, mode == 2), o);
            check(!unknown.lods[0].morphs->ready() && unknown.lods[0].morphs->buffer().delta_count(0) == 2,
                  "Unbound, external outer and custom identities retain raw deltas");
            auto diff = compare_meshes(*unknown.lods[0].geometry, *a.lods[0].geometry);
            check(!diff.complete && diff.to_json()["status"] == "incomplete" && diff.changes.empty(),
                  "Unknown morph binding never becomes removals or identical");
        }
        refused([&] { read(cooked_morph_fixture(targets, true, false, false, false, true), o); },
                "cooked-skeletal-layout");
        const auto streamed = read(cooked_morph_fixture(targets, true, false, false, false, false, true), o);
        check(streamed.lods.size() == 2 && streamed.lods[0].morphs->ready() &&
                  streamed.lods[0].morphs->buffer().delta(0, 0).quantized_position == targets[0][0].position,
              "External streamed compression retains binding and exact delta");
        for (unsigned mode = 0; mode < 4; ++mode) {
            auto limit = o;
            if (mode == 0)
                limit.max_morph_targets = 1;
            if (mode == 1)
                limit.max_morph_batches = 0;
            if (mode == 2)
                limit.max_morph_deltas = 1;
            if (mode == 3)
                limit.max_output_bytes = 1;
            try {
                read(cooked_morph_fixture(targets), limit);
                throw std::runtime_error("Budget accepted");
            } catch (const Error &e) {
                check(e.code == "compressed-morph-limit" || e.code == "cooked-skeletal-limit",
                      "Aggregate morph/output budgets refused before projection");
            }
        }
        auto options = o;
        bool cancel = false;
        options.control.cancelled = [&] { return cancel; };
        options.control.progress = [&](const auto &p) {
            if (p.phase == "compressed-morph-deltas")
                cancel = true;
        };
        refused([&] { read(cooked_morph_fixture(targets), options); }, "cancelled");
        // Different packing precision with exactly equal decoded vectors is semantic equality.
        CompressedMorphOptions codec;
        codec.profile = MorphCompressionProfile::unreal_5_3_windows;
        auto packed = compressed_morph_fixture({{{0, {4, 0, 0}, {}}}}, .0625f);
        auto packed2 = compressed_morph_fixture({{{0, {8, 0, 0}, {}}}}, .03125f);
        auto p = std::make_shared<CompressedMorphBuffer>(CompressedMorphBuffer::parse(packed.data, 3, codec));
        auto q =
            std::make_shared<CompressedMorphBuffer>(CompressedMorphBuffer::parse(packed2.data, 3, codec));
        MeshRenderMorphs x(p, {"M"}), y(q, {"M"});
        check(x.semantic_snapshot() == y.semantic_snapshot(),
              "Packing changes do not manufacture decoded-value changes");
        auto repeated = compressed_morph_fixture({{{2, {4, 0, 0}, {}}, {2, {8, 0, 0}, {}}}});
        auto rep =
            std::make_shared<CompressedMorphBuffer>(CompressedMorphBuffer::parse(repeated.data, 3, codec));
        check(MeshRenderMorphs(rep, {"M"}).semantic_snapshot()["targets"]["M"]["vertices"]["2"].size() == 2,
              "Repeated vertex entries retain sequence/multiplicity without blending");
        refused([&] { MeshRenderMorphs(p, {}); }, "render-morphs");
        fs::remove_all(root);
        std::cout << checks << " cooked morph checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
