#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "text_format_fixture.hpp"
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
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected byte string refusal");
}
} // namespace
int main() {
    const auto root =
        fs::current_path() /
        (".test-tmp-strings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schema = [](unsigned kind) {
            return UnversionedSchemas::parse(collection_schema_fixture(collection_type(kind)).data);
        };
        auto utf8 = schema(29), ascii = schema(30);
        UnversionedReadOptions options;
        options.byte_string_layout = ByteStringLayout::counted_utf8_ascii;
        auto decode = [&](const Bytes &payload, const UnversionedSchemas &schemas,
                          const UnversionedReadOptions &opts) {
            auto bytes = collection_single(payload);
            return decode_unversioned(bytes.data, 0, bytes.data.size(), schemas, "Collections", opts);
        };
        const std::string unicode = "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
        const std::vector<std::string> valid = {
            "", "ASCII", std::string("A\0B", 3), unicode,
            "\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf"};
        for (const auto &source : valid) {
            auto bytes = byte_string_value(source);
            auto value = decode(bytes, utf8, options);
            check(value.properties[field]["value"] == source, "UTF8 scalar bytes and embedded NUL preserved");
            check(value.consumed_bytes == source.size() + 6, "Count excludes terminator; no byte guessed");
            auto trailing = bytes;
            trailing.zeros(5);
            check(decode(trailing, utf8, options).consumed_bytes == bytes.data.size() + 2,
                  "Trailing native bytes untouched");
            auto tagged = Package::open(save(root, "valid", text_property_fixture(bytes, "Utf8StrProperty")));
            check(tagged.exports[0].properties["Formatted"]["value"] == source &&
                      tagged.exports[0].properties["AfterFormat"]["value"] == 876,
                  "Tagged byte string and following property remain aligned");
        }
        auto raw = byte_string_value(unicode);
        rejects([&] { decode(raw, utf8, {}); }, "schema-context");
        for (std::size_t end = 0; end < raw.data.size(); ++end) {
            auto cut = raw;
            cut.data.resize(end);
            rejects([&] { decode(cut, utf8, options); }, "bounds");
        }
        const std::vector<std::string> invalid = {"\x80",
                                                  "\xc0\x80",
                                                  "\xc1\xbf",
                                                  "\xc2",
                                                  "\xe0\x80\x80",
                                                  "\xed\xa0\x80",
                                                  "\xf0\x80\x80\x80",
                                                  "\xf4\x90\x80\x80",
                                                  "\xf5\x80\x80\x80",
                                                  "\xff",
                                                  "\xe2\x28\xa1"};
        for (const auto &source : invalid) {
            auto bytes = byte_string_value(source);
            rejects([&] { decode(bytes, utf8, options); }, "schema-value");
            const auto tagged =
                Package::open(save(root, "bad", text_property_fixture(bytes, "Utf8StrProperty")));
            check(tagged.exports[0].properties["Formatted"]["opaque"]["reason"] == "string-encoding" &&
                      tagged.exports[0].properties["AfterFormat"]["value"] == 876,
                  "Malformed tagged bytes remain opaque without losing next property");
        }
        for (int length : {-1, INT32_MIN}) {
            Bytes bytes;
            bytes.i(length);
            rejects([&] { decode(bytes, utf8, options); }, "schema-value");
        }
        for (const auto &source : {std::string("abc"), std::string("A\0Z", 3), std::string()})
            check(decode(byte_string_value(source), ascii, options).properties[field]["value"] == source,
                  "ANSI ASCII subset preserves byte count and embedded NUL");
        rejects([&] { decode(raw, ascii, options); }, "schema-type");
        auto ansi = Package::open(
            save(root, "ansi", text_property_fixture(byte_string_value("plain"), "AnsiStrProperty")));
        check(ansi.exports[0].properties["Formatted"]["value"] == "plain", "Tagged ANSI ASCII read");
        for (unsigned kind : {29u, 30u}) {
            Bytes zero;
            zero.put<std::uint16_t>(0x0380);
            zero.put<std::uint8_t>(1);
            auto result = decode_unversioned(zero.data, 0, 3, schema(kind), "Collections");
            check(result.consumed_bytes == 3 && result.properties[field]["value"] == "" &&
                      result.properties[field]["serializedZero"] == true,
                  "Zero byte string consumes no length or bytes");
        }
        auto listSchema = UnversionedSchemas::parse(
            collection_schema_fixture(collection_type(8, collection_type(29))).data);
        Bytes list;
        list.i(2);
        list.append(byte_string_value(""));
        list.append(raw);
        check(decode(list, listSchema, options).properties[field]["value"][1] == unicode,
              "Nested UTF8 array");
        auto limited = options;
        limited.max_text_bytes = field.size() + unicode.size() - 1;
        rejects([&] { decode(raw, utf8, limited); }, "schema-limit");
        limited = options;
        limited.max_values = 1;
        rejects([&] { decode(raw, utf8, limited); }, "schema-limit");
        limited = options;
        limited.control.cancelled = [] { return true; };
        rejects([&] { decode(raw, utf8, limited); }, "cancelled");
        std::uint64_t charged = 0;
        limited = options;
        limited.charge = [&](std::uint32_t, std::uint64_t text) { charged += text; };
        const auto empty_read = decode(byte_string_value(""), utf8, limited);
        const auto overhead = charged;
        charged = 0;
        const auto full_read = decode(raw, utf8, limited);
        check(overhead == empty_read.text_bytes && charged == full_read.text_bytes &&
                  charged - overhead == unicode.size(),
              "Byte string payload and retained context share the exact enclosing text budget");
        auto old = Package::open(save(root, "old", text_property_fixture(raw, "Utf8StrProperty", 1009)));
        check(old.exports[0].properties["Formatted"]["opaque"]["reason"] == "string-layout",
              "Unchecked tagged profile remains opaque");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_5;
        context->schemas = std::make_shared<UnversionedSchemas>(utf8);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        const auto before = ZenPackage::parse(collection_zen_fixture(collection_single(raw), true).data, zen);
        check(before.property_exports_decoded == 2, "Checked UE5.5 Zen byte string layout");
        const auto after = ZenPackage::parse(
            collection_zen_fixture(collection_single(byte_string_value("Edited")), true).data, zen);
        const auto diff = compare_zen_packages(before, after);
        bool found = false;
        for (const auto &c : diff["changes"])
            if (c["path"].get<std::string>().find("/properties/~1Script~1Test.Collections.Lookup/value") !=
                std::string::npos)
                found = c["before"] == unicode && c["after"] == "Edited";
        check(found, "UTF8 edit has precise property diff");
        Bytes bad;
        bad.i(-1);
        const auto opaque = ZenPackage::parse(collection_zen_fixture(collection_single(bad), true).data, zen);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties"),
              "Invalid byte string discards entire mapped property read");
        context->profile = ZenProfile::unreal_5_3;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(raw)).data, zen)
                      .exports[0]
                      .property_read["status"] == "opaque",
              "Unchecked UE5.3 byte string layout remains opaque");
        fs::remove_all(root);
        std::cout << checks << " byte string checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
