// Coverage-guided fuzz target for loose package reading (build with CYUEDIFF_BUILD_FUZZERS).
// Input layout: [u32 little-endian n][n package bytes][remaining bytes: .uexp companion].
// When n does not fit, the whole input is the package. See docs/FUZZING.md.
//
// Contract checked here: malformed input may only fail with cyuediff::Error. Any other
// exception, a sanitizer report, a timeout or an out-of-memory is a finding. A package
// compared with itself must report no change.
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
PackageSources sources_from(const std::uint8_t *data, std::size_t size) {
    PackageSources sources;
    sources.name = "Fuzz/Input.uasset";
    std::size_t offset = 0, package = size;
    if (size >= 4) {
        std::uint32_t n = 0;
        std::memcpy(&n, data, 4);
        if (n <= size - 4) {
            offset = 4;
            package = n;
        }
    }
    sources.package = memory_source(std::vector<std::uint8_t>(data + offset, data + offset + package));
    if (offset && offset + package < size)
        sources.companions[".uexp"] =
            memory_source(std::vector<std::uint8_t>(data + offset + package, data + size));
    return sources;
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    ReadOptions options;
    // Keep single inputs fast and bounded; the contract is that limits fail cleanly.
    options.max_properties = 20'000;
    options.max_table_entries = 20'000;
    options.max_string_bytes = 64 * 1024;
    options.max_property_bytes = 4 * 1024 * 1024;
    try {
        const auto package = Package::open(sources_from(data, size), options);
        (void)package.semantic_snapshot();
        const auto self = compare(package, package);
        if (!self.changes.empty())
            finding("a package compared with itself reports changes");
        // Reports must always serialize: hosts write them as JSON.
        std::ostringstream report;
        self.write_json(report, -1);
        (void)package.to_json().dump();
        TextureDecodeOptions texture;
        texture.max_pixels = 1 << 20;
        texture.max_payload_bytes = 8 * 1024 * 1024;
        texture.max_decoder_bytes = 32 * 1024 * 1024;
        for (const auto &source : package.textures)
            try {
                (void)decode_texture(source, 0, texture);
            } catch (const Error &) {
            }
        MeshDecodeOptions mesh;
        mesh.max_payload_bytes = 8 * 1024 * 1024;
        mesh.max_elements = 50'000;
        mesh.max_attribute_values = 400'000;
        for (const auto &source : package.mesh_sources)
            try {
                (void)decode_mesh(source, mesh);
            } catch (const Error &) {
            }
    } catch (const Error &) {
        // Expected rejection of malformed input.
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FUZZ FINDING: unexpected %s: %s\n", typeid(e).name(), e.what());
        std::fflush(stderr);
        std::abort();
    }
    return 0;
}
