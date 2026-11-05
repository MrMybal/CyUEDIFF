#include <cyuediff/binary_delta.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
struct Fixture {
    fs::path root, file, target;
    ~Fixture() {
        std::error_code ignored;
        if (!target.empty())
            fs::remove(target, ignored);
        if (!file.empty())
            fs::remove(file, ignored);
        if (!root.empty())
            fs::remove(root, ignored);
    }
    void create(const std::vector<std::uint8_t> &base, const std::vector<std::uint8_t> &after) {
        const auto candidate = fs::current_path() /
                               (".test-tmp-binary-sdk-" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(candidate))
            throw Error("fixture", "Cannot reserve fixture directory");
        root = candidate;
        file = root / "base.bin";
        target = root / "target.bin";
        auto write = [](const fs::path &path, const std::vector<std::uint8_t> &bytes) {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            if (!out)
                throw Error("fixture", "Cannot write SDK fixture");
        };
        write(file, base);
        write(target, after);
    }
};
} // namespace
int run(const std::vector<std::string> &args) {
    try {
        std::vector<std::uint8_t> base, target;
        Fixture fixture;
        fs::path base_path, target_path;
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
            fixture.create(base, target);
            base_path = fixture.file;
            target_path = fixture.target;
        } else if (args.size() == 2 || args.size() == 3) {
            base_path = fs::u8path(args[0]);
            target_path = fs::u8path(args[1]);
            base = read_binary_file(base_path, 256ull * 1024 * 1024);
            target = read_binary_file(target_path, 256ull * 1024 * 1024);
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
            maximum_chunk = std::max(maximum_chunk, n);
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
        std::uint64_t source_reads = 0;
        std::size_t maximum_read = 0;
        auto source = [&](const std::vector<std::uint8_t> &v) {
            return BinaryDeltaSource{v.size(),
                                     [&, p = &v](std::uint64_t at, std::uint8_t *out, std::size_t n) {
                                         if (at > p->size() || n > p->size() - at)
                                             throw Error("verification", "Source range escaped");
                                         source_reads += n;
                                         maximum_read = std::max(maximum_read, n);
                                         std::memcpy(out, p->data() + static_cast<std::size_t>(at), n);
                                         return n;
                                     }};
        };
        const auto callback_plan = make_binary_delta_sources(source(base), source(target));
        const auto file_plan = make_binary_delta_files(base_path, target_path);
        if (callback_plan.to_binary() != file_plan.to_binary() ||
            apply_binary_delta(base, file_plan) != target || maximum_read > 1024 * 1024)
            throw Error("verification", "File/callback generation failed");
        std::vector<std::uint8_t> sink_bytes;
        std::size_t maximum_output = 0;
        delta.write_binary([&](const std::uint8_t *p, std::size_t n) {
            maximum_output = std::max(maximum_output, n);
            sink_bytes.insert(sink_bytes.end(), p, p + n);
        });
        if (sink_bytes != compact || delta.binary_size() != compact.size() || maximum_output > 1024 * 1024)
            throw Error("verification", "Compact sink encoding failed");
        std::cout << Json{{"summary", parsed.summary()},
                          {"manifestBytes", serialized.size()},
                          {"compactBytes", compact.size()},
                          {"maximumStreamChunk", maximum_chunk},
                          {"maximumSourceRead", maximum_read},
                          {"sourceReadBytes", source_reads},
                          {"maximumCompactChunk", maximum_output},
                          {"verified", true},
                          {"compactVerified", true},
                          {"streamVerified", true},
                          {"composedVerified", true},
                          {"sourceVerified", true},
                          {"fileGenerationVerified", true},
                          {"compactSinkVerified", true}}
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
