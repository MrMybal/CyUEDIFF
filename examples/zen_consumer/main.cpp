#include <cyuediff/zen.hpp>
#include <iostream>
namespace {
int run(const std::vector<std::string> &args) {
    if (args.size() != 2) {
        std::cerr << "Usage: zen_consumer INPUT.utoc ue5.3|ue5.5\n";
        return 2;
    }
    try {
        cyuediff::ZenReadOptions options;
        if (args[1] == "ue5.3")
            options.profile = cyuediff::ZenProfile::unreal_5_3;
        else if (args[1] == "ue5.5")
            options.profile = cyuediff::ZenProfile::unreal_5_5;
        else
            throw cyuediff::Error("zen-profile", "Select ue5.3 or ue5.5");
        auto input = cyuediff::Container::open(cyuediff::fs::u8path(args[0]));
        cyuediff::ZenCatalogOptions catalog_options;
        catalog_options.include_snapshots = true;
        // This example uses copy blocks; production hosts supply codecs explicitly.
        auto catalog = cyuediff::catalog_zen_packages(input, options, {}, catalog_options);
        std::cout << catalog.dump(2) << '\n';
        return catalog.at("complete").get<bool>() ? 0 : 2;
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
