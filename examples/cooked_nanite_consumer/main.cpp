#include <cyuediff/cooked_mesh.hpp>
#include <cyuediff/schema_context.hpp>
#include <fstream>
#include <iostream>
using namespace cyuediff;
int run(const std::vector<std::string> &args) {
    try {
        if (args.size() < 6 || args.size() > 7) {
            std::cerr << "Usage: cooked_nanite_consumer INPUT.utoc ID GLOBAL.utoc CONTEXT.json MOBILE_0_OR_1 "
                         "OUTPUT.json [CODEC]\n";
            return 2;
        }
        if (args[4] != "0" && args[4] != "1")
            throw std::runtime_error("Select explicit producer mobile LOD setting");
        const auto target = fs::u8path(args[5]);
        auto input = Container::open(fs::u8path(args[0]));
        auto global = Container::open(fs::u8path(args[2]));
        const auto manifest = ZenSchemaManifest::read(fs::u8path(args[3]));
        auto protected_files = input.files();
        protected_files.insert(protected_files.end(), global.files().begin(), global.files().end());
        protected_files.push_back(manifest.source_path());
        protected_files.push_back(manifest.mapping_path());
        if (args.size() == 7)
            protected_files.push_back(fs::u8path(args[6]));
        if (fs::exists(target))
            for (const auto &p : protected_files)
                if (fs::equivalent(target, p))
                    throw std::runtime_error("Output cannot replace a protected input");
        ContainerReadOptions read;
        if (args.size() == 7)
            read.decoder = container_block_decoder(load_block_decoder(fs::u8path(args[6])));
        ZenReadOptions z;
        z.profile = ZenProfile::unreal_5_3;
        std::size_t entry = SIZE_MAX, script = SIZE_MAX;
        for (std::size_t i = 0; i < input.entries().size(); ++i)
            if (input.entries()[i].id == args[1] && !input.entries()[i].deleted) {
                if (entry != SIZE_MAX)
                    throw std::runtime_error("Ambiguous package");
                entry = i;
            }
        for (std::size_t i = 0; i < global.entries().size(); ++i)
            if (global.entries()[i].chunk_type == 5 && !global.entries()[i].deleted) {
                if (script != SIZE_MAX)
                    throw std::runtime_error("Ambiguous global table");
                script = i;
            }
        if (entry == SIZE_MAX || script == SIZE_MAX)
            throw std::runtime_error("Missing package/global member");
        z.script_objects =
            std::make_shared<ScriptObjects>(ScriptObjects::parse(global.read(script, read), z));
        SchemaReadOptions schema;
        schema.decoder = read.decoder;
        z.schema_context = manifest.load(schema);
        CookedMeshOptions o;
        o.profile = CookedMeshProfile::unreal_5_3_windows;
        o.keep_mobile_min_lod_setting = args[4] == "1";
        o.geometry = false;
        o.nanite.profile = NaniteProfile::unreal_5_3_windows;
        o.nanite.max_vertices = 200000;
        o.nanite.max_triangles = 200000;
        const auto result = read_zen_static_mesh(input, entry, z, read, o);
        const auto resource = result.nanite;
        const auto model = resource->geometry();
        Json out = {{"nanite", resource->catalog()}, {"hierarchy", Json::array()}, {"pages", Json::array()}};
        for (std::size_t ni = 0; ni < resource->hierarchy_node_count(); ++ni)
            out["hierarchy"].push_back(resource->hierarchy_node(ni));
        if (model)
            for (std::size_t pi = 0; pi < model->page_count(); ++pi) {
                Json page = Json::array();
                for (std::size_t ci = 0; ci < model->cluster_count(pi); ++ci) {
                    const auto metadata = model->cluster_metadata(pi, ci);
                    Json cluster = {{"metadata", metadata},
                                    {"positions", Json::array()},
                                    {"quantizedPositions", Json::array()},
                                    {"triangles", Json::array()}};
                    for (std::size_t vi = 0; vi < metadata.at("vertices").get<std::size_t>(); ++vi) {
                        cluster["positions"].push_back(model->position(pi, ci, vi));
                        cluster["quantizedPositions"].push_back(model->quantized_position(pi, ci, vi));
                    }
                    for (std::size_t ti = 0; ti < metadata.at("triangles").get<std::size_t>(); ++ti) {
                        const auto triangle = model->triangle(pi, ci, ti);
                        cluster["triangles"].push_back(
                            {{"vertices", triangle.vertices}, {"material", triangle.material}});
                    }
                    page.push_back(std::move(cluster));
                }
                out["pages"].push_back(std::move(page));
            }
        std::ofstream f(target, std::ios::binary);
        if (!f || !(f << out.dump(2) << '\n'))
            throw std::runtime_error("Cannot write result");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(fs::path(argv[i]).u8string());
    return run(args);
}
#else
int main(int argc, char **argv) {
    return run({argv + 1, argv + argc});
}
#endif
