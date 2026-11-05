#include <cyuediff/binary_delta.hpp>
#include <iostream>
using namespace cyuediff;
int run(const std::vector<std::string> &args) {
    try {
        std::vector<std::uint8_t> base, target;
        if (args.empty()) {
            std::uint32_t state = 197;
            base.resize(256 * 1024);
            for (auto &b : base) {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                b = std::uint8_t(state);
            }
            target = base;
            target.insert(target.begin() + 123, {0, 255, 10, 20, 30});
            target.erase(target.begin() + 190000, target.begin() + 190013);
        } else if (args.size() == 2) {
            base = read_binary_file(fs::u8path(args[0]), 256ull * 1024 * 1024);
            target = read_binary_file(fs::u8path(args[1]), 256ull * 1024 * 1024);
        } else
            throw Error("arguments", "Expected optional BASE TARGET");
        const auto delta = make_binary_delta(base, target);
        const auto serialized = delta.to_json().dump();
        const auto parsed = BinaryDelta::parse(serialized);
        const auto rebuilt = apply_binary_delta(base, parsed);
        if (rebuilt != target)
            throw Error("verification", "Byte-exact reconstruction failed");
        std::cout << Json{{"summary", parsed.summary()},
                          {"manifestBytes", serialized.size()},
                          {"verified", true}}
                         .dump()
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
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
int main(int argc, char **argv) { return run({argv + 1, argv + argc}); }
#endif
