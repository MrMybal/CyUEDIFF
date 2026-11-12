// Coverage-guided fuzz target for Pak and IoStore container indexes and member reads
// (build with CYUEDIFF_BUILD_FUZZERS). Input: [u8 kind][body]. Kind bit 0 clear: the body is a
// .pak file. Set: the body is [u32 little-endian n][n bytes .utoc][remaining bytes .ucas].
// Containers are opened by path, so each input is written to a per-process temporary folder.
// See docs/FUZZING.md. Malformed input may only fail with cyuediff::Error; a container compared
// with itself must report identical members.
#include <cyuediff/container.hpp>
#include "../container_fixtures.hpp"
#include "../zen_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <typeinfo>
using namespace cyuediff;
namespace {
[[noreturn]] void finding(const char *what) {
    std::fprintf(stderr, "FUZZ FINDING: %s\n", what);
    std::fflush(stderr);
    std::abort();
}
const fs::path &folder() {
    static const fs::path value = [] {
        auto path = fs::temp_directory_path() / ("cyuediff-container-fuzz-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
        return path;
    }();
    return value;
}
void write(const fs::path &path, const std::uint8_t *data, std::size_t size) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
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
std::vector<std::uint8_t> iostore(const test::Bytes &payload, std::uint8_t type) {
    const auto toc = test::zen_toc(payload, type);
    std::vector<std::uint8_t> body(4);
    const auto n = static_cast<std::uint32_t>(toc.data.size());
    std::memcpy(body.data(), &n, 4);
    body.insert(body.end(), toc.data.begin(), toc.data.end());
    body.insert(body.end(), payload.data.begin(), payload.data.end());
    return body;
}
} // namespace
extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
    fs::path out;
    if (!seed_request(*argc, *argv, out))
        return 0;
    fs::create_directories(out);
    const std::pair<const char *, std::vector<std::uint8_t>> paks[] = {
        {"pak-plain", test::pak_plain()},   {"pak-compressed", test::pak_compressed()},
        {"pak-v10", test::pak_v10()},       {"pak-wide", test::pak_wide()},
        {"pak-single", test::pak_single()}, {"pak-unsafe", test::pak_unsafe()},
        {"pak-no-directory", test::pak_no_directory()}};
    int count = 0;
    for (const auto &pak : paks) {
        save_seed(out / (std::string(pak.first) + ".bin"), 0, pak.second);
        ++count;
    }
    save_seed(out / "iostore-package.bin", 1, iostore(test::zen_fixture(), 1));
    save_seed(out / "iostore-script-objects.bin", 1, iostore(test::script_fixture(), 5));
    count += 2;
    std::printf("%d container seeds written\n", count);
    std::exit(0);
}
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    if (!size)
        return 0;
    fs::path file;
    if (data[0] & 1) {
        if (size < 5)
            return 0;
        std::uint32_t n = 0;
        std::memcpy(&n, data + 1, 4);
        if (n > size - 5)
            return 0;
        file = folder() / "input.utoc";
        write(file, data + 5, n);
        write(folder() / "input.ucas", data + 5 + n, size - 5 - n);
    } else {
        file = folder() / "input.pak";
        write(file, data + 1, size - 1);
    }
    ContainerOptions options;
    options.max_index_bytes = 4 * 1024 * 1024;
    options.max_entries = 20'000;
    options.max_blocks = 50'000;
    ContainerReadOptions read;
    read.max_payload_bytes = 4 * 1024 * 1024;
    read.max_block_bytes = 1024 * 1024;
    read.max_comparison_bytes = 16 * 1024 * 1024;
    try {
        const auto container = Container::open(file, options);
        (void)container.metadata().dump();
        (void)container.to_json().dump();
        std::size_t readable = 0;
        for (std::size_t i = 0; i < container.entries().size() && i < 64; ++i)
            try {
                readable += container.read(i, read).size();
            } catch (const Error &) {
            }
        (void)readable;
        const auto self = compare_containers(container, container, read);
        (void)self.dump();
        if (self.contains("changes") && !self.at("changes").empty())
            finding("a container compared with itself reports changes");
    } catch (const Error &) {
        // Expected rejection of malformed input.
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FUZZ FINDING: unexpected %s: %s\n", typeid(e).name(), e.what());
        std::fflush(stderr);
        std::abort();
    }
    return 0;
}
