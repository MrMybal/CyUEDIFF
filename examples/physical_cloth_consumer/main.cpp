#include <cyuediff/physical_cloth.hpp>
#include <cyuediff/schema_context.hpp>
#include <cyuediff/cooked_skeletal.hpp>
#include <iostream>
namespace {
cyuediff::ZenPackage package(const std::vector<std::string> &args, std::size_t offset) {
    using namespace cyuediff;
    auto input = Container::open(fs::u8path(args.at(offset)));
    auto global = Container::open(fs::u8path(args.at(offset + 2)));
    const auto manifest = ZenSchemaManifest::read(fs::u8path(args.at(offset + 3)));
    ZenReadOptions options;
    options.profile = manifest.profile();
    options.schema_context = manifest.load();
    std::size_t script = SIZE_MAX, entry = SIZE_MAX;
    for (std::size_t i = 0; i < global.entries().size(); ++i)
        if (global.entries()[i].chunk_type == 5 && !global.entries()[i].deleted) {
            if (script != SIZE_MAX)
                throw Error("arguments", "Ambiguous global script chunk");
            script = i;
        }
    for (std::size_t i = 0; i < input.entries().size(); ++i)
        if (input.entries()[i].id == args.at(offset + 1) && !input.entries()[i].deleted) {
            if (entry != SIZE_MAX)
                throw Error("arguments", "Ambiguous package chunk");
            entry = i;
        }
    if (entry == SIZE_MAX || script == SIZE_MAX)
        throw Error("arguments", "Missing package or global script chunk");
    options.script_objects =
        std::make_shared<ScriptObjects>(ScriptObjects::parse(global.read(script), options));
    return read_zen_package(input, entry, options);
}

cyuediff::Json project(const cyuediff::PhysicalClothAsset &asset) {
    using namespace cyuediff;
    auto out = asset.to_json(true);
    for (std::uint32_t i = 0; i < asset.lods().size(); ++i)
        for (bool up : {true, false}) {
            const auto &t = up ? asset.lods()[i].up : asset.lods()[i].down;
            out["lods"][i][up ? "wrapUp" : "wrapDown"] =
                t.wrap_available ? Json(asset.wrap_transition(i, up)) : Json(nullptr);
        }
    return out;
}
cyuediff::Json binding(const std::vector<std::string> &args, std::size_t at,
                       const cyuediff::PhysicalClothAsset &asset, bool keep, bool colors) {
    using namespace cyuediff;
    auto input = Container::open(fs::u8path(args[at]));
    auto global = Container::open(fs::u8path(args[at + 2]));
    auto manifest = ZenSchemaManifest::read(fs::u8path(args[at + 3]));
    ZenReadOptions z;
    z.profile = manifest.profile();
    z.schema_context = manifest.load();
    std::size_t entry = SIZE_MAX, script = SIZE_MAX;
    for (std::size_t i = 0; i < input.entries().size(); ++i)
        if (input.entries()[i].id == args[at + 1])
            entry = i;
    for (std::size_t i = 0; i < global.entries().size(); ++i)
        if (global.entries()[i].chunk_type == 5)
            script = i;
    if (entry == SIZE_MAX || script == SIZE_MAX)
        throw Error("arguments", "Missing explicit render/package entries");
    z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(global.read(script), z));
    CookedSkeletalOptions o;
    o.profile = CookedSkeletalProfile::unreal_5_3_windows;
    o.keep_mobile_min_lod_setting = keep;
    o.has_vertex_colors = colors;
    const auto mesh = read_zen_skeletal_mesh(input, entry, z, {}, o);
    Json rows = Json::array();
    for (std::size_t i = 0; i < mesh.lods.size(); ++i)
        for (const auto &s : mesh.lods[i].sections)
            if (s.cloth && s.cloth->asset_lod >= 0 && s.cloth->has_render_mapping()) {
                auto result = verify_cloth_source_bounds(*s.cloth, 0, asset,
                                                         static_cast<std::uint32_t>(s.cloth->asset_lod));
                result["renderLOD"] = i;
                rows.push_back(std::move(result));
            }
    return rows;
}
int run(std::vector<std::string> args) {
    using namespace cyuediff;
    bool bounds = false, keep = false, colors = false;
    if (args.size() >= 3 && args[args.size() - 3] == "--render-facts") {
        if ((args[args.size() - 2] != "0" && args[args.size() - 2] != "1") ||
            (args.back() != "0" && args.back() != "1"))
            return 2;
        bounds = true;
        keep = args[args.size() - 2] == "1";
        colors = args.back() == "1";
        args.resize(args.size() - 3);
    }
    if (args.size() != 4 && args.size() != 8) {
        std::cerr << "Usage: physical_cloth_consumer INPUT.utoc ENTRY GLOBAL.utoc CONTEXT.json [AFTER.utoc "
                     "AFTER_ENTRY AFTER_GLOBAL.utoc AFTER_CONTEXT.json] [--render-facts KEEP_MOBILE_MIN_LOD "
                     "HAS_VERTEX_COLORS]\n";
        return 2;
    }
    try {
        PhysicalClothOptions o;
        o.profile = PhysicalClothProfile::unreal_5_3_windows;
        const auto before = read_physical_cloth(package(args, 0), o);
        Json output = {{"before", project(before)}};
        bool failed = !before.available();
        if (bounds)
            output["beforeSourceBounds"] = binding(args, 0, before, keep, colors);
        if (args.size() == 8) {
            const auto after = read_physical_cloth(package(args, 4), o);
            output["after"] = project(after);
            output["comparison"] = compare_physical_cloth(before, after);
            failed = failed || !after.available();
            if (bounds)
                output["afterSourceBounds"] = binding(args, 4, after, keep, colors);
        }
        std::cout << output.dump(2) << '\n';
        return failed ? 2 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(cyuediff::fs::path(argv[i]).u8string());
    return run(std::move(args));
}
#else
int main(int argc, char **argv) {
    return run({argv + 1, argv + argc});
}
#endif
