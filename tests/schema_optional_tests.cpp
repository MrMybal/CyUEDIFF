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
    throw std::runtime_error("Expected optional refusal");
}
} // namespace
int main() {
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schema = [](Bytes inner) {
            return UnversionedSchemas::parse(collection_schema_fixture(collection_type(28, inner)).data);
        };
        auto integers = schema(collection_type(2));
        UnversionedReadOptions options;
        options.optional_layout = OptionalLayout::bool32_value;
        auto decode = [&](const Bytes &payload, const UnversionedSchemas &schemas,
                          const UnversionedReadOptions &opts) {
            auto wire = collection_single(payload);
            return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", opts);
        };
        Bytes n;
        n.i(-123);
        const auto present = optional_value(n), unset = optional_value({}, false);
        const auto result = decode(present, integers, options);
        check(result.properties[field]["value"] ==
                  Json{{"kind", "optional"}, {"isSet", true}, {"value", -123}},
              "Set optional retains signed inner value");
        check(result.consumed_bytes == 10, "Bool32 presence plus exact inner width");
        check(decode(unset, integers, options).properties[field]["value"] ==
                  Json{{"kind", "optional"}, {"isSet", false}},
              "Unset optional has no fabricated value");
        Bytes emptyHeader;
        emptyHeader.put<std::uint16_t>(0x0100);
        check(decode_unversioned(emptyHeader.data, 0, 2, integers, "Collections", options).properties.empty(),
              "Skipped optional remains absent");
        Bytes z;
        z.i(0);
        check(decode(optional_value(z), integers, options).properties[field]["value"]["isSet"] == true,
              "Set zero differs from unset");
        auto objects = schema(collection_type(4));
        const auto nullObject = decode(optional_value(z), objects, options).properties[field]["value"];
        check(nullObject["isSet"] == true && nullObject.contains("value") && nullObject["value"].is_null(),
              "Set null reference differs from unset");
        rejects([&] { decode(present, integers, {}); }, "schema-context");
        for (int marker : {-1, 2, 256}) {
            Bytes bad;
            bad.i(marker);
            bad.append(n);
            rejects([&] { decode(bad, integers, options); }, "schema-value");
        }
        for (const auto &payload : {present, unset}) {
            for (std::size_t end = 0; end < payload.data.size(); ++end) {
                auto cut = payload;
                cut.data.resize(end);
                rejects([&] { decode(cut, integers, options); }, "bounds");
            }
            auto tail = payload;
            tail.zeros(12);
            check(decode(tail, integers, options).consumed_bytes == payload.data.size() + 2,
                  "Trailing native bytes retained");
        }
        auto unknown = schema(collection_type(255));
        check(decode(unset, unknown, options).properties[field]["value"]["isSet"] == false,
              "Unset optional does not decode absent unsupported inner type");
        rejects([&] { decode(present, unknown, options); }, "schema-type");
        Bytes mask;
        mask.put<std::uint16_t>(0x0380);
        mask.put<std::uint8_t>(1);
        auto masked = decode_unversioned(mask.data, 0, mask.data.size(), unknown, "Collections");
        check(masked.consumed_bytes == 3 && masked.properties[field]["serializedZero"] == true &&
                  masked.properties[field]["value"]["isSet"] == false,
              "Zero mask consumes no presence or inner bytes");
        auto nested = schema(collection_type(28, collection_type(2)));
        check(decode(optional_value(present), nested, options).properties[field]["value"]["value"]["value"] ==
                  -123,
              "Nested set optionals");
        check(decode(optional_value(unset), nested, options).properties[field]["value"]["value"]["isSet"] ==
                  false,
              "Nested unset inner optional");
        auto arrays = UnversionedSchemas::parse(
            collection_schema_fixture(collection_type(8, collection_type(28, collection_type(2)))).data);
        Bytes list;
        list.i(2);
        list.append(unset);
        list.append(present);
        auto rows = decode(list, arrays, options).properties[field]["value"];
        check(rows[0]["isSet"] == false && rows[1]["value"] == -123,
              "Array element optionals use explicit inner descriptors");
        auto structSchema = schema(collection_struct());
        Bytes record;
        record.put<std::uint16_t>(0x0300);
        record.i(321);
        auto reflected = options;
        reflected.reflected_structs.insert("Record");
        check(decode(optional_value(record), structSchema, reflected)
                      .properties[field]["value"]["value"]["/Script/Test.Record.Code"]["value"] == 321,
              "Optional reflected struct shares nested layout bindings");
        rejects([&] { decode(optional_value(record), structSchema, options); }, "schema-type");
        auto strings = schema(collection_type(10));
        Bytes text;
        text.string("Optional source");
        auto limited = options;
        limited.max_text_bytes = field.size() + 2;
        rejects([&] { decode(optional_value(text), strings, limited); }, "schema-limit");
        limited = options;
        limited.max_values = 2;
        rejects([&] { decode(present, integers, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 2;
        rejects([&] { decode(present, integers, limited); }, "schema-limit");
        limited = options;
        limited.control.cancelled = [] { return true; };
        rejects([&] { decode(present, integers, limited); }, "cancelled");
        unsigned charges = 0;
        limited = options;
        limited.charge = [&](std::uint32_t work, std::uint64_t) {
            charges += work;
            if (charges > 2)
                throw Error("schema-limit", "Enclosing work limit");
        };
        rejects([&] { decode(present, integers, limited); }, "schema-limit");
        check(charges > 2, "Failed optional consumes enclosing budget");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_5;
        context->schemas = std::make_shared<UnversionedSchemas>(integers);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto before = ZenPackage::parse(collection_zen_fixture(collection_single(present), true).data, zen);
        check(before.property_exports_decoded == 2, "UE5.5 selects checked optional layout");
        Bytes edited;
        edited.i(456);
        auto after = ZenPackage::parse(
            collection_zen_fixture(collection_single(optional_value(edited)), true).data, zen);
        const auto diff = compare_zen_packages(before, after);
        bool precise = false;
        for (const auto &change : diff["changes"])
            if (change["path"].get<std::string>().find("/value/value") != std::string::npos)
                precise = change["before"] == -123 && change["after"] == 456;
        check(precise, "Optional inner diff retains property identity");
        auto cleared = ZenPackage::parse(collection_zen_fixture(collection_single(unset), true).data, zen);
        const auto clearDiff = compare_zen_packages(before, cleared);
        bool state = false;
        for (const auto &change : clearDiff["changes"])
            if (change["path"].get<std::string>().find("/value/isSet") != std::string::npos)
                state = change["before"] == true && change["after"] == false;
        check(state, "Optional clear is a presence change");
        auto bad = present;
        bad.data.pop_back();
        auto opaque = ZenPackage::parse(collection_zen_fixture(collection_single(bad), true).data, zen);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties"),
              "Truncation publishes no partial optional");
        zen.max_property_values = 2;
        rejects(
            [&] { ZenPackage::parse(collection_zen_fixture(collection_single(present), true).data, zen); },
            "schema-limit");
        zen.max_property_values = 100000;
        context->profile = ZenProfile::unreal_5_3;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(present)).data, zen)
                      .exports[0]
                      .property_read["status"] == "opaque",
              "Unchecked UE5.3 optional layout remains opaque");
        std::cout << checks << " unversioned optional checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
