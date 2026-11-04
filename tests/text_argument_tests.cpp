#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "text_format_fixture.hpp"
#include <chrono>
#include <cmath>
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
    throw std::runtime_error("Expected text argument refusal");
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-text-argument-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_type(11)).data);
        UnversionedReadOptions options;
        options.text_layout = TextLayout::ue5_history;
        auto decode = [&](const Bytes &payload, const UnversionedReadOptions &opts) {
            auto bytes = collection_single(payload);
            return decode_unversioned(bytes.data, 0, bytes.data.size(), schemas, "Collections", opts);
        };
        Bytes i, u, f, d, g;
        i.put<std::int64_t>(INT64_MIN);
        u.put<std::uint64_t>(UINT64_MAX);
        f.put<float>(1.25f);
        d.put<double>(1000000000000.125);
        g.put<std::uint64_t>(2);
        const std::vector<Bytes> args = {text_argument(0, i),
                                         text_argument(1, u),
                                         text_argument(2, f),
                                         text_argument(3, d),
                                         text_argument(4, transform_text_value(text_value("MiXeD"), 1)),
                                         text_argument(5, g)};
        const auto ordered = ordered_text_value(args);
        const auto value = decode(ordered, options);
        const auto &arguments = value.properties[field]["value"]["arguments"];
        check(arguments[0] == Json{{"argumentType", "int64"}, {"value", INT64_MIN}},
              "Signed 64-bit boundary retained");
        check(arguments[1] == Json{{"argumentType", "uint64"}, {"value", UINT64_MAX}},
              "Unsigned 64-bit boundary retained");
        check(arguments[2]["argumentType"] == "float32" && arguments[2]["value"] == 1.25,
              "Float argument type retained");
        check(arguments[3]["argumentType"] == "float64" && arguments[3]["value"] == 1000000000000.125,
              "Double precision retained");
        check(arguments[4]["value"]["source"]["source"] == "MiXeD" && arguments[5]["value"] == 2,
              "Recursive text and uint64 gender wire value retained");
        check(value.consumed_bytes == ordered.data.size() + 2, "Exact ordered format window");
        const auto named = named_text_value({{"Count", args[0]}, {"Label", args[4]}});
        const auto reordered = named_text_value({{"Label", args[4]}, {"Count", args[0]}});
        check(decode(named, options).properties == decode(reordered, options).properties,
              "Named argument order does not change semantic projection");
        auto repeated = named_text_value({{"Count", args[0]}, {"Count", args[1]}});
        rejects([&] { decode(repeated, options); }, "schema-identity");
        const auto duplicate = Package::open(save(root, "duplicate", text_property_fixture(repeated)));
        check(duplicate.exports[0].properties["Formatted"]["opaque"]["reason"] == "text-identity",
              "Duplicate names never overwrite stored arguments");
        for (const auto &payload : {ordered, named, ordered_text_value({}), named_text_value({})}) {
            const auto decoded = decode(payload, options).properties[field]["value"];
            auto tagged = Package::open(save(root, "format", text_property_fixture(payload)));
            check(tagged.exports[0].properties["Formatted"]["value"] == decoded &&
                      tagged.exports[0].properties["AfterFormat"]["value"] == 876,
                  "Classic and cooked formats agree including empty lists");
            auto tail = payload;
            tail.zeros(10);
            check(decode(tail, options).consumed_bytes == payload.data.size() + 2,
                  "Text format leaves trailing native bytes");
            for (std::size_t end = 0; end < payload.data.size(); ++end) {
                auto cut = payload;
                cut.data.resize(end);
                rejects([&] { decode(cut, options); }, "bounds");
            }
        }
        auto negative = ordered_text_value({});
        negative.patch(negative.data.size() - 4, -1);
        rejects([&] { decode(negative, options); }, "schema-value");
        auto huge = negative;
        huge.patch(huge.data.size() - 4, 100001);
        rejects([&] { decode(huge, options); }, "schema-limit");
        for (unsigned id : {6u, 255u})
            rejects([&] { decode(ordered_text_value({text_argument(id, {})}), options); }, "schema-type");
        for (bool doubleType : {false, true}) {
            Bytes nan;
            if (doubleType)
                nan.put<double>(std::numeric_limits<double>::infinity());
            else
                nan.put<float>(std::numeric_limits<float>::quiet_NaN());
            auto bad = ordered_text_value({text_argument(doubleType ? 3 : 2, nan)});
            rejects([&] { decode(bad, options); }, "schema-value");
            auto tagged = Package::open(save(root, "nonfinite", text_property_fixture(bad)));
            check(tagged.exports[0].properties["Formatted"]["opaque"]["reason"] == "text-value",
                  "Non-finite text arguments retain opaque payload");
        }
        const std::string escaped = "~Key/segment";
        auto escapedValue = named_text_value({{escaped, args[1]}});
        check(decode(escapedValue, options).properties[field]["value"]["arguments"].contains(escaped),
              "Named identities retain slash and tilde without path rewriting");
        auto nested = ordered_text_value({text_argument(4, named)});
        check(decode(nested, options).properties[field]["value"]["arguments"][0]["value"]["kind"] ==
                  "named-format",
              "Formatted argument recursion remains explicit");
        auto limited = options;
        limited.max_values = 12;
        rejects([&] { decode(ordered, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 4;
        rejects([&] { decode(nested, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = field.size() + 3;
        rejects([&] { decode(named, limited); }, "schema-limit");
        unsigned polls = 0;
        limited = options;
        limited.control.cancelled = [&] { return ++polls > 20; };
        rejects([&] { decode(ordered, limited); }, "cancelled");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto package = [&](Bytes b) {
            return ZenPackage::parse(collection_zen_fixture(collection_single(b)).data, zen);
        };
        const auto before = package(named), reorder = package(reordered);
        const auto reorderedDiff = compare_zen_packages(before, reorder);
        bool propertyChanged = false;
        for (const auto &c : reorderedDiff["changes"])
            if (c["path"].get<std::string>().find("/properties/") != std::string::npos)
                propertyChanged = true;
        check(!propertyChanged && reorderedDiff["binaryChanged"] == true,
              "Named reorder keeps byte evidence without invented semantic edit");
        Bytes edited;
        edited.put<std::int64_t>(42);
        const auto changed =
            package(named_text_value({{"Count", text_argument(0, edited)}, {"Label", args[4]}}));
        const auto diff = compare_zen_packages(before, changed);
        bool precise = false;
        for (const auto &c : diff["changes"])
            if (c["path"].get<std::string>().find("/value/arguments/Count/value") != std::string::npos)
                precise = c["before"] == INT64_MIN && c["after"] == 42;
        check(precise, "Named argument edit has a precise value path");
        auto opaque = package(repeated);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties"),
              "Failed named format publishes no partial mapped result");
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(ordered), true).data, zen)
                      .property_exports_decoded == 2,
              "Both Zen profiles read ordered arguments");
        fs::remove_all(root);
        std::cout << checks << " text argument checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
