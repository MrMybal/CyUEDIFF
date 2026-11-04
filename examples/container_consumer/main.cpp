#include <cyuediff/container.hpp>
#include <iostream>
namespace {
int run(const std::vector<std::string> &arguments) {
    if (arguments.size() != 1) {
        std::cerr << "Usage: container_consumer INPUT.pak|INPUT.utoc\n";
        return 2;
    }
    try {
        auto container = cyuediff::Container::open(cyuediff::fs::u8path(arguments[0]));
        auto output = container.to_json();
        if (!container.entries().empty() && !container.entries()[0].deleted) {
            auto data = container.read(0); // This example selects uncompressed inputs.
            output["firstMemberSha256"] = cyuediff::sha256_bytes(data);
        }
        std::cout << output.dump(2) << '\n';
        return 0;
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
