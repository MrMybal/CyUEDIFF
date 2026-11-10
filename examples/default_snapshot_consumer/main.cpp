#include <cyuediff/defaults.hpp>
#include <charconv>
#include <iostream>
using namespace cyuediff;
namespace {
std::int32_t number(const char *text) {
    const std::string value(text);
    std::int32_t result = -1;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result < 0)
        throw Error("argument", "Archive versions must be nonnegative int32 values");
    return result;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 5)
            throw Error(
                "argument",
                "Usage: default_snapshot_consumer capture.json exact-engine-version ue4 ue5 "
                "[--filtered] [--text-ue5-history] [--sha256 hash] [--native /qualified/type=layout]");
        DefaultSnapshotReadOptions options;
        options.expected_engine_version = argv[2];
        options.expected_ue4 = number(argv[3]);
        options.expected_ue5 = number(argv[4]);
        for (int i = 5; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--filtered")
                options.filter_editor_only = true;
            else if (argument == "--text-ue5-history")
                options.values.text_layout = TextLayout::ue5_history;
            else if (argument == "--sha256" && i + 1 < argc)
                options.expected_sha256 = argv[++i];
            else if (argument == "--native" && i + 1 < argc) {
                const std::string binding = argv[++i];
                const auto split = binding.find('=');
                if (split == std::string::npos ||
                    !options.values.native_structs
                         .emplace(binding.substr(0, split),
                                  parse_native_struct_layout(binding.substr(split + 1)))
                         .second)
                    throw Error("argument", "Expected a unique qualified schema=layout native binding");
            } else
                throw Error("argument", "Unknown or incomplete consumer option: " + argument);
        }
        const auto snapshot = DefaultSnapshot::read(fs::u8path(argv[1]), options);
        std::cout << snapshot.to_json().dump(2) << '\n';
        bool available = true;
        auto visit = [&](const Json &fields) {
            for (const auto &field : fields)
                available = available && field.at("available").get<bool>();
        };
        visit(snapshot.fields());
        for (const auto &structure : snapshot.structures()) {
            if (structure.at("available") == false)
                available = false;
            else
                visit(structure.at("fields"));
        }
        return available ? 0 : 1;
    } catch (const Error &e) {
        std::cerr << e.code << ": " << e.what() << '\n';
        return 2;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
