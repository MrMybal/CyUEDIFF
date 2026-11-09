#include <cyuediff/schema_context.hpp>
#include <iostream>
namespace {
cyuediff::ZenPackage read(const std::vector<std::string> &args, std::size_t offset) {
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
int run(const std::vector<std::string> &args) {
    if (args.size() != 4 && args.size() != 8) {
        std::cerr << "Usage: hybrid_struct_consumer INPUT.utoc ENTRY GLOBAL.utoc CONTEXT.json "
                     "[AFTER.utoc AFTER_ENTRY AFTER_GLOBAL.utoc AFTER_CONTEXT.json]\n";
        return 2;
    }
    try {
        const auto before = read(args, 0);
        cyuediff::Json output = {{"before", before.to_json()}};
        bool failed = before.property_exports_failed != 0;
        if (args.size() == 8) {
            const auto after = read(args, 4);
            failed = failed || after.property_exports_failed != 0;
            output["after"] = after.to_json();
            output["comparison"] = cyuediff::compare_zen_packages(before, after);
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
    return run(args);
}
#else
int main(int argc, char **argv) {
    return run({argv + 1, argv + argc});
}
#endif
