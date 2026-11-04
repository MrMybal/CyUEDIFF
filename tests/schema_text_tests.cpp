#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "schema_value_fixture.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code = nullptr) {
    try {
        f();
    } catch (const Error &e) {
        check(!code || e.code == code, "Expected text refusal");
        return;
    }
    throw std::runtime_error("Expected text refusal");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            write_value_fixtures(fs::u8path(argv[2]));
            return 0;
        }
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_type(11)).data);
        std::vector<std::string> names = {"None", "/Game/Table", "Table", "Other"};
        UnversionedReadOptions options;
        options.names = &names;
        options.text_layout = TextLayout::ue5_history;
        auto decode = [&](const Bytes &payload, const UnversionedReadOptions &opts) {
            const auto bytes = collection_single(payload);
            return decode_unversioned(bytes.data, 0, bytes.data.size(), schemas, "Collections", opts);
        };
        auto localized = text_value();
        auto result = decode(localized, options);
        const auto expected = Json{{"flags", 0x40000000}, {"history", 0},
                                   {"kind", "localized"}, {"namespace", "Stored Namespace"},
                                   {"key", "StableKey"},  {"source", "Original source"}};
        check(result.properties[field]["value"] == expected,
              "Localized text retains flags, namespace, key and source without evaluation");
        check(result.consumed_bytes == localized.data.size() + 2, "Exact text property window");
        rejects([&] { decode(localized, {}); }, "schema-context");
        for (const auto &payload :
             {localized, invariant_text_value(), invariant_text_value(false), table_text_value()}) {
            for (std::size_t end = 0; end < payload.data.size(); ++end) {
                auto cut = payload;
                cut.data.resize(end);
                rejects([&] { decode(cut, options); }, "bounds");
            }
            auto trailing = payload;
            trailing.zeros(9);
            check(decode(trailing, options).consumed_bytes == payload.data.size() + 2,
                  "Text leaves trailing native payload untouched");
        }
        check(decode(invariant_text_value(), options).properties[field]["value"]["kind"] ==
                  "culture-invariant",
              "Invariant source uses explicit marker");
        check(decode(invariant_text_value(false), options).properties[field]["value"]["source"] == "",
              "Empty history has no inferred localized source");
        auto table = decode(table_text_value(), options).properties[field]["value"];
        check(table["table"] == "Table" && table["key"] == "EntryKey" && !table.contains("source"),
              "String-table identity remains unresolved");
        auto numbered = table_text_value();
        numbered.patch(9, 3);
        check(decode(numbered, options).properties[field]["value"]["table"] == "Table_2",
              "Numbered string table FName");
        auto bad = table_text_value();
        bad.patch(9, -1);
        rejects([&] { decode(bad, options); }, "schema-context");
        bad = table_text_value();
        bad.patch(5, -1);
        rejects([&] { decode(bad, options); }, "schema-context");
        auto no_names = options;
        no_names.names = nullptr;
        rejects([&] { decode(table_text_value(), no_names); }, "schema-context");
        auto marker = invariant_text_value();
        marker.patch(5, 2);
        rejects([&] { decode(marker, options); }, "schema-value");
        for (int history : {3, -2}) {
            Bytes b;
            b.i(0);
            b.put<std::int8_t>(static_cast<std::int8_t>(history));
            rejects([&] { decode(b, options); }, "schema-type");
        }
        Bytes unicode;
        unicode.i(0);
        unicode.put<std::int8_t>(-1);
        unicode.i(1);
        unicode.i(-4);
        unicode.put<std::uint16_t>(0xe9);
        unicode.put<std::uint16_t>(0xd83d);
        unicode.put<std::uint16_t>(0xde00);
        unicode.put<std::uint16_t>(0);
        check(decode(unicode, options).properties[field]["value"]["source"] == "\xc3\xa9\xf0\x9f\x98\x80",
              "UTF16 source preserves non-ASCII and surrogate pair");
        auto invalid_utf16 = unicode;
        invalid_utf16.data[invalid_utf16.data.size() - 3] = 0;
        rejects([&] { decode(invalid_utf16, options); }, "schema-value");
        auto invalid_string = invariant_text_value();
        invalid_string.data.back() = 1;
        rejects([&] { decode(invalid_string, options); }, "schema-value");
        Bytes zero;
        zero.put<std::uint16_t>(0x0380);
        zero.put<std::uint8_t>(1);
        auto cleared = decode_unversioned(zero.data, 0, zero.data.size(), schemas, "Collections");
        check(cleared.consumed_bytes == 3 &&
                  cleared.properties[field]["value"] ==
                      Json{{"flags", 0}, {"history", -1}, {"kind", "none"}, {"source", ""}},
              "Zero-masked text consumes no payload");
        auto limited = options;
        limited.max_values = 3;
        rejects([&] { decode(localized, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 2;
        rejects([&] { decode(localized, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = 8;
        rejects([&] { decode(localized, limited); }, "schema-limit");
        limited = options;
        int polls = 0;
        limited.control.cancelled = [&] { return ++polls > 12; };
        rejects([&] { decode(localized, limited); }, "cancelled");
        auto array_schemas = UnversionedSchemas::parse(
            collection_schema_fixture(collection_type(8, collection_type(11))).data);
        Bytes list;
        list.i(2);
        list.append(localized);
        list.append(table_text_value());
        auto bytes = collection_single(list);
        auto array =
            decode_unversioned(bytes.data, 0, bytes.data.size(), array_schemas, "Collections", options);
        check(array.properties[field]["value"].size() == 2 && array.consumed_bytes == bytes.data.size(),
              "Nested text arrays share the bounded decoder");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        const auto package =
            ZenPackage::parse(collection_zen_fixture(collection_single(localized)).data, zen);
        check(package.property_exports_decoded == 2, "UE5.3 profile selects the text layout explicitly");
        auto changed = ZenPackage::parse(
            collection_zen_fixture(collection_single(text_value("Edited source"))).data, zen);
        bool precise = false;
        const auto difference = compare_zen_packages(package, changed);
        for (const auto &change : difference["changes"])
            if (change["path"] ==
                "/objects/~1Game~1Example.Example/properties/~1Script~1Test.Collections.Lookup/value/source")
                precise = change["before"] == "Original source" && change["after"] == "Edited source";
        check(precise, "Text source diff uses the named source field");
        auto truncated = localized;
        truncated.data.pop_back();
        auto opaque = ZenPackage::parse(collection_zen_fixture(collection_single(truncated)).data, zen);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties"),
              "Invalid text discards the full bound property read");
        auto versioned = collection_zen_fixture(collection_single(invariant_text_value()), false, true);
        const std::uint32_t editor_words[] = {0xe4b068ed, 0xf49442e9, 0xa231da0b, 0x2e46bb41};
        for (unsigned i = 0; i < 4; ++i)
            std::memcpy(versioned.data.data() + 72 + i * 4, &editor_words[i], 4);
        versioned.patch(88, 32);
        check(ZenPackage::parse(versioned.data, zen).property_exports_decoded == 2,
              "Explicit modern editor custom version certifies invariant marker");
        versioned.patch(88, 31);
        auto old_editor = ZenPackage::parse(versioned.data, zen);
        check(old_editor.exports[0].property_read["status"] == "opaque",
              "Explicit old editor custom version never uses the modern layout");
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(localized), true).data, zen)
                      .property_exports_decoded == 2,
              "UE5.5 text layout");
        std::cout << checks << " unversioned text checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
