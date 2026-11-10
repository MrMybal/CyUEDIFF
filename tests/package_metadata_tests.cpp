#include <cyuediff/cache.hpp>
#include <cyuediff/decoders.hpp>
#include "package_fixture.hpp"
#include "../src/native.hpp"
#include <chrono>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
struct Row {
    std::string name, value;
    int number = 0;
    std::u16string wide;
};
using Rows = std::vector<Row>;
using Objects = std::vector<std::pair<int, Rows>>;
const Rows roots = {{"Literal_1", "literal"},
                    {"Literal", "numbered", 2},
                    {"Esc/~Key", "", 0, u"\u00c9l\u00e9phant \u03bb \U0001f680"}};
const Objects objects = {{1, {{"Literal", "object", 2}}}, {0, {}}, {-1, {{"Esc/~Key", "import"}}}};
void values(Bytes &b, const NameWriter &name, const Rows &rows) {
    b.i(static_cast<int>(rows.size()));
    for (const auto &row : rows) {
        name(b, row.name);
        b.patch(b.data.size() - 4, row.number);
        if (row.wide.empty())
            b.string(row.value);
        else {
            b.i(-static_cast<int>(row.wide.size() + 1));
            for (const auto c : row.wide)
                b.put<std::uint16_t>(c);
            b.put<std::uint16_t>(0);
        }
    }
}
Bytes wire(const NameWriter &name, const Rows &root, const Objects &entries, bool guid = false) {
    Bytes b;
    b.i(guid ? 1 : 0);
    if (guid)
        for (const auto word : {1u, 0x12345678u, 0xabcdef09u, 0xffffffffu})
            b.put(word);
    b.i(static_cast<int>(entries.size()));
    for (const auto &entry : entries) {
        b.i(entry.first);
        values(b, name, entry.second);
    }
    values(b, name, root);
    return b;
}
Fixture package_fixture(const Rows &root = roots, const Objects &entries = {{1, {{"Literal", "object", 2}}}},
                        int version = 1009, int editor = 40, bool reorder = false, bool guid = false) {
    std::vector<std::string> names;
    for (const auto &row : root)
        names.push_back(row.name);
    for (const auto &entry : entries)
        for (const auto &row : entry.second)
            names.push_back(row.name);
    Bytes native;
    auto f = fixture(
        version, 1, reorder, 0, names,
        [&](Bytes &, const NameWriter &name) { native = wire(name, root, entries, guid); }, editor, -1, -1,
        -1, -1, -1, -1, "MetaData");
    f.script.data.resize(f.script.data.size() - 4);
    f.script.append(native);
    const auto size = std::int64_t(f.script.data.size());
    std::memcpy(f.header.data.data() + f.export_offset + 28, &size, 8);
    return f;
}
std::string id(const std::string &name, int number = 0) { return Json::array({name, number}).dump(); }
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-package-metadata-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        Package context;
        context.summary.ue4_version = 522;
        context.summary.ue5_version = 1009;
        context.summary.custom_versions["e4b068edf49442e9a231da0b2e46bb41"] = 40;
        context.names = {"Literal_1", "Literal", "Esc/~Key"};
        ObjectExport target;
        target.name = "Target";
        context.exports.push_back(target);
        ObjectImport imported;
        imported.name = "Imported";
        context.imports.push_back(imported);
        const NameWriter name = [&](Bytes &b, const std::string &s) {
            const auto at = std::find(context.names.begin(), context.names.end(), s);
            if (at == context.names.end())
                throw std::runtime_error("Undeclared fixture name");
            b.i(static_cast<int>(at - context.names.begin()));
            b.i(0);
        };
        auto decode = [&](Bytes b, const ReadOptions &options = ReadOptions{}) {
            const auto path = root / "wire.bin";
            b.save(path);
            detail::Archive archive(path, options);
            auto value = detail::decode_package_metadata(archive, context, b.data.size(), options);
            check(value.end == b.data.size(), "Successful metadata consumes its complete native window");
            return value.data;
        };
        auto rejected = [&](Bytes b, const ReadOptions &options = ReadOptions{},
                            const std::string &code = "") {
            try {
                decode(std::move(b), options);
            } catch (const Error &error) {
                return code.empty() || error.code == code;
            }
            return false;
        };
        auto original = wire(name, roots, objects);
        const auto decoded = decode(original);
        check(decoded["kind"] == "package-metadata" && decoded["scope"] == "stored",
              "Stored scope is explicit");
        check(decoded["rootValues"].size() == 3 && decoded["objectValues"].size() == 3,
              "Root and object maps are distinct");
        check(decoded["rootValues"][id("Literal_1")]["value"] == "literal" &&
                  decoded["rootValues"][id("Literal", 2)]["value"] == "numbered",
              "Same-display stored FNames stay distinct");
        check(decoded["rootValues"][id("Esc/~Key")]["value"] == u8"Éléphant λ 🚀",
              "UTF16 surrogate pairs become exact UTF8");
        check(decoded["objectValues"]["null"]["object"].is_null() &&
                  decoded["objectValues"]["null"]["values"].empty(),
              "Null weak object map is retained");
        check(decoded["objectValues"][Json("Imported").dump()]["object"] == "Imported",
              "Import references resolve through the package");
        const auto with_guid = decode(wire(name, roots, objects, true));
        check(with_guid["objectGuid"] == "0000000112345678abcdef09ffffffff",
              "Optional GUID preserves all four words");
        auto reordered_roots = roots;
        std::reverse(reordered_roots.begin(), reordered_roots.end());
        auto reordered_objects = objects;
        std::reverse(reordered_objects.begin(), reordered_objects.end());
        check(decode(wire(name, reordered_roots, reordered_objects)) == decoded,
              "Map storage order cannot create semantic changes");
        check(decode(wire(name, {}, {}))["rootValues"].empty(), "Empty metadata is a valid stored map");
        for (std::size_t cut = 0; cut < original.data.size(); ++cut) {
            auto broken = original;
            broken.data.resize(cut);
            check(rejected(std::move(broken)),
                  "Every truncated prefix fails without publishing a decoded prefix");
        }
        for (const int marker : {-1, 2, std::numeric_limits<int>::max()}) {
            auto broken = original;
            broken.patch(0, marker);
            check(rejected(std::move(broken), {}, "metadata-guid"),
                  "Invalid UObject GUID markers are rejected");
        }
        auto zero_guid = wire(name, {}, {}, true);
        std::fill(zero_guid.data.begin() + 4, zero_guid.data.begin() + 20, 0);
        check(rejected(zero_guid, {}, "metadata-guid"), "Declared zero GUID cannot certify metadata");
        for (const int count : {-1, 1'000'000}) {
            auto broken = original;
            broken.patch(4, count);
            check(rejected(broken), "Negative and excessive object counts are rejected");
            auto bad_roots = wire(name, {}, {});
            bad_roots.patch(8, count);
            check(rejected(bad_roots), "Negative and excessive root counts are rejected");
        }
        for (const int index : {2, -2, std::numeric_limits<int>::min()}) {
            check(rejected(wire(name, {}, {{index, {}}}), {}, "metadata-reference"),
                  "Out-of-range package references are rejected");
        }
        context.exports[0].outer_index = 1;
        check(rejected(wire(name, {}, {{1, {}}}), {}, "metadata-reference"),
              "Cyclic reference traversal is bounded");
        context.exports[0].outer_index = 0;
        check(rejected(wire(name, {roots[0], roots[0]}, {}), {}, "metadata-key"),
              "Duplicate stored FName keys are rejected");
        check(rejected(wire(name, {}, {{0, {}}, {0, {}}}), {}, "metadata-key"),
              "Duplicate object maps are rejected");
        auto invalid_name = wire(name, {roots[0]}, {});
        invalid_name.patch(12, 99);
        check(rejected(invalid_name, {}, "metadata-name"),
              "Invalid name-table indices cannot enter metadata");
        invalid_name = wire(name, {roots[0]}, {});
        invalid_name.patch(16, -1);
        check(rejected(invalid_name, {}, "metadata-name"), "Negative internal FName numbers are rejected");
        auto bad_string = wire(name, {{"Literal", "X"}}, {});
        bad_string.data.back() = 'Y';
        check(rejected(bad_string, {}, "format"), "Unterminated FString cannot be published");
        bad_string = wire(name, {{"Literal", "", 0, u"ab"}}, {});
        bad_string.data[24] = 0x00;
        bad_string.data[25] = 0xd8;
        check(rejected(bad_string, {}, "format"), "Unpaired UTF16 surrogate is rejected");
        auto narrow = wire(name, {{"Literal", std::string("caf\xe9")}}, {});
        check(rejected(narrow, {}, "metadata-string-encoding"), "Unspecified ANSI code page is not guessed");
        ReadOptions latin;
        latin.narrow_string_encoding = NarrowStringEncoding::latin1;
        check(decode(narrow, latin)["rootValues"][id("Literal")]["value"] == u8"café",
              "Explicit Latin1 selection decodes narrow text");
        auto trailing = original;
        trailing.i(0);
        check(rejected(trailing, {}, "metadata-size"),
              "Unconsumed trailing bytes cannot certify the native suffix");
        for (int profile = 0; profile < 6; ++profile) {
            const auto saved = context.summary;
            if (profile == 0)
                context.summary.ue4_version = 521;
            if (profile == 1)
                context.summary.ue5_version = 1014;
            if (profile == 2)
                context.summary.licensee_version = 1;
            if (profile == 3)
                context.summary.flags = 0x80000000u;
            if (profile == 4)
                context.summary.flags = 0x2000u;
            if (profile == 5)
                context.summary.custom_versions["e4b068edf49442e9a231da0b2e46bb41"] = 41;
            check(rejected(original, {}, "metadata-profile"),
                  "Unselected source/version/editor profiles remain unavailable");
            context.summary = saved;
        }
        ReadOptions limited;
        limited.max_properties = 2;
        check(rejected(original, limited, "limit"), "Nested maps share the work budget");
        limited = {};
        limited.max_property_bytes = original.data.size() - 1;
        check(rejected(original, limited, "limit"), "Native byte windows are bounded");
        limited = {};
        limited.max_property_bytes = original.data.size();
        auto long_names = context.names;
        context.names[0] = std::string(400, 'x');
        check(rejected(original, limited, "limit"), "Projected key text shares the aggregate budget");
        context.names = long_names;
        limited = {};
        limited.max_string_bytes = 3;
        check(rejected(original, limited, "limit"), "FString allocations respect the string limit");
        limited = {};
        int calls = 0;
        limited.control.cancelled = [&] { return ++calls > 20; };
        check(rejected(original, limited, "cancelled") && calls > 20,
              "Cancellation during map traversal propagates");
        const auto path = save(root, "before", package_fixture());
        auto before = Package::open(path);
        check(before.exports[0].native_coverage == "package-metadata" && before.exports[0].opaque.empty(),
              "Public source reader exposes complete native metadata");
        auto alternate = Package::open(save(
            root, "alternate", package_fixture(roots, {{1, {{"Literal", "object", 2}}}}, 1013, 40, true)));
        check(before.exports[0].native_data == alternate.exports[0].native_data,
              "UE5.3/5.5 profiles preserve native metadata independently of tagged type evolution");
        check(
            before.semantic_snapshot() ==
                Package::open(save(root, "reordered",
                                   package_fixture(roots, {{1, {{"Literal", "object", 2}}}}, 1009, 40, true)))
                    .semantic_snapshot(),
            "Reordering the name table cannot change the semantic snapshot");
        const auto split = Package::open(save(root, "split", package_fixture(), true));
        check(split.semantic_snapshot() == before.semantic_snapshot(),
              "Split uexp native metadata shares the source reader");
        auto changed = roots;
        changed[1].value = "changed";
        auto after = Package::open(save(root, "after", package_fixture(changed)));
        const auto report = compare(before, after).to_json();
        check(report["changes"].size() == 1 && report["status"] == "changed",
              "Known metadata edits create an ordinary semantic diff");
        check(report["changes"][0]["path"] == "/objects/TestAsset/native/rootValues/[\"Literal\",2]/value",
              "Changed stored metadata names have stable named paths");
        ReadOptions disabled;
        disabled.native_metadata = false;
        const auto unavailable = Package::open(path, disabled);
        check(unavailable.exports[0].native_data.empty() && !unavailable.exports[0].opaque.empty(),
              "Disabled metadata keeps its unread bytes opaque");
        check(compare(before, unavailable).to_json()["status"] == "unverified",
              "Unavailable peer cannot certify metadata equality");
        PackageCache cache;
        const auto enabled_cache = cache.open(path);
        const auto disabled_cache = cache.open(path, disabled);
        check(enabled_cache != disabled_cache && disabled_cache->exports[0].native_data.empty(),
              "Cache keys include native metadata selection");
        check(cache.open(path) == enabled_cache, "Metadata cache can reuse the identical enabled profile");
        AssetDecoder extension;
        extension.id = "test.metadata/1";
        extension.kind = DecoderKind::native_export;
        extension.target = "/Script/CoreUObject.MetaData";
        extension.decode = [](DecoderInput &input, const DecoderContext &) -> Json {
            input.skip(input.remaining());
            return {{"host", true}};
        };
        ReadOptions custom;
        custom.decoders = std::make_shared<const DecoderRegistry>(std::vector<AssetDecoder>{extension});
        check(Package::open(path, custom).exports[0].native_coverage == "extension",
              "Explicit host decoder retains precedence over the builtin");
        extension.decode = [](DecoderInput &, const DecoderContext &) -> Json {
            throw Error("host", "Selected host decoder failed");
        };
        custom.decoders = std::make_shared<const DecoderRegistry>(std::vector<AssetDecoder>{extension});
        const auto failed_host = Package::open(path, custom);
        check(failed_host.exports[0].native_data.empty() &&
                  failed_host.exports[0].native_coverage == "opaque",
              "Failed explicit decoder does not silently select a different interpretation");
        auto broken = package_fixture();
        broken.script.data.pop_back();
        const auto size = std::int64_t(broken.script.data.size());
        std::memcpy(broken.header.data.data() + broken.export_offset + 28, &size, 8);
        const auto failed = Package::open(save(root, "broken", broken));
        check(failed.exports[0].native_data.empty() && failed.exports[0].native_coverage == "opaque" &&
                  !failed.exports[0].opaque.empty(),
              "Public failure is transactional and retains the suffix fingerprint");
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto output = fs::u8path(argv[2]);
            fs::create_directories(output);
            save(output, "before", package_fixture());
            save(output, "after", package_fixture(changed));
            save(output, "guid", package_fixture(roots, {}, 1009, 40, false, true));
        }
        fs::remove_all(root);
        std::cout << checks << " package metadata checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
