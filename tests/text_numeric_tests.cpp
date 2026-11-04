#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "text_format_fixture.hpp"
#include <chrono>
#include <iostream>
#include <limits>
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
    throw std::runtime_error("Expected numeric text refusal");
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-text-numeric-" +
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
        for (unsigned history : {4u, 5u, 6u})
            for (bool withOptions : {false, true}) {
                const auto payload = numeric_text_value(history, withOptions);
                const auto decoded = decode(payload, options);
                const auto &value = decoded.properties[field]["value"];
                check(value["source"] == Json{{"argumentType", "float64"}, {"value", 1000000000000.125}},
                      "Numeric source keeps double precision and type");
                check(value["culture"] == "fr-FR" && value["history"] == history,
                      "Stored target culture and numeric history retained");
                if (withOptions) {
                    check(value["formatOptions"] == Json{{"alwaysSign", true},
                                                         {"useGrouping", false},
                                                         {"roundingMode", 0},
                                                         {"minimumIntegralDigits", 1},
                                                         {"maximumIntegralDigits", 324},
                                                         {"minimumFractionalDigits", 2},
                                                         {"maximumFractionalDigits", 4}},
                          "Formatting options retain exact serialized components");
                } else
                    check(value["formatOptions"].is_null(),
                          "Absent options never synthesize engine defaults");
                check((history == 6) == value.contains("currencyCode"),
                      "Currency code appears only in currency history");
                auto tagged = Package::open(save(root, "number", text_property_fixture(payload)));
                check(tagged.exports[0].properties["Formatted"]["value"] == value &&
                          tagged.exports[0].properties["AfterFormat"]["value"] == 876,
                      "Classic and mapped numeric text agree");
                auto tail = payload;
                tail.zeros(5);
                check(decode(tail, options).consumed_bytes == payload.data.size() + 2,
                      "Exact numeric text window");
                for (std::size_t end = 0; end < payload.data.size(); ++end) {
                    auto cut = payload;
                    cut.data.resize(end);
                    rejects([&] { decode(cut, options); }, "bounds");
                }
            }
        for (int rounding = 0; rounding <= 6; ++rounding) {
            const auto value =
                decode(numeric_text_value(4, true, "", true, rounding), options).properties[field]["value"];
            check(value["formatOptions"]["roundingMode"] == rounding && value["culture"] == "",
                  "Every checked rounding token is preserved; empty culture stays empty");
        }
        for (int rounding : {-1, 7, 127}) {
            auto bad = numeric_text_value(4, true, "", true, rounding);
            rejects([&] { decode(bad, options); }, "schema-value");
            check(Package::open(save(root, "rounding", text_property_fixture(bad)))
                          .exports[0]
                          .properties["Formatted"]["opaque"]["reason"] == "text-value",
                  "Unknown rounding mode remains opaque");
        }
        auto boolean = numeric_text_value();
        boolean.patch(14, 2);
        rejects([&] { decode(boolean, options); }, "schema-value");
        boolean = numeric_text_value();
        boolean.patch(18, 2);
        rejects([&] { decode(boolean, options); }, "schema-value");
        boolean = numeric_text_value();
        boolean.patch(22, 2);
        rejects([&] { decode(boolean, options); }, "schema-value");
        auto legacy = numeric_text_value(4, true, "de-DE", false);
        auto legacyPackage =
            Package::open(save(root, "legacy", text_property_fixture(legacy, "TextProperty", 1009, 20)));
        const auto legacyValue = legacyPackage.exports[0].properties["Formatted"]["value"];
        check(legacyValue["culture"] == "de-DE" && !legacyValue["formatOptions"].contains("alwaysSign"),
              "Recorded old editor layout omits alwaysSign bytes without inventing a default");
        auto modern = numeric_text_value();
        check(Package::open(save(root, "threshold", text_property_fixture(modern, "TextProperty", 1009, 21)))
                      .exports[0]
                      .properties["Formatted"]["value"]["formatOptions"]["alwaysSign"] == true,
              "AlwaysSign version threshold is explicit");
        auto unusual = numeric_text_value();
        unusual.patch(27, -5);
        unusual.patch(31, 0);
        check(decode(unusual, options).properties[field]["value"]["formatOptions"]["minimumIntegralDigits"] ==
                  -5,
              "Stored digit constraints retained without applying runtime normalization");
        for (double number :
             {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            const auto bad = numeric_text_value(4, true, "en-US", true, 0, number);
            rejects([&] { decode(bad, options); }, "schema-value");
        }
        auto nested = ordered_text_value({text_argument(4, numeric_text_value(6))});
        check(decode(nested, options).properties[field]["value"]["arguments"][0]["value"]["currencyCode"] ==
                  "EUR",
              "Currency text can be an explicit recursive format argument");
        auto limited = options;
        limited.max_values = 8;
        rejects([&] { decode(modern, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 3;
        rejects([&] { decode(nested, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = field.size() + 1;
        rejects([&] { decode(modern, limited); }, "schema-limit");
        unsigned polls = 0;
        limited = options;
        limited.control.cancelled = [&] { return ++polls > 18; };
        rejects([&] { decode(modern, limited); }, "cancelled");
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
        const auto before = package(modern),
                   after = package(numeric_text_value(4, true, "en-US", true, 2, 1000000000001.125));
        const auto diff = compare_zen_packages(before, after);
        bool source = false, culture = false, round = false;
        for (const auto &c : diff["changes"]) {
            const auto path = c["path"].get<std::string>();
            if (path.find("/value/source/value") != std::string::npos)
                source = c["before"] == 1000000000000.125 && c["after"] == 1000000000001.125;
            if (path.find("/value/culture") != std::string::npos)
                culture = c["before"] == "fr-FR" && c["after"] == "en-US";
            if (path.find("/value/formatOptions/roundingMode") != std::string::npos)
                round = c["before"] == 0 && c["after"] == 2;
        }
        check(source && culture && round,
              "Numeric value, culture and rounding changes have distinct precise paths");
        auto bad = modern;
        bad.data.pop_back();
        auto opaque = package(bad);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties"),
              "Invalid numeric text discards entire mapped read");
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(modern), true).data, zen)
                      .property_exports_decoded == 2,
              "Both profiles read numeric histories");
        fs::remove_all(root);
        std::cout << checks << " numeric text checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
