#include <cyuediff/cooked_mesh.hpp>
#include <cyuediff/schema_context.hpp>
#include <iostream>
namespace {
int run(const std::vector<std::string> &args) {
    if (args.size() < 5 || args.size() > 6) {
        std::cerr << "Usage: cooked_mesh_consumer INPUT.utoc ENTRY GLOBAL.utoc SCHEMA.json "
                     "MOBILE_MIN_LOD_0_OR_1 [CODEC]\n";
        return 2;
    }
    try {
        using namespace cyuediff;
        CookedMeshOptions mesh;
        mesh.profile = CookedMeshProfile::unreal_5_3_windows;
        if (args[4] != "0" && args[4] != "1")
            throw Error("arguments", "Producer mobile min LOD setting must be 0 or 1");
        mesh.keep_mobile_min_lod_setting = args[4] == "1";
        ContainerReadOptions read;
        if (args.size() == 6)
            read.decoder = container_block_decoder(load_block_decoder(fs::u8path(args[5])));
        auto input = Container::open(fs::u8path(args[0]));
        auto global = Container::open(fs::u8path(args[2]));
        ZenReadOptions zen;
        zen.profile = ZenProfile::unreal_5_3;
        std::size_t script = SIZE_MAX, entry = SIZE_MAX;
        for (std::size_t i = 0; i < global.entries().size(); ++i)
            if (global.entries()[i].chunk_type == 5 && !global.entries()[i].deleted) {
                if (script != SIZE_MAX)
                    throw Error("arguments", "Ambiguous global script chunk");
                script = i;
            }
        for (std::size_t i = 0; i < input.entries().size(); ++i)
            if (input.entries()[i].id == args[1] && !input.entries()[i].deleted) {
                if (entry != SIZE_MAX)
                    throw Error("arguments", "Ambiguous package chunk");
                entry = i;
            }
        if (entry == SIZE_MAX || script == SIZE_MAX)
            throw Error("arguments", "Missing package or global script chunk");
        zen.script_objects =
            std::make_shared<ScriptObjects>(ScriptObjects::parse(global.read(script, read), zen));
        SchemaReadOptions mappings;
        mappings.decoder = read.decoder;
        zen.schema_context = ZenSchemaManifest::read(fs::u8path(args[3])).load(mappings);
        auto result = read_zen_static_mesh(input, entry, zen, read, mesh);
        std::cout << result.to_json(true).dump(2) << '\n';
        return 0;
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
    return run(args);
}
#else
int main(int argc, char **argv) {
    return run({argv + 1, argv + argc});
}
#endif
