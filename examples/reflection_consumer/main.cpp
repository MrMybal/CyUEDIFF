#include <cyuediff/reflection.hpp>
#include <iostream>
namespace {
int run(const std::vector<std::string> &args) {
    try {
        if (args.size() != 1)
            throw std::runtime_error("Usage: reflection_consumer INPUT.uasset");
        const auto package = cyuediff::Package::open(cyuediff::fs::u8path(args[0]));
        std::cout << cyuediff::reflection_definitions(package).dump(2) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
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
