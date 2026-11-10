#include <cyuediff/core.hpp>
#include <iostream>
namespace {
int run(const std::vector<cyuediff::fs::path> &args) {
    try {
        if (args.size() != 1 && args.size() != 2)
            throw cyuediff::Error("argument", "Usage: value_identity_consumer SOURCE [AFTER]");
        cyuediff::ReadOptions options;
        options.value_identities.names = true;
        const auto package = cyuediff::Package::open(args[0], options);
        if (args.size() == 2) {
            const auto after = cyuediff::Package::open(args[1], options);
            const auto diff = cyuediff::compare(package, after);
            diff.write_json(std::cout);
            std::cout << '\n';
            return diff.status() == "identical"                                     ? 0
                   : diff.status() == "unverified" || diff.status() == "incomplete" ? 2
                                                                                    : 1;
        }
        std::cout << cyuediff::Json{{"snapshot", package.semantic_snapshot()},
                                    {"valueIdentities", package.value_identities},
                                    {"nameValues", package.name_values}}
                         .dump(2)
                  << '\n';
        return package.name_values.at("status") == "complete" &&
                       package.value_identities.at("status") == "complete"
                   ? 0
                   : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<cyuediff::fs::path> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    return run(args);
}
#else
int main(int argc, char **argv) {
    std::vector<cyuediff::fs::path> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(cyuediff::fs::u8path(argv[i]));
    return run(args);
}
#endif
