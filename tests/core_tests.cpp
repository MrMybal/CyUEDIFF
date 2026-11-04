#include <cyuediff/core.hpp>
#include "package_fixture.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void error(F function, const std::string &code) {
    try {
        function();
        throw std::runtime_error("Expected error: " + code);
    } catch (const Error &e) {
        if (e.code != code)
            throw std::runtime_error("Expected " + code + ", got " + e.code + ": " + e.what());
        check(true, "error");
    }
}
} // namespace
int main() {
    auto root = fs::current_path() /
                (".test-tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        {
            std::ofstream(root / "abc", std::ios::binary) << "abc";
            std::ofstream(root / "empty", std::ios::binary);
        }
        check(sha256_file(root / "abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 abc");
        check(sha256_file(root / "empty") ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA256 empty");
        auto cancellable = save(root, "cancellable", fixture(1012));
        ReadOptions counted;
        counted.fingerprints = false;
        std::size_t checkpoints = 0;
        counted.control.cancelled = [&] {
            ++checkpoints;
            return false;
        };
        Package::open(cancellable, counted);
        for (std::size_t stop = 1; stop <= checkpoints; ++stop) {
            std::size_t calls = 0;
            ReadOptions cancelled;
            cancelled.fingerprints = false;
            cancelled.control.cancelled = [&] { return ++calls >= stop; };
            error([&] { Package::open(cancellable, cancelled); }, "cancelled");
        }
        OperationControl cancel_hash;
        cancel_hash.cancelled = [] { return true; };
        error([&] { sha256_file(root / "abc", cancel_hash); }, "cancelled");
        bool reported = false;
        ReadOptions progress;
        progress.control.progress = [&](const OperationProgress &p) {
            reported = reported || (p.phase == "Reading exports" && p.total == 1);
        };
        Package::open(cancellable, progress);
        check(reported, "Package reports export progress");
        for (int version : {0, 1009, 1011, 1012, 1016, 1018}) {
            auto file = save(root, "v" + std::to_string(version), fixture(version));
            auto p = Package::open(file);
            check(p.exports.size() == 1 && p.imports.size() == 1, "Tables");
            check(p.exports[0].coverage == "properties", "Property coverage");
            check(p.exports[0].properties.at("Value").at("value") == 1, "Integer value");
            check(p.exports[0].properties.at("Enabled").at("value") == true, "Boolean value");
            check(p.exports[0].properties.at("Text").at("value") == "hello", "String value");
            check(p.exports[0].properties.at("Items").at("value")[1].at("Value").at("value") == 8,
                  "Struct array element");
            check(compare(p, p).status() == "identical", "Self diff");
        }
        auto first = Package::open(save(root, "before", fixture(1012)));
        auto second = Package::open(save(root, "after", fixture(1012, 2)));
        auto diff = compare(first, second);
        check(diff.status() == "changed" && diff.changes.size() == 1, "One property change");
        check(diff.changes[0].at("path") == "/objects/TestAsset/properties/Value/value",
              "Stable property path");
        auto reordered = Package::open(save(root, "reordered", fixture(1012, 1, true)));
        check(compare(first, reordered).status() == "binary-only", "Name-table reorder is not semantic");
        auto opaque = Package::open(save(root, "opaque", fixture(1012, 1, false, 9)));
        check(compare(first, opaque).changes[0].at("path") == "/objects/TestAsset/opaque/suffix/sha256",
              "Native suffix change detected");
        auto split_path = save(root, "split", fixture(1012), true);
        auto split = Package::open(split_path);
        check(split.exports[0].properties == first.exports[0].properties, "Split export properties");
        auto companion = split_path;
        companion.replace_extension(".uexp");
        fs::remove(companion);
        auto missing = Package::open(split_path);
        check(!missing.complete_input && compare(missing, missing).status() == "incomplete",
              "Missing .uexp never identical");
        ReadOptions metadata;
        metadata.properties = metadata.fingerprints = false;
        check(compare(Package::open(save(root, "meta", fixture(1012)), metadata),
                      Package::open(save(root, "meta2", fixture(1012)), metadata))
                      .status() == "unverified",
              "Metadata cannot establish equality");
        auto cycle = fixture(1012);
        cycle.header.patch(cycle.export_offset + 12, 1);
        error([&] { Package::open(save(root, "cycle", cycle)); }, "reference-cycle");
        auto future = fixture(1018);
        future.header.patch(16, 2000);
        error([&] { Package::open(save(root, "future", future)); }, "version");
        auto negative = fixture(1012);
        negative.header.patch(24, -1);
        error([&] { Package::open(save(root, "negative", negative)); }, "limit");
        {
            std::ofstream(root / "pointer.uasset")
                << "version https://git-lfs.github.com/spec/v1\noid sha256:abc\nsize 1\n";
        }
        error([&] { Package::open(root / "pointer.uasset"); }, "lfs-pointer");
        for (std::size_t n : {0u, 3u, 8u, 24u, 80u}) {
            auto truncated = fixture(1012);
            truncated.header.data.resize(n);
            truncated.header.save(root / "truncated.uasset");
            error([&] { Package::open(root / "truncated.uasset"); }, n >= 32 ? "header" : "bounds");
        }
        fs::create_directories(root / "Content");
        fs::copy_file(save(root, "mounted", fixture(1012)), root / "Content" / "Asset.uasset");
        {
            std::ofstream(root / "Test.uproject") << R"({"EngineAssociation":"5.4"})";
        }
        auto context = ProjectContext::open(root / "Test.uproject");
        check(fs::equivalent(context.resolve("/Game/Asset"), root / "Content" / "Asset.uasset"),
              "Mount resolution");
        check(context.package_name(root / "Content" / "Asset.uasset") == "/Game/Asset",
              "Reverse mount resolution");
        error([&] { context.resolve("/Game/../abc"); }, "package-path");
        fs::create_directories(root / "engine" / "Engine" / "Build");
        {
            std::ofstream(root / "engine" / "Engine" / "Build" / "Build.version")
                << R"({"MajorVersion":5,"MinorVersion":4,"PatchVersion":2})";
        }
        check(ProjectContext::open(root / "Test.uproject", root / "engine").engine.patch == 2,
              "Explicit target engine");
        fs::remove_all(root);
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << "; fixtures: " << root << '\n';
        return 1;
    }
}
