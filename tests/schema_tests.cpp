#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "schema_fixture.hpp"
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
        throw std::runtime_error("Expected schema error");
    } catch (const Error &e) {
        if (e.code != code)
            throw std::runtime_error("Expected " + std::string(code) + ", got " + e.code + ": " + e.what());
        ++checks;
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        auto mappings = UnversionedSchemas::parse(schema_fixture().data);
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto root = fs::u8path(argv[2]);
            fs::create_directories(root);
            schema_fixture().save(root / "test.usmap");
            auto payload = schema_zen_fixture();
            payload.save(root / "schema.ucas");
            zen_toc(payload).save(root / "schema.utoc");
            payload = schema_zen_fixture(99);
            payload.save(root / "changed.ucas");
            zen_toc(payload).save(root / "changed.utoc");
            auto scripts = script_fixture();
            scripts.save(root / "global.ucas");
            zen_toc(scripts, 5).save(root / "global.utoc");
            Json manifest = {
                {"format", "cyuediff.zen-schema/1"},
                {"profile", "ue5.3"},
                {"usmap", "test.usmap"},
                {"sha256", mappings.sha256()},
                {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "DemoAsset"}, {"offset", 0}}}}}};
            std::ofstream(root / "schema.json") << manifest.dump(2);
            return 0;
        }
        for (unsigned v = 0; v <= 4; ++v) {
            auto schemas = UnversionedSchemas::parse(schema_fixture(v, v > 0).data);
            check(schemas.schema("DemoAsset").properties.size() == 8, "Static arrays expand slots");
            check(schemas.to_json()["enums"]["ModeEnum"][1]["value"] == (v >= 4 ? 7 : 1),
                  "Enum version layouts");
        }
        check(mappings.schema("DemoAsset").super == "Base", "Inheritance metadata");
        error([&] { mappings.schema("Missing"); }, "schema-missing");
        UnversionedReadOptions options;
        std::vector<std::string> names = {"None", "/Game/Example", "Example", "Nested"};
        options.names = &names;
        options.reference = [](std::int32_t i) -> Json { return {{"import", -i - 1}}; };
        auto body = schema_values();
        auto decoded = decode_unversioned(body.data, 0, body.data.size(), mappings, "DemoAsset", options);
        const auto &props = decoded.properties;
        check(decoded.consumed_bytes == body.data.size(), "Exact property bytes consumed");
        check(props["DemoAsset.Health"]["value"] == 42, "Signed integer");
        check(props["DemoAsset.Values"]["value"] == Json::array({-12, 34}), "Typed array values");
        check(props["DemoAsset.Target"]["value"]["import"] == 0, "Signed object reference resolver");
        check(props["DemoAsset.Label"]["value"] == "Example_2", "FName package map and number");
        check(props["DemoAsset.Title"]["value"] == "A\xf0\x9f\x98\x80", "UTF-16 surrogate pair");
        check(props["DemoAsset.Samples[0]"]["value"] == 0 &&
                  props["DemoAsset.Samples[0]"]["serializedZero"] == true,
              "Serialized static-array zero");
        check(props["DemoAsset.Samples[1]"]["value"] == 2.5, "Unmasked static-array value");
        check(props["DemoAsset.Mode"]["value"] == 0 && props["DemoAsset.Mode"]["typeName"] == "ModeEnum",
              "Enum underlying type");
        check(props["Base.Enabled"]["value"] == true, "Derived slots precede base slots");
        Bytes sparse;
        sparse.put<std::uint16_t>((1u << 9) | 256u | 8u);
        sparse.put<std::uint8_t>(1);
        decoded = decode_unversioned(sparse.data, 0, sparse.data.size(), mappings, "DemoAsset", options);
        check(decoded.properties.size() == 1 && decoded.properties.contains("Base.Enabled"),
              "Absent slots are not filled with defaults");
        Bytes wide;
        wide.put<std::uint16_t>((32u << 9) | 256u | 128u);
        wide.put<std::uint32_t>(0xffffffff);
        decoded = decode_unversioned(wide.data, 0, wide.data.size(), mappings, "Wide", options);
        check(decoded.properties.size() == 32 && decoded.consumed_bytes == 6,
              "Zero mask above 16 bits uses 32-bit word");
        Bytes fragmented;
        fragmented.put<std::uint16_t>((1u << 9) | 128u);
        fragmented.put<std::uint16_t>((1u << 9) | 256u | 7u);
        fragmented.put<std::uint8_t>(1);
        fragmented.put<std::uint8_t>(1);
        decoded =
            decode_unversioned(fragmented.data, 0, fragmented.data.size(), mappings, "DemoAsset", options);
        check(decoded.properties.size() == 2 && decoded.properties["Base.Enabled"]["value"] == true,
              "Mask cursor skips unmasked fragment values");
        Bytes nested;
        nested.put<std::uint16_t>((1u << 9) | 256u);
        nested.put<std::uint16_t>((1u << 9) | 256u);
        nested.i(81);
        error([&] { decode_unversioned(nested.data, 0, nested.data.size(), mappings, "Record", options); },
              "schema-type");
        options.reflected_structs.insert("Nested");
        decoded = decode_unversioned(nested.data, 0, nested.data.size(), mappings, "Record", options);
        check(decoded.properties["Record.Inner"]["value"]["Nested.Inner"]["value"] == 81,
              "Explicit reflected struct uses nested header");
        error(
            [&] { decode_unversioned(nested.data, 0, nested.data.size(), mappings, "Unsupported", options); },
            "schema-context");
        for (std::size_t end = 0; end < body.data.size(); ++end)
            error([&] { decode_unversioned(body.data, 0, end, mappings, "DemoAsset", options); }, "bounds");
        auto wrong = body;
        wrong.data[0] = 127;
        error([&] { decode_unversioned(wrong.data, 0, wrong.data.size(), mappings, "DemoAsset", options); },
              "schema-mismatch");
        auto small = options;
        small.max_values = 2;
        error([&] { decode_unversioned(body.data, 0, body.data.size(), mappings, "DemoAsset", small); },
              "schema-limit");
        small = options;
        small.max_text_bytes = 2;
        error([&] { decode_unversioned(body.data, 0, body.data.size(), mappings, "DemoAsset", small); },
              "schema-limit");
        small = options;
        small.control.cancelled = [] { return true; };
        error([&] { decode_unversioned(body.data, 0, body.data.size(), mappings, "DemoAsset", small); },
              "cancelled");
        auto malformed = schema_fixture();
        malformed.data[0] = 0;
        error([&] { UnversionedSchemas::parse(malformed.data); }, "schema-format");
        malformed = schema_fixture();
        malformed.data[2] = 5;
        error([&] { UnversionedSchemas::parse(malformed.data); }, "schema-format");
        malformed = schema_fixture();
        malformed.data.push_back(0);
        malformed.patch(8, static_cast<int>(malformed.data.size() - 16));
        malformed.patch(12, static_cast<int>(malformed.data.size() - 16));
        error([&] { UnversionedSchemas::parse(malformed.data); }, "schema-extension");
        SchemaReadOptions schema_options;
        schema_options.max_bytes = 8;
        error([&] { UnversionedSchemas::parse(schema_fixture().data, schema_options); }, "schema-limit");
        schema_options = {};
        schema_options.max_entries = 3;
        error([&] { UnversionedSchemas::parse(schema_fixture().data, schema_options); }, "schema-limit");
        // Compression is delegated by name. This identity test verifies dispatch and
        // bounds only; it does not claim to validate an Oodle/Brotli/Zstd codec.
        auto compressed = schema_fixture();
        compressed.data[7] = 1;
        error([&] { UnversionedSchemas::parse(compressed.data); }, "schema-codec");
        SchemaReadOptions supplied;
        bool dispatched = false;
        supplied.decoder = [&](const std::string &method, const std::uint8_t *src, std::size_t source_size,
                               std::uint8_t *out, std::size_t output_size) {
            dispatched = method == "Oodle" && source_size == output_size;
            std::memcpy(out, src, std::min(source_size, output_size));
            return source_size;
        };
        check(UnversionedSchemas::parse(compressed.data, supplied).schema("DemoAsset").slots == 8 &&
                  dispatched,
              "Explicit named USMAP codec dispatch");
        supplied.decoder = [](const std::string &, const std::uint8_t *, std::size_t, std::uint8_t *,
                              std::size_t) { return std::size_t(0); };
        error([&] { UnversionedSchemas::parse(compressed.data, supplied); }, "schema-format");
        auto full = schema_fixture().data;
        bool all_truncations_rejected = true;
        for (std::size_t end = 0; end < full.size(); ++end) {
            try {
                UnversionedSchemas::parse({full.begin(), full.begin() + static_cast<std::ptrdiff_t>(end)});
                all_truncations_rejected = false;
            } catch (const Error &) {
            }
        }
        check(all_truncations_rejected, "Every truncated USMAP prefix is rejected");
        auto charged_options = options;
        unsigned failed_work = 0;
        charged_options.charge = [&](std::uint32_t work, std::uint64_t) { failed_work += work; };
        error(
            [&] {
                decode_unversioned(nested.data, 0, nested.data.size(), mappings, "Unsupported",
                                   charged_options);
            },
            "schema-context");
        check(failed_work > 0, "Enclosing work budget includes discarded failed reads");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(mappings);
        context->bindings["/Script/Test.DemoAsset"] = {"DemoAsset", 0};
        ZenReadOptions zen;
        zen.profile = ZenProfile::unreal_5_3;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto package = ZenPackage::parse(schema_zen_fixture().data, zen);
        check(package.property_exports_decoded == 2 && package.property_exports_failed == 0,
              "Explicit Zen class binding");
        check(package.exports[0].property_read["properties"]["DemoAsset.Target"]["value"]["path"] ==
                  "/Script/Test.DemoAsset",
              "Zen property import maps to global script path");
        check(package.snapshot()["objects"]["/Game/Example.Example"]["properties"]["DemoAsset.Health"]
                                ["value"] == 42,
              "Typed properties included in snapshot");
        auto diff = compare_zen_packages(package, ZenPackage::parse(schema_zen_fixture(99).data, zen));
        bool health = false;
        for (const auto &change : diff["changes"])
            if (change["path"] == "/objects/~1Game~1Example.Example/properties/DemoAsset.Health/value")
                health = change["before"] == 42 && change["after"] == 99;
        check(health && diff["binaryChanged"] == true,
              "Zen diff names changed property and preserves binary evidence");
        auto old = ZenPackage::parse(zen_fixture().data, zen);
        check(old.property_exports_failed == 2 && old.exports[0].property_read["status"] == "opaque",
              "Wrong property region retains opaque export");
        check(!old.exports[0].sha256.empty(), "Opaque fallback keeps complete body hash");
        auto tiny = zen;
        tiny.max_property_values = 2;
        error([&] { ZenPackage::parse(schema_zen_fixture().data, tiny); }, "schema-limit");
        context->profile = ZenProfile::unreal_5_5;
        error([&] { ZenPackage::parse(schema_zen_fixture().data, zen); }, "schema-context");
        std::cout << checks << " schema checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
