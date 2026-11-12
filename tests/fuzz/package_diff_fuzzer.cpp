// Coverage-guided fuzz target for comparing two loose packages (build with CYUEDIFF_BUILD_FUZZERS).
// Input layout: [u32 little-endian n][n bytes: before package][remaining bytes: after package].
// See docs/FUZZING.md. Malformed input may only fail with cyuediff::Error; the report must
// serialize; a package compared with itself must report no change.
#include <cyuediff/core.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <typeinfo>
using namespace cyuediff;
namespace {
[[noreturn]] void finding(const char *what) {
    std::fprintf(stderr, "FUZZ FINDING: %s\n", what);
    std::fflush(stderr);
    std::abort();
}
Package open(const std::uint8_t *data, std::size_t size, const char *name) {
    PackageSources sources;
    sources.name = name;
    sources.package = memory_source(std::vector<std::uint8_t>(data, data + size));
    ReadOptions options;
    options.max_properties = 20'000;
    options.max_table_entries = 20'000;
    options.max_string_bytes = 64 * 1024;
    options.max_property_bytes = 4 * 1024 * 1024;
    return Package::open(sources, options);
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    if (size < 4)
        return 0;
    std::uint32_t n = 0;
    std::memcpy(&n, data, 4);
    if (n > size - 4)
        return 0;
    try {
        const auto before = open(data + 4, n, "Fuzz/Before.uasset");
        const auto after = open(data + 4 + n, size - 4 - n, "Fuzz/After.uasset");
        const auto result = compare(before, after);
        std::ostringstream report;
        result.write_json(report, -1);
        if (report.str().empty())
            finding("empty comparison report");
        (void)result.to_json().dump();
        if (!compare(after, after).changes.empty())
            finding("a package compared with itself reports changes");
    } catch (const Error &) {
        // Expected rejection of malformed input.
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FUZZ FINDING: unexpected %s: %s\n", typeid(e).name(), e.what());
        std::fflush(stderr);
        std::abort();
    }
    return 0;
}
