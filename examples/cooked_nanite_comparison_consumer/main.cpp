#include <cyuediff/cooked_mesh.hpp>
#include <cyuediff/schema_context.hpp>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
fs::path path(const Json &row, const char *key) {
    auto p = fs::u8path(row.at(key).get<std::string>());
    if (!p.is_absolute() || !fs::is_regular_file(p))
        throw std::runtime_error("Use explicit absolute existing inputs");
    return p;
}
std::shared_ptr<const CookedNaniteResource> load(const Json &row, bool attributes,
                                                 const ContainerReadOptions &read,
                                                 std::vector<fs::path> &protected_files) {
    auto input = Container::open(path(row, "container")), global = Container::open(path(row, "global"));
    for (const auto *c : {&input, &global})
        protected_files.insert(protected_files.end(), c->files().begin(), c->files().end());
    auto manifest = ZenSchemaManifest::read(path(row, "context"));
    protected_files.push_back(manifest.source_path());
    protected_files.push_back(manifest.mapping_path());
    if (!row.at("mobileMinLod").is_boolean())
        throw std::runtime_error("Explicit producer mobile LOD boolean required");
    ZenReadOptions z;
    z.profile = ZenProfile::unreal_5_3;
    std::size_t selected = SIZE_MAX, scripts = SIZE_MAX;
    const auto id = row.at("entry").get<std::string>();
    for (std::size_t i = 0; i < input.entries().size(); ++i)
        if (input.entries()[i].id == id && !input.entries()[i].deleted) {
            if (selected != SIZE_MAX)
                throw std::runtime_error("Ambiguous selected member");
            selected = i;
        }
    for (std::size_t i = 0; i < global.entries().size(); ++i)
        if (global.entries()[i].chunk_type == 5 && !global.entries()[i].deleted) {
            if (scripts != SIZE_MAX)
                throw std::runtime_error("Ambiguous global table");
            scripts = i;
        }
    if (selected == SIZE_MAX || scripts == SIZE_MAX)
        throw std::runtime_error("Missing package or global script table");
    z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(global.read(scripts, read), z));
    SchemaReadOptions schema;
    schema.decoder = read.decoder;
    z.schema_context = manifest.load(schema);
    CookedMeshOptions o;
    o.profile = CookedMeshProfile::unreal_5_3_windows;
    o.keep_mobile_min_lod_setting = row.at("mobileMinLod").get<bool>();
    o.geometry = false;
    o.nanite.profile = NaniteProfile::unreal_5_3_windows;
    o.nanite.attributes = row.value("decodeAttributes", attributes);
    o.nanite.max_vertices = 200000;
    o.nanite.max_triangles = 200000;
    return read_zen_static_mesh(input, selected, z, read, o).nanite;
}
int run(const std::vector<fs::path> &args) {
    try {
        if (args.size() != 2 && args.size() != 3) {
            std::cerr << "Usage: cooked_nanite_comparison_consumer CONFIG.json OUTPUT.json [CODEC]\n";
            return 2;
        }
        const auto config_path = fs::absolute(args[0]), output = fs::absolute(args[1]);
        if (fs::file_size(config_path) > 2 * 1024 * 1024)
            throw std::runtime_error("Comparison configuration exceeds budget");
        std::ifstream input(config_path);
        Json config;
        input >> config;
        if (config.at("schema") != "cyuediff.cooked-nanite-comparison/1")
            throw std::runtime_error("Unsupported comparison configuration");
        const bool attributes = config.value("compareAttributes", true);
        std::vector<fs::path> protected_files{config_path};
        ContainerReadOptions read;
        if (args.size() == 3) {
            protected_files.push_back(fs::absolute(args[2]));
            read.decoder = container_block_decoder(load_block_decoder(fs::absolute(args[2])));
        }
        auto before = load(config.at("before"), attributes, read, protected_files),
             after = load(config.at("after"), attributes, read, protected_files);
        if (fs::exists(output))
            for (const auto &p : protected_files)
                if (fs::equivalent(output, p))
                    throw std::runtime_error("Output cannot replace a protected input");
        NaniteSurfaceDiffOptions options;
        options.attributes = attributes;
        auto result = compare_nanite_surfaces(*before, *after, options);
        auto out = result.to_json();
        out["beforeContext"] = before->information();
        out["afterContext"] = after->information();
        std::ofstream stream(output, std::ios::binary);
        if (!stream || !(stream << out.dump(2) << '\n'))
            throw std::runtime_error("Cannot write comparison");
        return !out.at("complete").get<bool>() ? 2 : out.at("status") == "identical" ? 0 : 1;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    return run(args);
}
#else
int main(int argc, char **argv) {
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(fs::u8path(argv[i]));
    return run(args);
}
#endif
