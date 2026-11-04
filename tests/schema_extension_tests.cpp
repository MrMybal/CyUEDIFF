#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "schema_extension_fixture.hpp"
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
        throw std::runtime_error("Expected schema extension error");
    } catch (const Error &e) {
        if (e.code != code)
            throw std::runtime_error("Expected " + std::string(code) + ", got " + e.code + ": " + e.what());
        ++checks;
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        auto fixture = extended_schema_fixture();
        auto schemas = UnversionedSchemas::parse(fixture.data);
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto root = fs::u8path(argv[2]);
            fs::create_directories(root);
            fixture.save(root / "extended.usmap");
            ambiguous_schema_fixture().save(root / "ambiguous.usmap");
            auto bad = append_schema_extensions(schema_fixture(), {{"WHAT", {}}});
            bad.save(root / "unsupported.usmap");
            Json context = {
                {"format", "cyuediff.zen-schema/1"},
                {"profile", "ue5.3"},
                {"usmap", "extended.usmap"},
                {"sha256", schemas.sha256()},
                {"bindings",
                 {{"/Script/Test.DemoAsset", {{"schema", "/Script/Test.DemoAsset"}, {"offset", 0}}}}}};
            std::ofstream(root / "extended.json") << context.dump(2);
            return 0;
        }
        const auto &asset = schemas.schema("DemoAsset");
        check(asset.path == "/Script/Test.DemoAsset" && asset.module == "/Script/Test",
              "PPTH preserves schema module identity");
        check(&asset == &schemas.schema("/Script/Test.DemoAsset"),
              "Unique short names resolve to qualified definitions");
        check(asset.super == "/Script/Test.Base", "Base names resolve after module extensions");
        check(asset.attributes_present && asset.struct_kind == 2 && asset.flags == 0x80000001u,
              "EATR preserves class kind and full flags");
        check(asset.serialized_property_flags.size() == 8 &&
                  asset.serialized_property_flags[7] == 0x8000000000000007ull,
              "EATR raw flag sequence preserves high bits");
        auto report = schemas.to_json();
        check(report["extensions"] == Json::array({"PPTH", "EATR", "ENVP"}),
              "Extension inventory retains original order");
        check(report["schemas"][1]["serializedPropertyFlagsHex"][0] == "8000000000000000",
              "JSON property flags are lossless hex strings");
        const auto &enumeration = schemas.enumeration("ModeEnum");
        check(enumeration.path == "/Script/Test.ModeEnum" && enumeration.attributes_present &&
                  enumeration.flags == 3,
              "PPTH and EATR preserve enum identity/flags");
        check(enumeration.values[0]["value"] == -5 && enumeration.values[1]["value"] == 9223372036854775807ll,
              "ENVP replaces ordinal values with signed 64-bit pairs");
        auto reordered = append_schema_extensions(
            schema_fixture(4, false, true),
            {{"ENVP", schema_envp()}, {"EATR", schema_eatr()}, {"PPTH", schema_ppth()}});
        check(UnversionedSchemas::parse(reordered.data).to_json()["enums"] == report["enums"],
              "Extension order does not change identities/values");
        auto legacy = UnversionedSchemas::parse(
            append_schema_extensions(schema_fixture(), {{"MODL", schema_modl()}}, true).data);
        check(legacy.schema("DemoAsset").path == "/Script/Module0.DemoAsset", "Legacy MODL module identity");
        auto modern = UnversionedSchemas::parse(
            append_schema_extensions(schema_fixture(), {{"MODL", schema_modl()}}).data);
        check(modern.schema("DemoAsset").path == legacy.schema("DemoAsset").path,
              "CEXT MODL is equivalent to legacy layout");
        auto wide = UnversionedSchemas::parse(
            append_schema_extensions(schema_fixture(), {{"MODL", schema_modl(256)}}).data);
        check(wide.schema("Base").path == "/Script/Module255.Base",
              "MODL switches to 16-bit indices above 255 modules");
        auto homonyms = UnversionedSchemas::parse(ambiguous_schema_fixture().data);
        check(homonyms.schema("/Script/A.Twin").properties.at(0).type.kind == 2 &&
                  homonyms.schema("/Script/B.Twin").properties.at(0).type.kind == 1,
              "Homonymous schemas retain different layouts");
        check(homonyms.schema("Child").super == "/Script/A.Twin",
              "Explicit qualified base is preserved across modules");
        check(homonyms.enumeration("/Script/A.Mode").values[0]["value"] == 7 &&
                  homonyms.enumeration("/Script/B.Mode").values[0]["value"] == 8,
              "Homonymous enums retain distinct values");
        error([&] { homonyms.schema("Twin"); }, "schema-ambiguous");
        error([&] { homonyms.enumeration("Mode"); }, "schema-ambiguous");
        error([&] { UnversionedSchemas::parse(ambiguous_schema_fixture(true, true).data); },
              "schema-ambiguous");
        error([&] { UnversionedSchemas::parse(ambiguous_schema_fixture(false).data); }, "schema-format");
        error([&] { UnversionedSchemas::parse(ambiguous_schema_fixture(true, false, true).data); },
              "schema-format");
        auto homonym_chain = UnversionedSchemas::parse(ambiguous_schema_fixture(true, false, false, 1).data);
        check(homonym_chain.schema("/Script/B.Twin").super == "/Script/A.Twin",
              "Homonymous types can form a valid qualified inheritance chain");
        Bytes inherited;
        inherited.put<std::uint16_t>(0x0500);
        inherited.put<std::uint8_t>(1);
        inherited.i(42);
        auto inherited_values =
            decode_unversioned(inherited.data, 0, inherited.data.size(), homonym_chain, "/Script/B.Twin");
        check(inherited_values.properties["/Script/B.Twin.Value"]["value"] == true &&
                  inherited_values.properties["/Script/A.Twin.Value"]["value"] == 42,
              "Homonymous declaring types retain distinct inherited property keys");
        error([&] { UnversionedSchemas::parse(ambiguous_schema_fixture(true, false, false, 2).data); },
              "schema-format");
        error([&] { UnversionedSchemas::parse(ambiguous_schema_fixture(true, false, false, 3).data); },
              "schema-format");
        auto base = schema_fixture(4, false, true);
        error(
            [&] {
                UnversionedSchemas::parse(
                    append_schema_extensions(base, {{"PPTH", schema_ppth()}, {"PPTH", schema_ppth()}}).data);
            },
            "schema-extension");
        error(
            [&] {
                UnversionedSchemas::parse(
                    append_schema_extensions(base, {{"PPTH", schema_ppth()}, {"MODL", schema_modl()}}).data);
            },
            "schema-extension");
        error([&] { UnversionedSchemas::parse(append_schema_extensions(base, {{"WHAT", {}}}).data); },
              "schema-extension");
        for (const auto &id : {std::string("PPTH"), std::string("EATR"), std::string("ENVP")}) {
            Bytes bytes = id == "PPTH" ? schema_ppth() : (id == "EATR" ? schema_eatr() : schema_envp());
            bytes.data[0] = 1;
            error([&] { UnversionedSchemas::parse(append_schema_extensions(base, {{id, bytes}}).data); },
                  "schema-extension");
        }
        auto malformed = schema_ppth();
        malformed.patch(1, 0);
        error([&] { UnversionedSchemas::parse(append_schema_extensions(base, {{"PPTH", malformed}}).data); },
              "schema-format");
        malformed = schema_ppth();
        malformed.patch(5, 999);
        error([&] { UnversionedSchemas::parse(append_schema_extensions(base, {{"PPTH", malformed}}).data); },
              "schema-format");
        malformed = schema_eatr();
        malformed.data[13] = 3;
        error([&] { UnversionedSchemas::parse(append_schema_extensions(base, {{"EATR", malformed}}).data); },
              "schema-format");
        malformed = schema_modl();
        malformed.data.back() = 1;
        error([&] { UnversionedSchemas::parse(append_schema_extensions(base, {{"MODL", malformed}}).data); },
              "schema-format");
        std::string invalid_tag("ABCD");
        invalid_tag[1] = static_cast<char>(0xff);
        error([&] { UnversionedSchemas::parse(append_schema_extensions(base, {{invalid_tag, {}}}).data); },
              "schema-format");
        auto short_payload = schema_ppth();
        short_payload.data.pop_back();
        error(
            [&] {
                UnversionedSchemas::parse(
                    append_schema_extensions(base, {{"PPTH", short_payload}, {"ENVP", schema_envp()}}).data);
            },
            "bounds");
        auto trailing = schema_ppth();
        trailing.put<std::uint8_t>(0);
        error([&] { UnversionedSchemas::parse(append_schema_extensions(base, {{"PPTH", trailing}}).data); },
              "schema-format");
        bool rejected = true;
        for (std::size_t end = base.data.size() + 1; end < fixture.data.size(); ++end) {
            auto cut = fixture;
            cut.data.resize(end);
            cut.patch(8, static_cast<int>(end - 16));
            cut.patch(12, static_cast<int>(end - 16));
            try {
                UnversionedSchemas::parse(cut.data);
                rejected = false;
            } catch (const Error &) {
            }
        }
        check(rejected, "Every truncated extension payload is rejected with corrected outer sizes");
        auto cancelled = SchemaReadOptions{};
        cancelled.control.cancelled = [] { return true; };
        error([&] { UnversionedSchemas::parse(fixture.data, cancelled); }, "cancelled");
        bool stop = false;
        cancelled.control.cancelled = [&] { return stop; };
        cancelled.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "schema-extension")
                stop = true;
        };
        error([&] { UnversionedSchemas::parse(fixture.data, cancelled); }, "cancelled");
        SchemaReadOptions limited;
        limited.max_text_bytes = 20;
        error([&] { UnversionedSchemas::parse(fixture.data, limited); }, "schema-limit");
        UnversionedReadOptions values_options;
        std::vector<std::string> names = {"None", "/Game/Example", "Example", "Nested"};
        values_options.names = &names;
        values_options.reference = [](std::int32_t n) -> Json { return n; };
        auto body = schema_values();
        auto decoded = decode_unversioned(body.data, 0, body.data.size(), schemas, "/Script/Test.DemoAsset",
                                          values_options);
        check(decoded.properties["/Script/Test.DemoAsset.Health"]["value"] == 42 &&
                  decoded.properties["/Script/Test.Base.Enabled"]["value"] == true,
              "Declaring module paths survive property decoding");
        check(decoded.properties["/Script/Test.DemoAsset.Mode"]["typeName"] == "/Script/Test.ModeEnum",
              "Enum property exposes qualified type");
        Bytes nested;
        nested.put<std::uint16_t>(0x0300);
        nested.put<std::uint16_t>(0x0300);
        nested.i(42);
        error(
            [&] {
                decode_unversioned(nested.data, 0, nested.data.size(), schemas, "Record", values_options);
            },
            "schema-type");
        values_options.reflected_structs.insert("/Script/Test.Nested");
        decoded = decode_unversioned(nested.data, 0, nested.data.size(), schemas, "Record", values_options);
        check(
            decoded.properties["/Script/Test.Record.Inner"]["value"]["/Script/Test.Nested.Inner"]["value"] ==
                42,
            "Qualified explicit reflected struct binding");
        auto context = std::make_shared<ZenSchemaContext>();
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->profile = ZenProfile::unreal_5_3;
        context->bindings["/Script/Test.DemoAsset"] = {"/Script/Test.DemoAsset", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto package = ZenPackage::parse(schema_zen_fixture().data, zen);
        check(package.property_exports_decoded == 2, "Zen accepts a qualified extended schema");
        auto diff = compare_zen_packages(package, ZenPackage::parse(schema_zen_fixture(99).data, zen));
        bool found = false;
        for (const auto &change : diff["changes"])
            if (change["path"] ==
                "/objects/~1Game~1Example.Example/properties/~1Script~1Test.DemoAsset.Health/value")
                found = change["before"] == 42 && change["after"] == 99;
        check(found, "Qualified property names yield escaped, precise diff paths");
        std::cout << checks << " schema extension checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
