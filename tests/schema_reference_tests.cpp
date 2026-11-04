#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "schema_collection_fixture.hpp"
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
        throw std::runtime_error("Expected reference error");
    } catch (const Error &e) {
        if (e.code != code)
            throw std::runtime_error(e.code + ": " + e.what());
        ++checks;
    }
}
} // namespace
int main() {
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schema = [&](unsigned kind) {
            return UnversionedSchemas::parse(collection_schema_fixture(collection_type(kind)).data);
        };
        auto soft = schema(17), interface = schema(12), lazy = schema(15);
        std::vector<std::string> names = {"None", "/Game/Reference", "Asset", "Other"};
        UnversionedReadOptions options;
        options.names = &names;
        auto decode = [&](const Bytes &values, const UnversionedSchemas &schemas,
                          const UnversionedReadOptions &opts) {
            auto wire = collection_single(values);
            return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", opts);
        };
        Bytes path;
        collection_name(path, 1);
        collection_name(path, 2);
        path.string("Component.Child");
        error([&] { decode(path, soft, options); }, "schema-context");
        options.soft_path_layout = SoftPathLayout::ue5_top_level_names;
        auto decoded = decode(path, soft, options);
        check(decoded.properties[field]["value"]["packageName"] == "/Game/Reference", "Soft package name");
        check(decoded.properties[field]["value"]["assetName"] == "Asset" &&
                  decoded.properties[field]["value"]["subPath"] == "Component.Child",
              "Soft asset and subobject remain distinct");
        check(decoded.consumed_bytes == path.data.size() + 2, "Exact soft path window");
        auto numbered = path;
        numbered.patch(12, 4);
        check(decode(numbered, soft, options).properties[field]["value"]["assetName"] == "Asset_3",
              "Numbered soft asset FName");
        Bytes unicode;
        collection_name(unicode, 1);
        collection_name(unicode, 2);
        unicode.i(-3);
        unicode.put<std::uint16_t>(0xe9);
        unicode.put<std::uint16_t>(0x3a9);
        unicode.put<std::uint16_t>(0);
        check(decode(unicode, soft, options).properties[field]["value"]["subPath"] == "\xc3\xa9\xce\xa9",
              "UTF-16 soft subpath");
        Bytes bad;
        collection_name(bad, 999);
        collection_name(bad, 2);
        bad.string("");
        error([&] { decode(bad, soft, options); }, "schema-context");
        bool truncated = true;
        for (std::size_t end = 0; end < path.data.size(); ++end) {
            auto cut = path;
            cut.data.resize(end);
            try {
                decode(cut, soft, options);
                truncated = false;
            } catch (const Error &) {
            }
        }
        check(truncated, "All truncated soft paths rejected");
        Bytes object;
        object.i(-2);
        error([&] { decode(object, interface, options); }, "schema-context");
        options.reference = [](std::int32_t index) -> Json {
            return {{"index", index}, {"path", "/Game/Reference.Asset"}};
        };
        check(decode(object, interface, options).properties[field]["value"]["object"]["index"] == -2,
              "Interface uses signed package index only");
        Bytes null;
        null.i(0);
        check(decode(null, interface, options).properties[field]["value"]["object"].is_null(),
              "Null interface");
        Bytes guid;
        guid.put<std::uint32_t>(0x01234567);
        guid.put<std::uint32_t>(0x89abcdef);
        guid.put<std::uint32_t>(0x10203040);
        guid.put<std::uint32_t>(0xffeeddcc);
        auto id = decode(guid, lazy, options);
        check(id.properties[field]["value"]["guid"] == "0123456789abcdef10203040ffeeddcc",
              "Lazy GUID preserves four serialized words");
        check(id.consumed_bytes == 18, "Lazy GUID window");
        for (unsigned kind : {12u, 15u, 17u}) {
            Bytes zero;
            zero.put<std::uint16_t>(0x0380);
            zero.put<std::uint8_t>(1);
            auto schemas = schema(kind);
            auto result = decode_unversioned(zero.data, 0, zero.data.size(), schemas, "Collections");
            check(result.consumed_bytes == 3 && result.properties[field]["serializedZero"] == true,
                  "Zero reference consumes no payload");
        }
        auto limited = options;
        limited.max_text_bytes = 8;
        error([&] { decode(guid, lazy, limited); }, "schema-limit");
        limited = options;
        limited.max_values = 2;
        error([&] { decode(path, soft, limited); }, "schema-limit");
        limited = options;
        limited.control.cancelled = [] { return true; };
        error([&] { decode(path, soft, limited); }, "cancelled");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(soft);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        // Fixture package names 1/2 are /Game/Example and Example.
        auto package = ZenPackage::parse(collection_zen_fixture(collection_single(path)).data, zen);
        check(package.property_exports_decoded == 2 &&
                  package.exports[0].property_read["properties"][field]["value"]["assetName"] == "Example",
              "Zen profile supplies soft layout");
        auto change = path;
        change.patch(8, 3);
        auto difference = compare_zen_packages(
            package, ZenPackage::parse(collection_zen_fixture(collection_single(change)).data, zen));
        bool found = false;
        for (const auto &c : difference["changes"])
            if (c["path"] == "/objects/~1Game~1Example.Example/properties/~1Script~1Test.Collections.Lookup/"
                             "value/assetName")
                found = c["before"] == "Example" && c["after"] == "Nested";
        check(found, "Soft asset diff has precise member path");
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(path), true).data, zen)
                      .property_exports_decoded == 2,
              "UE5.5 soft layout");
        std::cout << checks << " unversioned reference checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
