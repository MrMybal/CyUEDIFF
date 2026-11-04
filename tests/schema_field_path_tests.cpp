#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "schema_path_fixture.hpp"
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
    throw std::runtime_error("Expected field path refusal");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            write_path_fixtures(fs::u8path(argv[2]));
            return 0;
        }
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_type(27)).data);
        UnversionedReadOptions options;
        options.field_path_layout = FieldPathLayout::owner_and_names;
        const std::vector<std::string> names = {"None", "/Script/Test", "Outer", "Inner"};
        options.names = &names;
        options.reference = [](std::int32_t index) -> Json {
            return {{"index", index}, {"path", "/Script/Test.Owner"}};
        };
        auto decode = [&](const Bytes &payload, const UnversionedSchemas &schema,
                          const UnversionedReadOptions &opts) {
            auto bytes = collection_single(payload);
            return decode_unversioned(bytes.data, 0, bytes.data.size(), schema, "Collections", opts);
        };
        const auto wire = field_path_value();
        auto decoded = decode(wire, schemas, options);
        check(decoded.properties[field]["value"] ==
                  Json{{"kind", "field-path"},
                       {"path", {"Inner", "Outer"}},
                       {"owner", {{"index", 1}, {"path", "/Script/Test.Owner"}}}},
              "Field path retains serialized segment order and owner identity");
        check(decoded.consumed_bytes == 26, "Exact path count/name/owner window");
        auto numbered = wire;
        numbered.patch(8, 4);
        check(decode(numbered, schemas, options).properties[field]["value"]["path"][0] == "Inner_3",
              "Field path numeric FName suffix");
        const auto imported = decode(field_path_value({3}, -2), schemas, options).properties[field]["value"];
        check(imported["owner"]["index"] == -2, "Signed imported owner uses explicit resolver");
        auto null = decode(field_path_value({3, 2}, 0), schemas, options).properties[field]["value"];
        check(null["owner"].is_null() && null["path"].size() == 2,
              "Null owner never discards serialized segments");
        auto none = decode(field_path_value({0}, 0), schemas, options).properties[field]["value"];
        check(none["path"] == Json::array({"None"}), "Stored None path is not normalized to runtime empty");
        auto noNames = options;
        noNames.names = nullptr;
        noNames.reference = {};
        auto empty = decode(field_path_value({}, 0), schemas, noNames);
        check(empty.properties[field]["value"]["path"].empty() && empty.consumed_bytes == 10,
              "Empty null path needs no external tables but consumes owner slot");
        rejects([&] { decode(wire, schemas, {}); }, "schema-context");
        rejects([&] { decode(wire, schemas, noNames); }, "schema-context");
        auto noResolver = options;
        noResolver.reference = {};
        rejects([&] { decode(wire, schemas, noResolver); }, "schema-context");
        for (std::size_t end = 0; end < wire.data.size(); ++end) {
            auto cut = wire;
            cut.data.resize(end);
            rejects([&] { decode(cut, schemas, options); }, "bounds");
        }
        auto tail = wire;
        tail.zeros(11);
        check(decode(tail, schemas, options).consumed_bytes == wire.data.size() + 2,
              "Trailing native bytes remain unconsumed");
        for (int count : {-1, 100001}) {
            Bytes bad;
            bad.i(count);
            rejects([&] { decode(bad, schemas, options); }, "schema-limit");
        }
        for (int index : {-1, 4}) {
            auto bad = wire;
            bad.patch(4, index);
            rejects([&] { decode(bad, schemas, options); }, "schema-context");
        }
        auto bad = wire;
        bad.patch(8, -1);
        rejects([&] { decode(bad, schemas, options); }, "schema-context");
        Bytes zero;
        zero.put<std::uint16_t>(0x0380);
        zero.put<std::uint8_t>(1);
        auto masked = decode_unversioned(zero.data, 0, zero.data.size(), schemas, "Collections");
        check(masked.consumed_bytes == 3 && masked.properties[field]["serializedZero"] == true &&
                  masked.properties[field]["value"] ==
                      Json{{"kind", "field-path"}, {"path", Json::array()}, {"owner", nullptr}},
              "Zero mask has empty path/null owner without payload or maps");
        auto lists = UnversionedSchemas::parse(
            collection_schema_fixture(collection_type(8, collection_type(27))).data);
        Bytes list;
        list.i(2);
        list.append(wire);
        list.append(field_path_value({}, 0));
        check(decode(list, lists, options).properties[field]["value"][1]["path"].empty(),
              "Nested array field paths");
        auto optional = UnversionedSchemas::parse(
            collection_schema_fixture(collection_type(28, collection_type(27))).data);
        auto optionalOptions = options;
        optionalOptions.optional_layout = OptionalLayout::bool32_value;
        check(decode(optional_value(wire), optional, optionalOptions)
                      .properties[field]["value"]["value"]["path"][0] == "Inner",
              "Optional field path shares explicit layout");
        auto maps = UnversionedSchemas::parse(
            collection_schema_fixture(map_type(collection_type(27), collection_type(2))).data);
        Bytes map;
        map.i(0);
        map.i(1);
        map.append(wire);
        map.i(17);
        auto mapped = decode(map, maps, options).properties[field]["value"]["entries"];
        check(mapped.size() == 1 && mapped.begin().value()["key"]["path"][0] == "Inner",
              "Canonical map key retains owner and segments");
        auto limited = options;
        limited.max_values = 3;
        rejects([&] { decode(wire, schemas, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 2;
        rejects([&] { decode(wire, schemas, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = field.size() + 1;
        rejects([&] { decode(wire, schemas, limited); }, "schema-limit");
        unsigned checkpoints = 0;
        limited = options;
        limited.control.cancelled = [&] { return checkpoints != 0; };
        limited.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "unversioned-field-path")
                ++checkpoints;
        };
        rejects([&] { decode(wire, schemas, limited); }, "cancelled");
        check(checkpoints == 1, "Cancellation applies between path segments");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto before = ZenPackage::parse(collection_zen_fixture(collection_single(wire)).data, zen);
        check(before.property_exports_decoded == 2, "UE5.3 profile supplies owner/name field layout");
        auto edited = field_path_value({2, 2}, 2);
        auto after = ZenPackage::parse(collection_zen_fixture(collection_single(edited)).data, zen);
        const auto diff = compare_zen_packages(before, after);
        bool segment = false, owner = false;
        for (const auto &change : diff["changes"]) {
            const auto path = change["path"].get<std::string>();
            if (path.find("/value/path/0") != std::string::npos)
                segment = change["before"] == "Nested" && change["after"] == "Example";
            if (path.find("/value/owner/path") != std::string::npos)
                owner = change["before"] == "/Game/Example.Example" &&
                        change["after"] == "/Game/Example.Example.Nested";
        }
        check(segment && owner, "Field segment and owner changes have distinct precise diff paths");
        auto invalid = field_path_value({3}, 999);
        auto opaque = ZenPackage::parse(collection_zen_fixture(collection_single(invalid)).data, zen);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties"),
              "Invalid owner discards entire bound read");
        auto versioned = collection_zen_fixture(collection_single(wire), false, true);
        const std::uint32_t guids[][4] = {{0x9c54d522, 0xa8264fbe, 0x94210746, 0x61b482d0},
                                          {0x601d1886, 0xac644f84, 0xaa16d3de, 0x0deac7d6}};
        for (unsigned kind = 0; kind < 2; ++kind) {
            for (unsigned i = 0; i < 4; ++i)
                std::memcpy(versioned.data.data() + 72 + i * 4, &guids[kind][i], 4);
            const auto threshold = kind ? 33 : 30;
            versioned.patch(88, threshold);
            check(ZenPackage::parse(versioned.data, zen).property_exports_decoded == 2,
                  "Modern custom version certifies owner slot");
            versioned.patch(88, threshold - 1);
            auto old = ZenPackage::parse(versioned.data, zen);
            check(old.exports[0].property_read["status"] == "opaque" &&
                      old.exports[0].property_read["error"] == "schema-context",
                  "Explicit legacy field path version stays opaque");
        }
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(wire), true).data, zen)
                      .property_exports_decoded == 2,
              "UE5.5 profile supplies owner/name field layout");
        zen.max_property_values = 3;
        rejects([&] { ZenPackage::parse(collection_zen_fixture(collection_single(wire), true).data, zen); },
                "schema-limit");
        std::cout << checks << " unversioned field path checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
