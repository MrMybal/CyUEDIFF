#include <cyuediff/schema.hpp>
#include <fstream>
#include <iostream>
namespace {
int run(const std::vector<std::string> &args) {
    if (args.size() != 2) {
        std::cerr << "Usage: schema_consumer MAPPINGS.usmap VALUES.bin\n";
        return 2;
    }
    try {
        auto schemas = cyuediff::UnversionedSchemas::read(cyuediff::fs::u8path(args[0]));
        std::ifstream file(cyuediff::fs::u8path(args[1]), std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > 1024 * 1024)
            throw std::runtime_error("Invalid property input");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Cannot read properties");
        auto decoded = cyuediff::decode_unversioned(bytes, 0, bytes.size(), schemas, "Consumer");
        std::cout << decoded.properties.dump(2) << '\n';
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
