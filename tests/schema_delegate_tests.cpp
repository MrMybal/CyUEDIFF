#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "schema_value_fixture.hpp"
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool good, const char *m) {
    ++checks;
    if (!good)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f, const char *code = nullptr) {
    try {
        f();
    } catch (const Error &e) {
        check(!code || e.code == code, "Expected delegate refusal");
        return;
    }
    throw std::runtime_error("Expected delegate refusal");
}
} // namespace
int main() {
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto mapping = [](unsigned kind) {
            return UnversionedSchemas::parse(collection_schema_fixture(collection_type(kind)).data);
        };
        auto single = mapping(6), multi = mapping(13), weak = mapping(14);
        std::vector<std::string> names = {"None", "/Game/Example", "OnChanged", "OnOther"};
        UnversionedReadOptions options;
        options.names = &names;
        options.script_reference_layout = ScriptReferenceLayout::package_indices;
        options.reference = [](std::int32_t index) -> Json {
            return {{"index", index}, {"path", "/Game/Example.Actor"}};
        };
        auto decode = [&](const Bytes &payload, const UnversionedSchemas &schema,
                          const UnversionedReadOptions &opts) {
            auto bytes = collection_single(payload);
            return decode_unversioned(bytes.data, 0, bytes.data.size(), schema, "Collections", opts);
        };
        const auto bound = delegate_value(-7, 2, 4);
        auto value = decode(bound, single, options).properties[field]["value"];
        check(value["kind"] == "script-delegate" && value["object"]["index"] == -7 &&
                  value["function"] == "OnChanged_3",
              "Delegate retains signed package target and numbered function");
        check(decode(bound, single, options).consumed_bytes == 14, "Delegate consumes object and FName only");
        auto no_context = options;
        no_context.script_reference_layout = ScriptReferenceLayout::unspecified;
        rejects([&] { decode(bound, single, no_context); }, "schema-context");
        auto no_resolver = options;
        no_resolver.reference = {};
        rejects([&] { decode(bound, single, no_resolver); }, "schema-context");
        check(
            decode(delegate_value(0, 2), single, no_resolver).properties[field]["value"]["object"].is_null(),
            "Null target needs no resolver and retains stored function");
        auto bad = bound;
        bad.patch(8, -1);
        rejects([&] { decode(bad, single, options); }, "schema-context");
        bad = bound;
        bad.patch(4, 999);
        rejects([&] { decode(bad, single, options); }, "schema-context");
        no_context = options;
        no_context.names = nullptr;
        rejects([&] { decode(bound, single, no_context); }, "schema-context");
        auto bindings = decode(multicast_value(), multi, options).properties[field]["value"]["bindings"];
        check(bindings.size() == 3 && bindings[0] == bindings[2] && bindings[1]["object"].is_null(),
              "Multicast retains order, duplicate and unresolved/null bindings without compaction");
        check(decode(multicast_value(false, true), multi, options).properties[field]["value"]["bindings"] !=
                  bindings,
              "Reordering bindings remains observable");
        Bytes empty;
        empty.i(0);
        check(decode(empty, multi, options).properties[field]["value"]["bindings"].empty(),
              "Empty invocation list");
        Bytes huge;
        huge.i(-1);
        rejects([&] { decode(huge, multi, options); }, "schema-limit");
        huge = Bytes{};
        huge.i(100001);
        rejects([&] { decode(huge, multi, options); }, "schema-limit");
        Bytes reference;
        reference.i(-7);
        check(decode(reference, weak, options).properties[field]["value"]["object"]["index"] == -7,
              "Weak value is a persistent package reference");
        rejects([&] { decode(reference, weak, UnversionedReadOptions{}); }, "schema-context");
        for (const auto &test : std::vector<std::pair<Bytes, const UnversionedSchemas *>>{
                 {bound, &single}, {multicast_value(), &multi}, {reference, &weak}}) {
            for (std::size_t end = 0; end < test.first.data.size(); ++end) {
                auto cut = test.first;
                cut.data.resize(end);
                rejects([&] { decode(cut, *test.second, options); }, "bounds");
            }
            auto trailing = test.first;
            trailing.zeros(16);
            check(decode(trailing, *test.second, options).consumed_bytes == test.first.data.size() + 2,
                  "Reference layouts leave trailing native bytes untouched");
            Bytes zero;
            zero.put<std::uint16_t>(0x0380);
            zero.put<std::uint8_t>(1);
            auto result = decode_unversioned(zero.data, 0, zero.data.size(), *test.second, "Collections");
            check(result.consumed_bytes == 3 && result.properties[field]["serializedZero"] == true,
                  "Zero delegate/reference consumes no payload or host resolution");
        }
        auto limited = options;
        limited.max_values = 6;
        rejects([&] { decode(multicast_value(), multi, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 2;
        rejects([&] { decode(bound, single, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = 8;
        rejects([&] { decode(bound, single, limited); }, "schema-limit");
        limited = options;
        int polls = 0;
        limited.control.cancelled = [&] { return ++polls > 12; };
        rejects([&] { decode(multicast_value(), multi, limited); }, "cancelled");
        limited = options;
        limited.reference = [](std::int32_t) -> Json { return {{std::string(100, 'k'), 1}}; };
        limited.max_text_bytes = 90;
        rejects([&] { decode(bound, single, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 5;
        limited.reference = [](std::int32_t) -> Json {
            Json v = 0;
            for (int i = 0; i < 20; ++i)
                v = Json::array({v});
            return v;
        };
        rejects([&] { decode(bound, single, limited); }, "schema-limit");
        limited = options;
        limited.reference = [](std::int32_t) -> Json { return std::numeric_limits<double>::infinity(); };
        rejects([&] { decode(bound, single, limited); }, "schema-value");
        limited = options;
        limited.reference = [](std::int32_t) -> Json { return std::string(1, char(0xff)); };
        rejects([&] { decode(bound, single, limited); }, "schema-value");
        limited = options;
        limited.reference = [](std::int32_t) -> Json { return Json(std::vector<int>(1000, 0)); };
        limited.max_values = 30;
        rejects([&] { decode(bound, single, limited); }, "schema-limit");
        auto arrays =
            UnversionedSchemas::parse(collection_schema_fixture(collection_type(8, collection_type(6))).data);
        Bytes array;
        array.i(2);
        array.append(bound);
        array.append(delegate_value(0, 0));
        check(decode(array, arrays, options).properties[field]["value"].size() == 2,
              "Delegate arrays use bounded recursive decoding");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(multi);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto package =
            ZenPackage::parse(collection_zen_fixture(collection_single(multicast_value())).data, zen);
        check(package.property_exports_decoded == 2 &&
                  package.exports[0].property_read["properties"][field]["value"]["bindings"][0]["object"]
                                                  ["path"] == "/Game/Example.Example",
              "Zen supplies persistent reference layout and export identity");
        const auto changed =
            ZenPackage::parse(collection_zen_fixture(collection_single(multicast_value(true))).data, zen);
        const auto difference = compare_zen_packages(package, changed);
        bool precise = false;
        for (const auto &change : difference["changes"])
            if (change["path"] == "/objects/~1Game~1Example.Example/properties/"
                                  "~1Script~1Test.Collections.Lookup/value/bindings/0/function")
                precise = change["before"] == "Example" && change["after"] == "Nested";
        check(precise, "Delegate diff names the changed binding function");
        auto invalid = multicast_value();
        invalid.patch(4, 999);
        auto opaque = ZenPackage::parse(collection_zen_fixture(collection_single(invalid)).data, zen);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties"),
              "Invalid delegate target discards partial list");
        zen.max_property_values = 8;
        rejects(
            [&] {
                ZenPackage::parse(collection_zen_fixture(collection_single(multicast_value())).data, zen);
            },
            "schema-limit");
        zen.max_property_values = 100000;
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(multicast_value()), true).data, zen)
                      .property_exports_decoded == 2,
              "UE5.5 delegate layout");
        std::cout << checks << " unversioned delegate checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
