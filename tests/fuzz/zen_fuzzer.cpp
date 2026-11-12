// Coverage-guided fuzz target for cooked Zen package parsing (build with CYUEDIFF_BUILD_FUZZERS).
// Input: [u8 profile][Zen export-bundle bytes]. Bit 0 of the first byte selects UE 5.5, else 5.3.
// See docs/FUZZING.md. Malformed input may only fail with cyuediff::Error; snapshots must
// serialize; a package compared with itself must report no change.
#include <cyuediff/zen.hpp>
#include "../zen_fixture.hpp"
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <typeinfo>
using namespace cyuediff;
namespace {
[[noreturn]] void finding(const char *what) {
    std::fprintf(stderr, "FUZZ FINDING: %s\n", what);
    std::fflush(stderr);
    std::abort();
}
const std::shared_ptr<const ScriptObjects> &scripts() {
    static const auto value =
        std::make_shared<const ScriptObjects>(ScriptObjects::parse(test::script_fixture().data));
    return value;
}
// `--write-seeds <dir>` writes this target's seed corpus from the synthetic test fixtures and exits.
bool seed_request(int argc, char **argv, fs::path &out) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--write-seeds") {
            out = fs::u8path(argv[i + 1]);
            return true;
        }
    return false;
}
void save_seed(const fs::path &file, std::uint8_t prefix, const std::vector<std::uint8_t> &body) {
    std::ofstream out(file, std::ios::binary);
    out.put(static_cast<char>(prefix));
    out.write(reinterpret_cast<const char *>(body.data()), static_cast<std::streamsize>(body.size()));
}
} // namespace
extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
    fs::path out;
    if (!seed_request(*argc, *argv, out))
        return 0;
    fs::create_directories(out);
    int count = 0;
    for (const bool modern : {false, true})
        for (const bool versioned : {false, true})
            for (const bool reorder : {false, true}) {
                const auto name = std::string(modern ? "ue55" : "ue53") + (versioned ? "-versioned" : "") +
                                  (reorder ? "-reorder" : "") + ".bin";
                save_seed(out / name, modern ? 1 : 0, test::zen_fixture(modern, versioned, reorder).data);
                ++count;
            }
    std::printf("%d zen seeds written\n", count);
    std::exit(0);
}
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    if (!size)
        return 0;
    ZenReadOptions options;
    options.profile = (data[0] & 1) ? ZenProfile::unreal_5_5 : ZenProfile::unreal_5_3;
    options.script_objects = scripts();
    options.max_package_bytes = 4 * 1024 * 1024;
    options.max_header_bytes = 1024 * 1024;
    options.max_text_bytes = 4 * 1024 * 1024;
    options.max_entries = 20'000;
    options.max_property_values = 50'000;
    try {
        const std::vector<std::uint8_t> bytes(data + 1, data + size);
        const auto package = ZenPackage::parse(bytes, options);
        (void)package.snapshot().dump();
        (void)package.to_json().dump();
        const auto self = compare_zen_packages(package, package);
        if (!self.at("changes").empty())
            finding("a cooked package compared with itself reports changes");
        (void)self.dump();
    } catch (const Error &) {
        // Expected rejection of malformed input.
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FUZZ FINDING: unexpected %s: %s\n", typeid(e).name(), e.what());
        std::fflush(stderr);
        std::abort();
    }
    return 0;
}
