#include <cyuediff/container.hpp>
#include "package_fixture.hpp"
#include "container_fixtures.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void error(F f, const char *code) {
    try {
        f();
        throw std::runtime_error("Expected container failure");
    } catch (const Error &e) {
        if (e.code != code)
            throw std::runtime_error("Expected " + std::string(code) + ", got " + e.code + ": " + e.what());
        ++checks;
    }
}
void save(const fs::path &path, const std::vector<std::uint8_t> &data) {
    Bytes b;
    b.data = data;
    b.save(path);
}
void number(Bytes &b, std::uint64_t value, unsigned n, bool big = false) {
    for (unsigned j = 0; j < n; ++j)
        b.put<std::uint8_t>(static_cast<std::uint8_t>(value >> (8 * (big ? n - 1 - j : j))));
}
// Two chunks overlap shared blocks and the first chunk crosses a partition.
Bytes toc(const std::string &name = "A.bin", bool compressed = false, unsigned version = 5) {
    Bytes directory;
    directory.string("../../../");
    directory.i(2);
    for (auto v : {~0u, 1u, ~0u, ~0u, 0u, ~0u, ~0u, 0u})
        directory.put(v);
    directory.i(2);
    for (auto v : {1u, 1u, 0u, 2u, ~0u, 1u})
        directory.put(v);
    directory.i(3);
    directory.string("Content");
    directory.string(name);
    directory.string("B.bin");
    Bytes b;
    const std::string magic = "-==--==--==--==-";
    b.data.insert(b.data.end(), magic.begin(), magic.end());
    b.put<std::uint8_t>(static_cast<std::uint8_t>(version));
    b.zeros(3);
    for (auto v :
         {144u, 2u, 2u, 12u, compressed ? 1u : 0u, 32u, 4u, static_cast<unsigned>(directory.data.size()), 2u})
        b.put(v);
    b.zeros(24);
    b.put<std::uint8_t>(8);
    b.zeros(3);
    b.i(0);
    b.put<std::uint64_t>(4);
    b.zeros(48);
    b.zeros(11);
    b.put<std::uint8_t>(2);
    b.zeros(10);
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(2);
    number(b, 1, 5, true);
    number(b, 5, 5, true);
    number(b, 2, 5, true);
    number(b, 2, 5, true);
    for (unsigned offset : {0u, 4u}) {
        number(b, offset, 5);
        number(b, 4, 3);
        number(b, 4, 3);
        number(b, compressed ? 1 : 0, 1);
    }
    if (compressed) {
        std::string method = "Reverse";
        b.data.insert(b.data.end(), method.begin(), method.end());
        b.zeros(25);
    }
    b.append(directory);
    b.zeros(66);
    return b;
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
        auto directory = fs::u8path(argv[2]);
        fs::create_directories(directory);
        save(directory / "plain.pak", pak_plain());
        toc().save(directory / "test.utoc");
        save(directory / "test.ucas", {'a', 'b', 'c', 'd'});
        save(directory / "test_s1.ucas", {'e', 'f', 'g', 'h'});
        return 0;
    }
    auto root =
        fs::current_path() / (".test-tmp-containers-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        auto io = root / "test.utoc", pak = root / "test.pak";
        save(root / "test.ucas", {'a', 'b', 'c', 'd'});
        save(root / "test_s1.ucas", {'e', 'f', 'g', 'h'});
        toc().save(io);
        auto container = Container::open(io);
        check(container.entries().size() == 2, "IoStore indexed chunks");
        check(container.entries()[0].path == "Content/A.bin" &&
                  container.entries()[1].path == "Content/B.bin",
              "IoStore directory names");
        check(container.files().size() == 3, "Partition paths exposed to host");
        check(container.read(0) == std::vector<std::uint8_t>({'b', 'c', 'd', 'e', 'f'}),
              "Unaligned chunk crosses partitions");
        check(container.read(1) == std::vector<std::uint8_t>({'c', 'd'}), "Shared block subrange");
        check(container.to_json()["coverage"] == "container-bytes", "Honest format coverage");
        check(container.metadata()["entryCount"] == container.entries().size() &&
                  !container.metadata().contains("entries"),
              "Summary does not serialize entry rows");
        auto summary = container.metadata();
        summary.erase("entryCount");
        auto full = container.to_json();
        full.erase("entries");
        check(summary == full, "Summary preserves existing container metadata");
        check(compare_containers(container, container)["unchanged"] == 2, "Decoded-byte self comparison");
        auto other = root / "other.utoc";
        toc("Changed.bin").save(other);
        save(root / "other.ucas", {'a', 'b', 'X', 'd'});
        save(root / "other_s1.ucas", {'e', 'f', 'g', 'h'});
        auto difference = compare_containers(container, Container::open(other));
        check(difference["status"] == "changed" && difference["changes"].size() == 3,
              "Added, removed and modified decoded members");
        auto pairs = pair_container_entries(container, Container::open(other));
        check(pairs.size() == 3 && pairs[0].key == "path:Content/A.bin" && pairs[0].before &&
                  !pairs[0].after && pairs[1].before && pairs[1].after && !pairs[2].before && pairs[2].after,
              "Stable pairing retains one-sided members and exact path identity");
        ContainerReadOptions bounded;
        bounded.max_comparison_bytes = 13;
        bool payload_read = false;
        bounded.control.progress = [&](const auto &progress) {
            if (progress.phase == "container-read")
                payload_read = true;
        };
        error([&] { compare_containers(container, container, bounded); }, "container-limit");
        check(!payload_read, "Aggregate byte budget is checked before payload IO");
        bounded.max_comparison_bytes = 14;
        check(compare_containers(container, container, bounded)["unchanged"] == 2 && payload_read,
              "Aggregate byte budget includes both sides at the exact boundary");
        bounded.control.cancelled = [] { return true; };
        error([&] { pair_container_entries(container, container, bounded.control); }, "cancelled");
        for (unsigned version : {3u, 4u}) {
            toc("A.bin", false, version).save(io);
            check(Container::open(io).read(0) == container.read(0), "Older supported TOC layout");
        }
        toc().save(io);
        error([&] { container.read(2); }, "container-entry");
        ContainerOptions limits;
        limits.max_index_bytes = 100;
        error([&] { Container::open(io, limits); }, "container-limit");
        limits = {};
        limits.max_entries = 1;
        error([&] { Container::open(io, limits); }, "container-limit");
        limits = {};
        limits.max_depth = 0;
        error([&] { Container::open(io, limits); }, "container-limit");
        ContainerReadOptions ro;
        ro.max_payload_bytes = 4;
        error([&] { container.read(0, ro); }, "container-limit");
        ro = {};
        ro.max_block_bytes = 3;
        error([&] { container.read(0, ro); }, "container-limit");
        limits = {};
        limits.control.cancelled = [] { return true; };
        error([&] { Container::open(io, limits); }, "cancelled");
        ro = {};
        ro.control.cancelled = [] { return true; };
        error([&] { container.read(0, ro); }, "cancelled");
        bool cancelled = false;
        ro = {};
        ro.control.cancelled = [&] { return cancelled; };
        ro.control.progress = [&](const auto &) { cancelled = true; };
        error([&] { container.read(0, ro); }, "cancelled");
        toc("../evil").save(io);
        error([&] { Container::open(io); }, "container-format");
        // UTF-16 FString names, including a supplementary Unicode code point.
        auto wide = toc();
        const std::vector<std::uint8_t> ascii = {6, 0, 0, 0, 'A', '.', 'b', 'i', 'n', 0};
        auto name_at = std::search(wide.data.begin(), wide.data.end(), ascii.begin(), ascii.end());
        check(name_at != wide.data.end(), "Wide-name fixture location");
        auto location = static_cast<std::size_t>(name_at - wide.data.begin());
        Bytes utf16;
        utf16.i(-8);
        for (std::uint16_t cp : {0x41, 0xd83d, 0xdc99, 0x2e, 0x62, 0x69, 0x6e, 0})
            utf16.put(cp);
        wide.data.erase(wide.data.begin() + location, wide.data.begin() + location + ascii.size());
        wide.data.insert(wide.data.begin() + location, utf16.data.begin(), utf16.data.end());
        std::uint32_t directory_bytes = 0;
        std::memcpy(&directory_bytes, wide.data.data() + 48, 4);
        wide.patch(48, static_cast<int>(directory_bytes + utf16.data.size() - ascii.size()));
        wide.save(io);
        check(Container::open(io).entries()[0].path == "Content/A\xf0\x9f\x92\x99.bin",
              "UTF-16 surrogate pair becomes UTF-8");
        wide.data[location + 6] = 0;
        wide.data[location + 7] = 0xdc;
        wide.save(io);
        error([&] { Container::open(io); }, "container-format");
        // Signature bytes can be located, but are never described as authenticated.
        auto signed_toc = toc();
        signed_toc.data[80] |= 4;
        Bytes signature;
        signature.i(2);
        signature.zeros(44);
        signed_toc.data.insert(signed_toc.data.begin() + 212, signature.data.begin(), signature.data.end());
        signed_toc.save(io);
        check(Container::open(io).to_json()["diagnostics"][0].get<std::string>().find("not authenticated") !=
                  std::string::npos,
              "Explicit unauthenticated signature diagnostic");
        auto bad = toc();
        bad.data[16] = 8;
        bad.save(io);
        error([&] { Container::open(io); }, "container-version");
        bad = toc();
        bad.data[80] |= 2;
        bad.save(io);
        error([&] { Container::open(io); }, "container-encrypted");
        bad = toc();
        bad.patch(24, 0x7fffffff);
        bad.save(io);
        error([&] { Container::open(io); }, "container-limit");
        bad = toc();
        bad.data.resize(150);
        bad.save(io);
        error([&] { Container::open(io); }, "bounds");
        bad = toc(); // Duplicate chunk IDs.
        for (unsigned j = 0; j < 12; ++j)
            bad.data[156 + j] = bad.data[144 + j];
        bad.save(io);
        error([&] { Container::open(io); }, "container-format");
        bad = toc();
        auto dir_at = 144 + 24 + 20 + 24;
        bad.patch(dir_at + 22, 0); // root child points to itself
        bad.save(io);
        error([&] { Container::open(io); }, "container-format");
        toc().save(io);
        fs::rename(root / "test_s1.ucas", root / "missing.ucas");
        error([&] { Container::open(io); }, "container-partition");
        fs::rename(root / "missing.ucas", root / "test_s1.ucas");
        save(root / "test_s1.ucas", {'e'});
        error([&] { container.read(0); }, "container-changed");
        error([&] { Container::open(io); }, "bounds");
        save(root / "test_s1.ucas", {'e', 'f', 'g', 'h'});
        ContainerReadOptions reverse;
        reverse.decoder = [](const std::string &method, const std::uint8_t *src, std::size_t n,
                             std::uint8_t *dst, std::size_t size) {
            check(method == "Reverse" && n == size, "Named bounded codec");
            std::reverse_copy(src, src + n, dst);
            return size;
        };
        toc("A.bin", true).save(io);
        auto compressed = Container::open(io);
        error([&] { compressed.read(0); }, "container-codec");
        check(compressed.read(0, reverse) == std::vector<std::uint8_t>({'c', 'b', 'a', 'h', 'g'}),
              "Compressed IoStore block decoding");
        auto wrong = reverse;
        wrong.decoder = [](auto &&...) { return std::size_t(0); };
        error([&] { compressed.read(0, wrong); }, "container-codec");
        for (auto fixture : {pak_plain(), pak_v10()}) {
            save(pak, fixture);
            auto pc = Container::open(pak);
            check(pc.entries().size() == 3 && pc.entries()[2].deleted, "Pak entries and tombstone");
            check(pc.read(0) == std::vector<std::uint8_t>({'a', 'b', 'c', 'd', 'e', 'f'}),
                  "Compact Pak entry with SHA-1");
            check(pc.read(1) == pc.read(0), "Nonencoded Pak entry with SHA-1");
            error([&] { pc.read(2); }, "container-deleted");
            check(compare_containers(pc, pc)["unchanged"] == 2, "Deleted records omitted in byte diff");
        }
        save(pak, pak_wide());
        check(Container::open(pak).read(0) == std::vector<std::uint8_t>({'a', 'b', 'c', 'd', 'e', 'f'}),
              "64-bit compact entry fields");
        save(pak, pak_single());
        check(Container::open(pak).read(0, reverse) ==
                  std::vector<std::uint8_t>({'a', 'b', 'c', 'd', 'e', 'f'}),
              "Implicit single-block Pak range");
        save(pak, pak_unsafe());
        error([&] { Container::open(pak); }, "container-format");
        save(pak, pak_no_directory());
        error([&] { Container::open(pak); }, "container-index");
        save(pak, pak_compressed());
        auto pc = Container::open(pak);
        error([&] { pc.read(0); }, "container-codec");
        check(pc.read(0, reverse) == std::vector<std::uint8_t>({'d', 'a', 'b', 'e', 'f', 'c'}),
              "Pak multiblock decompression");
        auto bp = pak_plain();
        bp[53] ^= 1;
        save(pak, bp);
        pc = Container::open(pak);
        error([&] { pc.read(0); }, "container-integrity");
        bp = pak_plain();
        bp[bp.size() - 221 - 1] ^= 1;
        save(pak, bp);
        error([&] { Container::open(pak); }, "container-integrity");
        bp = pak_plain();
        bp[65] ^= 1;
        save(pak, bp);
        error([&] { Container::open(pak); }, "container-integrity");
        bp = pak_plain();
        bp[bp.size() - 221 + 16] = 1;
        save(pak, bp);
        error([&] { Container::open(pak); }, "container-encrypted");
        bp = pak_plain();
        bp.resize(100);
        save(pak, bp);
        error([&] { Container::open(pak); }, "container-format");
        unsigned mapped = 0;
        auto bridge = container_block_decoder([&](unsigned method, const auto *, auto, auto *, auto size) {
            mapped = method;
            return size;
        });
        std::uint8_t byte = 0;
        bridge("Oodle", &byte, 1, &byte, 1);
        check(mapped == 3, "Oodle codec ABI mapping");
        bridge("LZ4", &byte, 1, &byte, 1);
        check(mapped == 4, "LZ4 codec ABI mapping");
        error([&] { bridge("Zlib", &byte, 1, &byte, 1); }, "container-codec");
        fs::remove_all(root);
        std::cout << checks << " container checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "; fixtures: " << root << '\n';
        return 1;
    }
}
