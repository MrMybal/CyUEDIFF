#include <cyuediff/zen.hpp>
#include "zen_fixture.hpp"
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
        throw std::runtime_error("Expected cooked error");
    } catch (const Error &e) {
        if (e.code != code)
            throw std::runtime_error("Expected " + std::string(code) + ", got " + e.code + ": " + e.what());
        ++checks;
    }
}
unsigned u32(const Bytes &b, std::size_t at) {
    unsigned value;
    std::memcpy(&value, b.data.data() + at, 4);
    return value;
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
        auto root = fs::u8path(argv[2]);
        fs::create_directories(root);
        auto payload = zen_fixture();
        payload.save(root / "test.ucas");
        zen_toc(payload).save(root / "test.utoc");
        auto changed = payload;
        changed.data[u32(payload, 4)] = 'X';
        changed.save(root / "changed.ucas");
        zen_toc(changed).save(root / "changed.utoc");
        auto scripts = script_fixture();
        scripts.save(root / "global.ucas");
        zen_toc(scripts, 5).save(root / "global.utoc");
        auto other = script_fixture("OtherAsset");
        other.save(root / "other-global.ucas");
        zen_toc(other, 5).save(root / "other-global.utoc");
        return 0;
    }
    auto root =
        fs::current_path() /
        (".test-tmp-zen-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        ZenReadOptions options;
        options.profile = ZenProfile::unreal_5_3;
        auto scripts = ScriptObjects::parse(script_fixture().data);
        options.script_objects = std::make_shared<ScriptObjects>(scripts);
        check(scripts.size() == 3 && scripts.resolve(script_class) == "/Script/Test.DemoAsset",
              "Global script classes resolve");
        check(scripts.resolve(0).empty() && !scripts.sha256().empty(),
              "Unknown script IDs and context fingerprint");
        auto bytes = zen_fixture();
        auto p = ZenPackage::parse(bytes.data, options);
        check(p.name == "/Game/Example" && p.exports.size() == 2 && p.unversioned_properties,
              "Cooked summary and exports");
        check(p.exports[1].path == "/Game/Example.Example.Nested", "Local outer names resolve");
        check(p.exports[0].class_name == "/Script/Test.DemoAsset", "Cooked class uses global script table");
        check(p.exports[0].sha256 == sha256_bytes({'a', 'b'}), "Exact opaque export range fingerprint");
        check(p.bulk_data.size() == 1 && p.bulk_data[0]["duplicateOffset"] == -1,
              "Signed bulk resource metadata");
        check(p.other_payload.size() == 2, "Unclaimed payload gaps retained");
        check(p.imports[1]["publicExportHash"] == "0000000000001234" &&
                  p.imports[1]["package"] == "/Game/Referenced",
              "Public package import reference");
        check(p.dependencies[1]["commandGroups"][0][0]["kind"] == "package", "Dependency import identities");
        check(p.to_json()["propertiesDecoded"] == false && p.to_json()["externalBulkCompared"] == false,
              "Explicit cooked coverage limits");
        check(compare_zen_packages(p, p)["status"] == "identical", "Cooked self comparison");
        auto reordered = ZenPackage::parse(zen_fixture(false, false, true).data, options);
        check(compare_zen_packages(p, reordered)["status"] == "binary-only",
              "Export table reorder preserves stable object identities");
        auto changed = bytes;
        changed.data[p.header_size] = 'X';
        auto difference = compare_zen_packages(p, ZenPackage::parse(changed.data, options));
        check(difference["changes"].size() == 1 &&
                  difference["changes"][0]["path"] == "/objects/~1Game~1Example.Example/opaque/sha256",
              "Opaque change targets one object with escaped pointer");
        changed = bytes;
        changed.patch(u32(bytes, 32) + 64, 4);
        difference = compare_zen_packages(p, ZenPackage::parse(changed.data, options));
        check(difference["changes"].size() == 1, "Metadata-only object flag change");
        changed = bytes;
        changed.data.back() = 'X';
        difference = compare_zen_packages(p, ZenPackage::parse(changed.data, options));
        check(difference["status"] == "changed", "Trailing bytes participate in equality");
        auto other_options = options;
        other_options.script_objects =
            std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture("OtherAsset").data));
        difference = compare_zen_packages(p, ZenPackage::parse(bytes.data, other_options));
        check(difference["contextChanged"] == true && difference["binaryChanged"] == false &&
                  difference["status"] == "changed",
              "Distinct script contexts affect resolved class comparison");
        other_options = options;
        other_options.script_objects = {};
        check(ZenPackage::parse(bytes.data, other_options).exports[0].class_name.find("$script:") == 0,
              "Unresolved script identity retained");
        for (bool modern : {false, true})
            for (bool versioned : {false, true}) {
                auto o = options;
                o.profile = modern ? ZenProfile::unreal_5_5 : ZenProfile::unreal_5_3;
                auto z = ZenPackage::parse(zen_fixture(modern, versioned).data, o);
                check(z.exports[0].sha256 == p.exports[0].sha256,
                      "Both explicit bulk layouts and optional versioning");
                if (versioned)
                    check(z.versioning["customVersions"].size() == 1, "Custom versions preserved");
            }
        error([&] { ZenPackage::parse(bytes.data, {}); }, "zen-profile");
        other_options = options;
        other_options.profile = ZenProfile::unreal_5_5;
        error([&] { ZenPackage::parse(bytes.data, other_options); }, "zen-format");
        auto bad = zen_fixture(false, true);
        bad.patch(52, 4);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-version");
        bad = bytes;
        bad.patch(4, 51);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(28, u32(bytes, 24) - 1);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(8, 9999);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(12, -1);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(u32(bytes, 32) + 24, 0);
        bad.patch(u32(bytes, 32) + 28, 0);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(u32(bytes, 32) + 32, 99);
        bad.patch(u32(bytes, 32) + 36, 0);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(u32(bytes, 32) + 16, 0x40000000);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(u32(bytes, 36) + 4, 1);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(u32(bytes, 32) + 8, 100000);
        error([&] { ZenPackage::parse(bad.data, options); }, "bounds");
        bad = bytes;
        bad.patch(u32(bytes, 32) + 72, 0);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(u32(bytes, 40) + 20, 9999);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.patch(u32(bytes, 44), 9999);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        bad = bytes;
        bad.data.resize(p.header_size - 1);
        error([&] { ZenPackage::parse(bad.data, options); }, "zen-format");
        auto limit = options;
        limit.max_package_bytes = 1;
        error([&] { ZenPackage::parse(bytes.data, limit); }, "zen-limit");
        limit = options;
        limit.max_header_bytes = 1;
        error([&] { ZenPackage::parse(bytes.data, limit); }, "zen-limit");
        limit = options;
        limit.max_entries = 1;
        error([&] { ZenPackage::parse(bytes.data, limit); }, "zen-limit");
        limit = options;
        limit.max_text_bytes = 1;
        error([&] { ZenPackage::parse(bytes.data, limit); }, "zen-limit");
        limit = options;
        limit.max_depth = 1;
        error([&] { ZenPackage::parse(bytes.data, limit); }, "zen-limit");
        limit = options;
        limit.max_name_bytes = 3;
        error([&] { ZenPackage::parse(bytes.data, limit); }, "zen-limit");
        limit = options;
        limit.control.cancelled = [] { return true; };
        error([&] { ZenPackage::parse(bytes.data, limit); }, "cancelled");
        error([&] { ScriptObjects::parse(script_fixture().data, limit); }, "cancelled");
        error([&] { compare_zen_packages(p, p, limit.control); }, "cancelled");
        bool cancel = false;
        limit = options;
        limit.control.cancelled = [&] { return cancel; };
        limit.control.progress = [&](const auto &) { cancel = true; };
        error([&] { ZenPackage::parse(bytes.data, limit); }, "cancelled");
        auto unicode = script_fixture();
        auto class_at = std::size_t(46 + 12);
        Bytes utf16;
        for (std::uint16_t cp : {0x00c9, 0xd83d, 0xdc99})
            utf16.put(cp);
        unicode.data.erase(unicode.data.begin() + class_at, unicode.data.begin() + class_at + 9);
        unicode.data.insert(unicode.data.begin() + class_at, utf16.data.begin(), utf16.data.end());
        unicode.patch(4, static_cast<int>(u32(unicode, 4)) - 3);
        unicode.data[42] = 0x80;
        unicode.data[43] = 3;
        check(ScriptObjects::parse(unicode.data).resolve(script_class) == "/Script/Test.É💙",
              "UTF-16 global names and supplementary Unicode");
        unicode.data[class_at + 4] = 0;
        unicode.data[class_at + 5] = 0xd8;
        error([&] { ScriptObjects::parse(unicode.data); }, "zen-format");
        auto cyclic = script_fixture();
        auto table = cyclic.data.size() - 96;
        std::memcpy(cyclic.data.data() + table + 16, &script_class, 8);
        error([&] { ScriptObjects::parse(cyclic.data); }, "zen-format");
        auto missing = script_fixture();
        std::uint64_t absent = 0x4000000000000999ull;
        std::memcpy(missing.data.data() + table + 16, &absent, 8);
        error([&] { ScriptObjects::parse(missing.data); }, "zen-format");
        auto duplicate = script_fixture();
        std::memcpy(duplicate.data.data() + table + 40, &script_root, 8);
        error([&] { ScriptObjects::parse(duplicate.data); }, "zen-format");
        auto overlap = bytes;
        overlap.patch(u32(bytes, 32) + 72, 0);
        unsigned hash_calls = 0;
        auto bounded = options;
        bounded.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Hashing data")
                ++hash_calls;
        };
        error([&] { ZenPackage::parse(overlap.data, bounded); }, "zen-format");
        check(hash_calls == 0, "Overlapping ranges rejected before hashing");
        auto global = script_fixture();
        global.data.back() = 0;
        error([&] { ScriptObjects::parse(global.data); }, "zen-format");
        global = script_fixture();
        global.data.resize(global.data.size() - 1);
        error([&] { ScriptObjects::parse(global.data); }, "bounds");
        limit = options;
        limit.max_depth = 1;
        error([&] { ScriptObjects::parse(script_fixture().data, limit); }, "zen-limit");
        bytes.save(root / "test.ucas");
        zen_toc(bytes).save(root / "test.utoc");
        auto container = Container::open(root / "test.utoc");
        check(read_zen_package(container, 0, options).sha256 == p.sha256, "Container-to-cooked Core API");
        auto catalog = catalog_zen_packages(container, options);
        check(catalog["complete"] == true && catalog["exportCount"] == 2, "Cooked catalogue API");
        ZenCatalogOptions catalog_limits;
        catalog_limits.max_packages = 0;
        check(catalog_zen_packages(container, options, {}, catalog_limits)["complete"] == false,
              "Cooked catalogue package limit");
        catalog_limits = {};
        catalog_limits.max_exports = 1;
        check(catalog_zen_packages(container, options, {}, catalog_limits)["errors"][0]["error"] ==
                  "zen-catalog-limit",
              "Cooked catalogue aggregate export limit");
        catalog_limits = {};
        catalog_limits.max_report_bytes = 1;
        catalog_limits.include_snapshots = true;
        check(catalog_zen_packages(container, options, {}, catalog_limits)["packages"].empty(),
              "Cooked catalogue report limit");
        error([&] { read_zen_package(container, 1, options); }, "zen-entry");
        ContainerReadOptions cancelled;
        cancelled.control.cancelled = [] { return true; };
        error([&] { read_zen_package(container, 0, options, cancelled); }, "cancelled");
        error([&] { catalog_zen_packages(container, options, cancelled); }, "cancelled");
        bad = bytes;
        bad.patch(4, 1);
        bad.save(root / "bad.ucas");
        zen_toc(bad).save(root / "bad.utoc");
        catalog = catalog_zen_packages(Container::open(root / "bad.utoc"), options);
        check(catalog["complete"] == false && catalog["errors"].size() == 1,
              "Partial cooked catalogue retains failures");
        fs::remove_all(root);
        std::cout << checks << " cooked Zen checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "; fixtures: " << root << '\n';
        return 1;
    }
}
