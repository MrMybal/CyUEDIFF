#include <cyuediff/string_encoding.hpp>
#include <cyuediff/binary_layout.hpp>
#include <cyuediff/cache.hpp>
#include <cyuediff/schema_context.hpp>
#include "native_property_fixture.hpp"
#include "schema_collection_fixture.hpp"
#include "../src/archive.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool b, const char *m) {
    ++checks;
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f, const char *code = nullptr) {
    try {
        f();
    } catch (const Error &e) {
        check(!code || e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected encoding rejection");
}
std::string python_style_utf8(unsigned cp) {
    if (cp < 128)
        return std::string(1, char(cp));
    if (cp < 2048)
        return std::string({char(0xc0 | (cp >> 6)), char(0x80 | (cp & 63))});
    return std::string({char(0xe0 | (cp >> 12)), char(0x80 | ((cp >> 6) & 63)), char(0x80 | (cp & 63))});
}
Json encoding_catalog(const char *encoding) {
    return {{"format", "cyuediff.binary-layouts/1"},
            {"narrowStringEncoding", encoding},
            {"layouts", Json::object()}};
}
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() /
        (".test-tmp-encoding-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        for (unsigned i = 0; i < 256; ++i) {
            const std::string byte(1, char(i));
            check(decode_narrow_string(byte, NarrowStringEncoding::latin1, 3) == python_style_utf8(i),
                  "Every Latin-1 code point");
            if (i < 128)
                check(decode_narrow_string(byte, NarrowStringEncoding::ascii, 1) == byte, "ASCII identity");
            else
                rejects([&] { decode_narrow_string(byte, NarrowStringEncoding::ascii, 3); },
                        "string-encoding");
        }
        check(decode_narrow_string("\x80\x93\x94\x9c\xe9", NarrowStringEncoding::windows_1252, 20) ==
                  "\xe2\x82\xac\xe2\x80\x9c\xe2\x80\x9d\xc5\x93\xc3\xa9",
              "Windows punctuation and accented characters");
        for (unsigned byte : {0x81, 0x8d, 0x8f, 0x90, 0x9d})
            rejects(
                [&] {
                    decode_narrow_string(std::string(1, char(byte)), NarrowStringEncoding::windows_1252, 3);
                },
                "string-encoding");
        for (const std::string bad : {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80"})
            rejects([&] { decode_narrow_string(bad, NarrowStringEncoding::utf8, 20); }, "string-encoding");
        const std::string utf8 = "caf\xc3\xa9 \xf0\x9f\x98\x80";
        check(decode_narrow_string(utf8, NarrowStringEncoding::utf8, utf8.size()) == utf8,
              "Strict UTF-8 identity");
        rejects([&] { decode_narrow_string("\x80", NarrowStringEncoding::windows_1252, 2); }, "string-limit");
        rejects([&] { decode_narrow_string("x", NarrowStringEncoding::unspecified, 1); }, "string-encoding");
        rejects([&] { parse_narrow_string_encoding("system"); }, "string-encoding");
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        rejects([&] { decode_narrow_string("x", NarrowStringEncoding::latin1, 1, cancel); }, "cancelled");
        for (const auto *name : {"ascii", "latin1", "windows-1252", "utf8"})
            check(name == std::string(narrow_string_encoding_name(parse_narrow_string_encoding(name))),
                  "Encoding token round trip");
        const std::string raw = "caf\xe9 \x80";
        Bytes payload;
        payload.string(raw);
        payload.save(root / "string.bin");
        ReadOptions options;
        options.narrow_string_encoding = NarrowStringEncoding::windows_1252;
        {
            detail::Archive input(root / "string.bin", options);
            check(input.string() == "caf\xc3\xa9 \xe2\x82\xac", "Archive FString explicit encoding");
        }
        auto fixture_file =
            save(root, "text",
                 fixture(1013, 1, false, 0, {"EncodingText"}, [&](Bytes &out, const NameWriter &name) {
                     name(out, "EncodingText");
                     name(out, "StrProperty");
                     out.i(0);
                     out.i(static_cast<int>(payload.data.size()));
                     out.put<std::uint8_t>(0);
                     out.append(payload);
                 }));
        check(Package::open(fixture_file, options).exports[0].properties["EncodingText"]["value"] ==
                  "caf\xc3\xa9 \xe2\x82\xac",
              "Tagged FString explicit encoding");
        {
            // Found by fuzzing: without an explicit encoding, non-ASCII narrow bytes used to stay raw
            // and broke every later JSON serialization with a non-cyuediff exception.
            const auto unspecified = Package::open(fixture_file);
            const auto &property = unspecified.exports[0].properties["EncodingText"];
            check(!property.contains("value") && property.at("opaque").at("reason") == "string-encoding",
                  "Unspecified encoding keeps non-ASCII narrow FStrings opaque");
            check(!unspecified.semantic_snapshot().dump().empty() &&
                      compare(unspecified, unspecified).changes.empty(),
                  "Packages with non-ASCII narrow strings still serialize and compare");
            detail::Archive input(root / "string.bin", ReadOptions{});
            rejects([&] { input.string(); }, "string-encoding");
        }
        PackageCache cache;
        auto first = cache.open(fixture_file, options);
        options.narrow_string_encoding = NarrowStringEncoding::latin1;
        auto second = cache.open(fixture_file, options);
        check(first != second && cache.stats().misses == 2, "Cache separates encoding profiles");
        check(second->exports[0].properties["EncodingText"]["value"] == "caf\xc3\xa9 \xc2\x80",
              "Latin-1 interpretation retained");
        auto node_description = encoding_catalog("latin1");
        node_description["layouts"]["Text"] = {{"type", "string"}, {"encoding", "windows-1252"}};
        node_description["decoders"] =
            Json::array({{{"id", "encoding.text/1"},
                          {"kind", "struct"},
                          {"target", "EncodedString"},
                          {"profile", {{"ue4", {522, 522}}, {"ue5", {1013, 1013}}}},
                          {"layout", "Text"}}});
        const auto native_file =
            save(root, "native-encoded", native_property_fixture(payload, "EncodedString"));
        ReadOptions native_options;
        native_options.decoders = BinaryLayouts::parse(node_description).decoder_registry();
        check(Package::open(native_file, native_options).exports[0].properties["Native"]["value"]["data"] ==
                  "caf\xc3\xa9 \xe2\x82\xac",
              "Per-field encoding overrides catalogue encoding");
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_type(10)).data);
        auto wire = collection_single(payload);
        UnversionedReadOptions read;
        read.narrow_string_encoding = NarrowStringEncoding::windows_1252;
        auto decode = [&](const UnversionedReadOptions &o) {
            return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", o);
        };
        check(decode(read).properties["/Script/Test.Collections.Lookup"]["value"] ==
                  "caf\xc3\xa9 \xe2\x82\xac",
              "Mapped FString explicit encoding");
        rejects([&] { decode({}); }, "schema-type");
        read.max_text_bytes = 32;
        rejects([&] { decode(read); }, "schema-limit");
        auto manifest =
            Json{{"format", "cyuediff.zen-schema/3"},
                 {"profile", "ue5.3"},
                 {"usmap", "text.usmap"},
                 {"sha256", schemas.sha256()},
                 {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "Collections"}, {"offset", 0u}}}}},
                 {"nativeStructs", Json::object()},
                 {"binaryLayouts", encoding_catalog("windows-1252")},
                 {"structLayouts", Json::object()}};
        collection_schema_fixture(collection_type(10)).save(root / "text.usmap");
        std::ofstream(root / "context.json") << manifest.dump();
        const auto context = ZenSchemaManifest::read(root / "context.json").load();
        check(context->narrow_string_encoding == NarrowStringEncoding::windows_1252,
              "Schema context inherits explicit encoding catalogue");
        auto other = *context;
        other.narrow_string_encoding = NarrowStringEncoding::latin1;
        check(other.fingerprint() != context->fingerprint(), "Encoding changes cooked context identity");
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto z = ZenPackage::parse(collection_zen_fixture(wire).data, zen);
        check(z.exports[0].property_read["properties"]["/Script/Test.Collections.Lookup"]["value"] ==
                  "caf\xc3\xa9 \xe2\x82\xac",
              "Zen explicit encoded FString");
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            fs::copy_file(fixture_file, out / "encoded.uasset", fs::copy_options::overwrite_existing);
            std::ofstream(out / "encoded-layouts.json") << encoding_catalog("windows-1252").dump(2);
            collection_schema_fixture(collection_type(10)).save(out / "text.usmap");
            std::ofstream(out / "encoded-context.json") << manifest.dump(2);
            auto package = collection_zen_fixture(wire);
            package.save(out / "encoded.ucas");
            zen_toc(package).save(out / "encoded.utoc");
            auto global = script_fixture();
            global.save(out / "encoded-global.ucas");
            zen_toc(global, 5).save(out / "encoded-global.utoc");
        }
        fs::remove_all(root);
        std::cout << checks << " string encoding checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks\nFixtures: " << root << '\n';
        return 1;
    }
}
