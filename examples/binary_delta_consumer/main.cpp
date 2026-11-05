#include <cyuediff/binary_delta.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
struct Fixture {
    fs::path root, file;
    ~Fixture() {
        std::error_code ignored;
        if (!file.empty())
            fs::remove(file, ignored);
        if (!root.empty())
            fs::remove(root, ignored);
    }
    void create(const std::vector<std::uint8_t> &bytes) {
        const auto candidate = fs::current_path() /
                               (".test-tmp-binary-sdk-" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(candidate))
            throw Error("fixture", "Cannot reserve fixture directory");
        root = candidate;
        file = root / "base.bin";
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out)
            throw Error("fixture", "Cannot write SDK fixture");
    }
};
} // namespace
int run(const std::vector<std::string> &args) {
    try {
        std::vector<std::uint8_t> base, target;
        Fixture fixture;
        fs::path base_path;
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
            fixture.create(base);
            base_path = fixture.file;
        } else if (args.size() == 2 || args.size() == 3) {
            base_path = fs::u8path(args[0]);
            base = read_binary_file(base_path, 256ull * 1024 * 1024);
            target = read_binary_file(fs::u8path(args[1]), 256ull * 1024 * 1024);
        } else
            throw Error("arguments", "Expected optional BASE TARGET [DELTA]");
        const auto delta =
            args.size() == 3 ? read_binary_delta(fs::u8path(args[2])) : make_binary_delta(base, target);
        const auto serialized = delta.to_json().dump();
        const auto parsed = BinaryDelta::parse(serialized);
        if (apply_binary_delta(base, parsed) != target)
            throw Error("verification", "JSON reconstruction failed");
        const auto compact = delta.to_binary();
        const auto packed = BinaryDelta::from_binary(compact);
        if (apply_binary_delta(base, packed) != target)
            throw Error("verification", "Compact reconstruction failed");
        std::vector<std::uint8_t> streamed;
        std::size_t maximum_chunk = 0;
        apply_binary_delta_file(base_path, packed, [&](const std::uint8_t *p, std::size_t n) {
            if (n > maximum_chunk)
                maximum_chunk = n;
            streamed.insert(streamed.end(), p, p + n);
        });
        if (streamed != target || maximum_chunk > 1024 * 1024)
            throw Error("verification", "Streaming reconstruction failed");
        auto final = target;
        if (args.empty())
            final.insert(final.begin() + 190, {255, 0, 71});
        const auto composed = compose_binary_deltas(packed, make_binary_delta(target, final));
        if (apply_binary_delta(base, BinaryDelta::from_binary(composed.to_binary())) != final)
            throw Error("verification", "Composed reconstruction failed");
        std::cout << Json{{"summary", parsed.summary()},
                          {"manifestBytes", serialized.size()},
                          {"compactBytes", compact.size()},
                          {"maximumStreamChunk", maximum_chunk},
                          {"verified", true},
                          {"compactVerified", true},
                          {"streamVerified", true},
                          {"composedVerified", true}}
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
int main(int argc, char **argv) {
    return run({argv + 1, argv + argc});
}
#endif
