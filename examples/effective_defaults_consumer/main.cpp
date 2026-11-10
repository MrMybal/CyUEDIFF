#include <cyuediff/defaults.hpp>
#include <iostream>
using namespace cyuediff;
namespace {
EffectiveDefaults inspect(const fs::path &path, const DefaultContext &context) {
    ReadOptions options;
    options.value_identities.names = true;
    options.value_identities.max_names = options.max_properties;
    options.value_identities.max_text_bytes = options.max_property_bytes;
    const auto package = Package::open(path, options);
    const ObjectExport *selected = nullptr;
    for (const auto &object : package.exports)
        if (object.outer_index == 0 &&
            object.class_name == context.snapshot().provenance().at("object").at("class")) {
            if (selected)
                throw Error("arguments", "Choose a package with one matching root export");
            selected = &object;
        }
    if (!selected)
        throw Error("default-context", "No root export matches the prepared class");
    return resolve_source_defaults(package, selected->path, context);
}
int run(const std::vector<fs::path> &args) {
    if (args.size() != 2 && args.size() != 3)
        throw Error("arguments", "Usage: effective_defaults_consumer CONTEXT ASSET [AFTER]");
    const auto context = DefaultContext::read(args[0]);
    const auto before = inspect(args[1], context);
    if (args.size() == 2) {
        std::cout << before.to_json().dump(2) << '\n';
        return before.complete ? 0 : 2;
    }
    const auto after = inspect(args[2], context);
    const auto diff = compare_effective_defaults(before, after);
    std::cout << diff.to_json().dump(2) << '\n';
    return diff.status() == "identical" ? 0 : diff.status() == "changed" ? 1 : 2;
}
int guarded(const std::vector<fs::path> &args) {
    try {
        return run(args);
    } catch (const Error &e) {
        std::cerr << Json({{"error", e.code}, {"message", e.what()}}).dump() << '\n';
        return 2;
    } catch (const std::exception &e) {
        std::cerr << Json({{"error", "consumer"}, {"message", e.what()}}).dump() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    return guarded(args);
}
#else
int main(int argc, char **argv) {
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(fs::u8path(argv[i]));
    return guarded(args);
}
#endif
