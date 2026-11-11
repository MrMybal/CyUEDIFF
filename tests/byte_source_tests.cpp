#include "package_fixture.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cyuediff;
using cyuediff::test::fixture;
using cyuediff::test::save;
namespace {
unsigned checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F &&f, const std::string &code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, ("Expected " + code + ", got " + e.code + ": " + e.what()).c_str());
        return;
    }
    throw std::runtime_error("Expected error " + code);
}
std::vector<std::uint8_t> bytes_of(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
} // namespace
int main() {
    const auto root = fs::current_path() / (".test-tmp-byte-source-" +
                                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        // Split package: header in .uasset, export payload in .uexp.
        const auto path = save(root, "split", fixture(1012, 7), true);
        auto uexp = path;
        uexp.replace_extension(".uexp");
        const auto from_file = Package::open(path);
        check(from_file.complete_input && from_file.inputs && from_file.inputs->companion(".uexp"),
              "File packages record the sources they were read from");

        PackageSources memory;
        memory.name = "Depot/Content/Split.uasset";
        memory.package = memory_source(bytes_of(path), "split.uasset");
        memory.companions[".uexp"] = memory_source(bytes_of(uexp), "split.uexp");
        const auto from_memory = Package::open(memory);
        check(from_memory.source == fs::path("Depot/Content/Split.uasset"),
              "Explicit sources report their logical asset path");
        check(from_memory.semantic_snapshot() == from_file.semantic_snapshot(),
              "Memory sources decode exactly like the same files");
        check(from_memory.fingerprints == from_file.fingerprints, "Fingerprints cover the supplied companions");
        check(compare(from_file, from_memory).status() == "identical", "File and memory revisions compare equal");

        // Explicit sources never fall back to siblings on disk, even when a file exists there.
        PackageSources header_only;
        header_only.name = path;
        header_only.package = memory.package;
        const auto missing = Package::open(header_only);
        check(!missing.complete_input && !missing.fingerprints.contains(".uexp"),
              "An absent companion stays absent instead of being read from disk");

        // Host callback (e.g. a VCS blob reader): reads are windowed, exceptions become io errors.
        const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(bytes_of(path));
        std::atomic<unsigned> calls{0};
        PackageSources callback = memory;
        callback.package = callback_source(
            bytes->size(),
            [&](std::uint64_t offset, std::uint8_t *destination, std::size_t count) {
                ++calls;
                std::copy_n(bytes->data() + offset, count, destination);
            },
            "callback.uasset");
        const auto from_callback = Package::open(callback);
        check(from_callback.semantic_snapshot() == from_file.semantic_snapshot(),
              "Callback sources decode like files");
        check(calls > 0 && calls < 8, "Primitive reads are served from the read window");
        PackageSources failing = memory;
        failing.package = callback_source(
            bytes->size(), [](std::uint64_t, std::uint8_t *, std::size_t) { throw std::runtime_error("offline"); });
        rejects([&] { Package::open(failing); }, "io");

        // Zero-copy views stay valid through their owner.
        PackageSources view = memory;
        view.package = memory_source(bytes, bytes->data(), bytes->size(), "view.uasset");
        check(Package::open(view).semantic_snapshot() == from_file.semantic_snapshot(),
              "Zero-copy memory views decode like files");

        // Contract validation.
        PackageSources invalid;
        rejects([&] { Package::open(invalid); }, "byte-source");
        invalid.package = memory.package;
        invalid.companions[".txt"] = memory.package;
        rejects([&] { Package::open(invalid); }, "byte-source");
        invalid.companions.clear();
        invalid.companions[".uexp"] = nullptr;
        rejects([&] { Package::open(invalid); }, "byte-source");
        std::uint8_t sink[4]{};
        rejects([&] { memory.package->read(memory.package->size() - 2, sink, 4); }, "bounds");
        rejects([] { file_source(fs::path("missing") / "absent.uasset"); }, "io");

        // A file source captures its size: a later rewrite is reported, not misread.
        const auto mutable_path = root / "mutable.bin";
        write(mutable_path, {1, 2, 3, 4});
        const auto file = file_source(mutable_path);
        file->read(1, sink, 2);
        check(sink[0] == 2 && sink[1] == 3, "File sources read at offsets");
        write(mutable_path, {1, 2, 3, 4, 5});
        rejects([&] { file->read(0, sink, 2); }, "source-changed");

        // Concurrent reads of one source.
        std::vector<std::thread> threads;
        std::atomic<bool> mismatch{false};
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&] {
                for (int i = 0; i < 20; ++i)
                    if (Package::open(memory).semantic_snapshot() != from_file.semantic_snapshot())
                        mismatch = true;
            });
        for (auto &thread : threads)
            thread.join();
        check(!mismatch, "Shared sources decode consistently from several threads");
    } catch (const std::exception &e) {
        std::cerr << "Byte source test failure: " << e.what() << '\n';
        std::cerr << "Preserved test workspace: " << root.u8string() << '\n';
        return 1;
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << checks << " byte source checks passed\n";
    return 0;
}
