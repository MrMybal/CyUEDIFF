#include <cyuediff/catalog.hpp>
#include <iostream>
namespace {
int run(const std::vector<std::string> &arguments) {
    if (arguments.size() < 1 || arguments.size() > 2) {
        std::cerr << "Usage: catalog_consumer PROJECT.uproject [ENGINE_ROOT]\n";
        return 2;
    }
    try {
        auto context = cyuediff::ProjectContext::open(
            cyuediff::fs::u8path(arguments[0]),
            arguments.size() == 2 ? cyuediff::fs::u8path(arguments[1]) : cyuediff::fs::path{});
        auto catalog = cyuediff::catalog_assets(context);
        std::cout << catalog.to_json().dump(2) << '\n';
        return catalog.complete ? 0 : 2;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.push_back(cyuediff::fs::path(argv[i]).u8string());
    return run(arguments);
}
#else
int main(int argc, char **argv) {
    return run({argv + 1, argv + argc});
}
#endif
