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
        throw std::runtime_error("Expected collection error");
    } catch (const Error &e) {
        if (e.code != code)
            throw std::runtime_error("Expected " + std::string(code) + ", got " + e.code + ": " + e.what());
        ++checks;
    }
}
const std::string lookup = "/Script/Test.Collections.Lookup", tags = "/Script/Test.Collections.Tags",
                  after = "/Script/Test.Collections.After";
} // namespace
int main(int argc, char **argv) {
    try {
        auto mapping = collection_schema_fixture();
        auto schemas = UnversionedSchemas::parse(mapping.data);
        auto body = collection_values();
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto root = fs::u8path(argv[2]);
            fs::create_directories(root);
            mapping.save(root / "collections.usmap");
            for (const auto &record :
                 std::vector<std::pair<std::string, Bytes>>{{"collections", body},
                                                            {"reordered", collection_values(true)},
                                                            {"changed", collection_values(false, 99)}}) {
                auto package = collection_zen_fixture(record.second);
                package.save(root / (record.first + ".ucas"));
                zen_toc(package).save(root / (record.first + ".utoc"));
            }
            Bytes replacement;
            replacement.i(-1);
            replacement.i(1);
            collection_name(replacement, 2);
            replacement.i(77);
            auto replace_values = collection_single(replacement);
            auto package = collection_zen_fixture(replace_values, true);
            package.save(root / "replace.ucas");
            zen_toc(package).save(root / "replace.utoc");
            package = collection_zen_fixture(replace_values);
            package.save(root / "replace-legacy.ucas");
            zen_toc(package).save(root / "replace-legacy.utoc");
            Bytes duplicate;
            duplicate.i(0);
            duplicate.i(2);
            for (unsigned j = 0; j < 2; ++j) {
                collection_name(duplicate, 2);
                duplicate.i(42);
            }
            package = collection_zen_fixture(collection_single(duplicate));
            package.save(root / "duplicate.ucas");
            zen_toc(package).save(root / "duplicate.utoc");
            auto global = script_fixture();
            global.save(root / "global.ucas");
            zen_toc(global, 5).save(root / "global.utoc");
            Json context = {
                {"format", "cyuediff.zen-schema/1"},
                {"profile", "ue5.3"},
                {"usmap", "collections.usmap"},
                {"sha256", schemas.sha256()},
                {"bindings",
                 {{"/Script/Test.DemoAsset", {{"schema", "/Script/Test.Collections"}, {"offset", 0}}}}}};
            std::ofstream(root / "collections.json") << context.dump(2);
            context["profile"] = "ue5.5";
            std::ofstream(root / "modern.json") << context.dump(2);
            return 0;
        }
        UnversionedReadOptions options;
        std::vector<std::string> names = {"None", "/Game/Example", "Example", "Nested"};
        options.names = &names;
        auto decode = [&](const Bytes &bytes, const UnversionedSchemas &schema,
                          const UnversionedReadOptions &opts) {
            return decode_unversioned(bytes.data, 0, bytes.data.size(), schema, "Collections", opts);
        };
        auto decoded = decode(body, schemas, options);
        const auto &properties = decoded.properties;
        const auto &map = properties.at(lookup).at("value");
        const auto &set = properties.at(tags).at("value");
        check(decoded.consumed_bytes == body.data.size() && properties.at(after).at("value") == 123,
              "Map/set windows leave following scalar intact");
        check(map.at("kind") == "stored-map-delta" && map.at("replace") == false,
              "Map delta remains explicit");
        check(map.at("removed").at("\"Nested\"") == "Nested", "Map removal key retained");
        check(map.at("entries").at("\"Example\"").at("value") == 10, "Map pair key/value decoded");
        check(set.at("kind") == "stored-set-delta" && set.at("removed").at("99") == 99,
              "Set removal retained");
        check(set.at("entries").at("3") == 3 && set.at("entries").at("5") == 5,
              "Set additions keyed by canonical identity");
        check(decode(collection_values(true), schemas, options).properties == properties,
              "Map/set iteration order does not change properties");
        check(decode(collection_values(false, 99), schemas, options)
                      .properties.at(lookup)
                      .at("value")
                      .at("entries")
                      .at("\"None\"")
                      .at("value") == 99,
              "Changed value retains its key identity");
        Bytes zero;
        zero.put<std::uint16_t>(0x0780);
        zero.put<std::uint8_t>(3);
        zero.i(123);
        auto zeros = decode(zero, schemas, options);
        check(zeros.properties.at(lookup).at("value").at("replace") == true &&
                  zeros.properties.at(tags).at("value").at("replace") == true,
              "Zero-masked map/set explicitly clear previous contents");
        check(zeros.consumed_bytes == 7 && zeros.properties.at(after).at("value") == 123,
              "Zero containers consume no count bytes");
        Bytes replacement;
        replacement.i(-1);
        replacement.i(1);
        collection_name(replacement, 2);
        replacement.i(77);
        auto replacement_body = collection_single(replacement);
        error([&] { decode(replacement_body, schemas, options); }, "schema-type");
        auto modern_options = options;
        modern_options.allow_map_replacement = true;
        auto replaced = decode(replacement_body, schemas, modern_options);
        check(replaced.properties.at(lookup).at("value").at("replace") == true &&
                  replaced.properties.at(lookup).at("value").at("removed").empty(),
              "Explicit supported map replacement marker");
        Bytes malformed;
        malformed.i(-2);
        error([&] { decode(collection_single(malformed), schemas, options); }, "schema-value");
        malformed = {};
        malformed.i(-1);
        error([&] { decode(collection_single(malformed, true), schemas, modern_options); }, "schema-value");
        for (bool removals : {false, true}) {
            Bytes duplicate;
            duplicate.i(removals ? 2 : 0);
            if (!removals)
                duplicate.i(2);
            for (unsigned j = 0; j < 2; ++j) {
                collection_name(duplicate, 2);
                if (!removals)
                    duplicate.i(42);
            }
            error([&] { decode(collection_single(duplicate), schemas, options); }, "schema-identity");
            duplicate = {};
            duplicate.i(removals ? 2 : 0);
            if (!removals)
                duplicate.i(2);
            duplicate.i(7);
            duplicate.i(7);
            error([&] { decode(collection_single(duplicate, true), schemas, options); }, "schema-identity");
        }
        Bytes readd;
        readd.i(1);
        collection_name(readd, 2);
        readd.i(1);
        collection_name(readd, 2);
        readd.i(55);
        auto readded = decode(collection_single(readd), schemas, options).properties.at(lookup).at("value");
        check(readded.at("removed").contains("\"Example\"") && readded.at("entries").contains("\"Example\""),
              "Removal and re-addition of the same key stay distinct");
        auto arrays_schema = UnversionedSchemas::parse(
            collection_schema_fixture(map_type(collection_type(2), collection_type(8, collection_type(22))))
                .data);
        Bytes arrays;
        arrays.i(0);
        arrays.i(1);
        arrays.i(9);
        arrays.i(2);
        arrays.put<std::int16_t>(-5);
        arrays.put<std::int16_t>(12);
        check(decode(collection_single(arrays), arrays_schema, options)
                      .properties.at(lookup)
                      .at("value")
                      .at("entries")
                      .at("9")
                      .at("value") == Json::array({-5, 12}),
              "Map values can contain typed arrays");
        auto nested_schema = UnversionedSchemas::parse(
            collection_schema_fixture(map_type(collection_type(2), collection_struct())).data);
        Bytes nested;
        nested.i(0);
        nested.i(1);
        nested.i(9);
        nested.put<std::uint16_t>(0x0300);
        nested.i(81);
        error([&] { decode(collection_single(nested), nested_schema, options); }, "schema-type");
        auto reflected = options;
        reflected.reflected_structs.insert("/Script/Test.Record");
        check(decode(collection_single(nested), nested_schema, reflected)
                      .properties.at(lookup)
                      .at("value")
                      .at("entries")
                      .at("9")
                      .at("value")
                      .at("/Script/Test.Record.Code")
                      .at("value") == 81,
              "Map value uses an explicitly reflected struct");
        auto set_schema = UnversionedSchemas::parse(
            collection_schema_fixture({}, collection_type(25, collection_enum())).data);
        Bytes enums;
        enums.i(0);
        enums.i(2);
        enums.put<std::uint8_t>(0);
        enums.put<std::uint8_t>(1);
        check(decode(collection_single(enums, true), set_schema, options)
                      .properties.at(tags)
                      .at("value")
                      .at("entries")
                      .size() == 2,
              "Set elements honor enum storage");
        auto struct_set_schema = UnversionedSchemas::parse(
            collection_schema_fixture({}, collection_type(25, collection_struct())).data);
        Bytes structs;
        structs.i(0);
        structs.i(2);
        for (int i : {8, 9}) {
            structs.put<std::uint16_t>(0x0300);
            structs.i(i);
        }
        check(decode(collection_single(structs, true), struct_set_schema, reflected)
                      .properties.at(tags)
                      .at("value")
                      .at("entries")
                      .size() == 2,
              "Reflected struct set identities stay distinct");
        auto maps_schema = UnversionedSchemas::parse(
            collection_schema_fixture(collection_type(8, map_type(collection_type(2), collection_type(2))))
                .data);
        Bytes maps;
        maps.i(2);
        for (int i : {1, 2}) {
            maps.i(0);
            maps.i(1);
            maps.i(i);
            maps.i(i * 10);
        }
        check(
            decode(collection_single(maps), maps_schema, options).properties.at(lookup).at("value").size() ==
                2,
            "Nested array of map deltas");
        auto references_schema = UnversionedSchemas::parse(
            collection_schema_fixture(map_type(collection_type(4), collection_type(2))).data);
        auto references = options;
        references.reference = [](std::int32_t) -> Json { return {{"path", "/Game/Shared.Object"}}; };
        Bytes refs;
        refs.i(0);
        refs.i(2);
        refs.i(-1);
        refs.i(5);
        refs.i(-2);
        refs.i(6);
        error([&] { decode(collection_single(refs), references_schema, references); }, "schema-identity");
        auto strings_schema = UnversionedSchemas::parse(
            collection_schema_fixture(map_type(collection_type(10), collection_type(2))).data);
        Bytes strings;
        strings.i(0);
        strings.i(1);
        strings.string("quote\"\n\\");
        strings.i(42);
        const auto string_map =
            decode(collection_single(strings), strings_schema, options).properties.at(lookup).at("value");
        check(string_map.at("entries").begin().key() == "\"quote\\\"\\n\\\\\"",
              "Canonical identity preserves quote/control/backslash escaping");
        auto floats_schema = UnversionedSchemas::parse(
            collection_schema_fixture(map_type(collection_type(3), collection_type(2))).data);
        Bytes nan;
        nan.i(0);
        nan.i(1);
        nan.put<std::uint32_t>(0x7fc00000);
        nan.i(42);
        error([&] { decode(collection_single(nan), floats_schema, options); }, "schema-value");
        auto unsupported_schema = UnversionedSchemas::parse(
            collection_schema_fixture(map_type(collection_type(2), collection_type(11))).data);
        Bytes unsupported;
        unsupported.i(0);
        unsupported.i(1);
        unsupported.i(9);
        error([&] { decode(collection_single(unsupported), unsupported_schema, options); }, "schema-context");
        Bytes empty;
        empty.i(0);
        empty.i(0);
        check(decode(collection_single(empty), unsupported_schema, options)
                  .properties.at(lookup)
                  .at("value")
                  .at("entries")
                  .empty(),
              "Empty collection needs no unsupported element bytes");
        bool truncated = true;
        for (std::size_t end = 0; end < body.data.size(); ++end) {
            try {
                decode_unversioned(body.data, 0, end, schemas, "Collections", options);
                truncated = false;
            } catch (const Error &) {
            }
        }
        check(truncated, "Every truncated collection window is rejected");
        Bytes huge;
        huge.i(1000000);
        error([&] { decode(collection_single(huge), schemas, options); }, "schema-limit");
        auto limited = options;
        limited.max_values = 10;
        error([&] { decode(body, schemas, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 2;
        error([&] { decode(collection_single(arrays), arrays_schema, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = 250;
        Bytes long_key;
        long_key.i(0);
        long_key.i(1);
        long_key.string(std::string(150, 'a'));
        long_key.i(42);
        error([&] { decode(collection_single(long_key), strings_schema, limited); }, "schema-limit");
        auto cancel = options;
        bool stop = false;
        cancel.control.cancelled = [&] { return stop; };
        cancel.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "unversioned-container-removals")
                stop = true;
        };
        error([&] { decode(body, schemas, cancel); }, "cancelled");
        cancel = options;
        unsigned work = 0;
        bool identity_work = false;
        cancel.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "unversioned-key-identity")
                identity_work = true;
        };
        cancel.charge = [&](std::uint32_t n, std::uint64_t) {
            if (identity_work)
                work += n;
            if (work == 4)
                stop = true;
        };
        stop = false;
        cancel.control.cancelled = [&] { return stop; };
        error([&] { decode(body, schemas, cancel); }, "cancelled");
        check(work == 4, "Cancellation propagates during canonical identity traversal");
        Bytes zero_only;
        zero_only.put<std::uint16_t>(0x0380);
        zero_only.put<std::uint8_t>(1);
        cancel = options;
        stop = false;
        cancel.control.cancelled = [&] { return stop; };
        // Request cancellation from the last value's charge, after its final byte read.
        unsigned calls = 0;
        const auto last_charge = decode(zero_only, schemas, options).values_read;
        cancel.charge = [&](std::uint32_t n, std::uint64_t) {
            if (n && ++calls == last_charge)
                stop = true;
        };
        error([&] { decode(zero_only, schemas, cancel); }, "cancelled");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"/Script/Test.Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto package = ZenPackage::parse(collection_zen_fixture(body).data, zen);
        check(package.property_exports_decoded == 2 && package.property_exports_failed == 0,
              "Zen decodes maps and sets through explicit mappings");
        auto reordered = compare_zen_packages(
            package, ZenPackage::parse(collection_zen_fixture(collection_values(true)).data, zen));
        check(reordered["status"] == "changed" && reordered["changes"].size() == 1 &&
                  reordered["changes"][0]["path"] == "/objects/~1Game~1Example.Example/opaque/sha256",
              "Iteration-only change remains visible solely in raw evidence");
        auto difference = compare_zen_packages(
            package, ZenPackage::parse(collection_zen_fixture(collection_values(false, 99)).data, zen));
        bool found = false;
        for (const auto &change : difference["changes"])
            if (change["path"] == "/objects/~1Game~1Example.Example/properties/"
                                  "~1Script~1Test.Collections.Lookup/value/entries/\"None\"/value")
                found = change["before"] == 20 && change["after"] == 99;
        check(found, "Map diff locates the changed key/value");
        auto failed = ZenPackage::parse(collection_zen_fixture(collection_single(unsupported)).data, zen);
        check(failed.property_exports_failed == 1 && failed.exports[0].property_read["status"] == "opaque" &&
                  !failed.exports[0].sha256.empty(),
              "Failed bound map retains complete opaque export hash");
        auto old_profile = ZenPackage::parse(collection_zen_fixture(replacement_body).data, zen);
        check(old_profile.property_exports_failed == 1,
              "UE 5.3 profile refuses an unverified replacement marker");
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        auto modern = ZenPackage::parse(collection_zen_fixture(replacement_body, true).data, zen);
        check(modern.property_exports_decoded == 2 &&
                  modern.exports[0].property_read["properties"][lookup]["value"]["replace"] == true,
              "UE 5.5 profile enables its explicit replacement layout");
        auto tiny = zen;
        tiny.max_property_values = 4;
        error([&] { ZenPackage::parse(collection_zen_fixture(body, true).data, tiny); }, "schema-limit");
        std::cout << checks << " unversioned collection checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
