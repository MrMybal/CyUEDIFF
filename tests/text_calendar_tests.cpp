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
    throw std::runtime_error("Expected calendar text refusal");
}
} // namespace
int main() {
    const auto root =
        fs::current_path() /
        (".test-tmp-calendar-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_type(11)).data);
        UnversionedReadOptions options;
        options.text_layout = TextLayout::ue5_history;
        auto decode = [&](const Bytes &payload, const UnversionedReadOptions &opts) {
            const auto wire = collection_single(payload);
            return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", opts);
        };
        for (unsigned history : {7u, 8u, 9u})
            for (int style : {0, 1, 2, 3, 4, 5}) {
                const auto payload = calendar_text_value(history, 638980123456789012LL, style, style);
                const auto decoded = decode(payload, options);
                const auto value = decoded.properties[field]["value"];
                check(value["ticks"] == 638980123456789012LL, "Ticks retain exact 64-bit precision");
                check(value["culture"] == "fr-FR" && value["timeZone"] == "Europe/Paris",
                      "Stored timezone and culture retained");
                check(value.contains("dateStyle") == (history != 8) &&
                          value.contains("timeStyle") == (history != 7),
                      "Date/time fields match the serialized history");
                check(value.contains("customPattern") == (history == 9 && style == 5),
                      "Custom pattern appears only when serialized");
                auto package = Package::open(save(root, "calendar", text_property_fixture(payload)));
                check(package.exports[0].properties["Formatted"]["value"] == value &&
                          package.exports[0].properties["AfterFormat"]["value"] == 876,
                      "Classic and mapped calendar values agree and preserve alignment");
                check(decoded.consumed_bytes == payload.data.size() + 2, "Exact calendar read window");
                if (style == 2 || style == 5)
                    for (std::size_t n = 0; n < payload.data.size(); ++n) {
                        auto cut = payload;
                        cut.data.resize(n);
                        rejects([&] { decode(cut, options); }, "bounds");
                    }
            }
        for (auto ticks : {std::numeric_limits<std::int64_t>::min(), std::int64_t(-1), std::int64_t(0),
                           std::numeric_limits<std::int64_t>::max()})
            check(decode(calendar_text_value(9, ticks), options).properties[field]["value"]["ticks"] == ticks,
                  "Stored tick bit pattern retained without runtime normalization");
        for (int style : {-128, -1, 6, 127}) {
            auto bad = calendar_text_value(9, 0, style);
            rejects([&] { decode(bad, options); }, "schema-value");
            auto package = Package::open(save(root, "bad-style", text_property_fixture(bad)));
            check(package.exports[0].properties["Formatted"]["opaque"].contains("reason") &&
                      package.exports[0].properties["AfterFormat"]["value"] == 876,
                  "Invalid style is opaque with following classic tag intact");
            rejects([&] { decode(calendar_text_value(8, 0, 0, style), options); }, "schema-value");
        }
        const auto empty =
            decode(calendar_text_value(9, 0, 5, 0, "", "", ""), options).properties[field]["value"];
        check(empty["timeZone"] == "" && empty["culture"] == "" && empty["customPattern"] == "",
              "Empty strings are explicit and do not acquire host defaults");
        rejects([&] { decode(calendar_text_value(7, 0, 0, 0, "\xc0\x80"), options); }, "schema-type");
        auto nested = ordered_text_value({text_argument(4, calendar_text_value())});
        check(decode(nested, options).properties[field]["value"]["arguments"][0]["value"]["kind"] ==
                  "date-time",
              "Recursive calendar histories");
        auto limited = options;
        limited.max_values = 5;
        rejects([&] { decode(nested, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 3;
        rejects([&] { decode(nested, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = field.size() + 2;
        rejects([&] { decode(calendar_text_value(), limited); }, "schema-limit");
        limited = options;
        unsigned polls = 0;
        limited.control.cancelled = [&] { return ++polls > 12; };
        rejects([&] { decode(nested, limited); }, "cancelled");
        auto context = std::make_shared<ZenSchemaContext>();
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        for (bool modern : {false, true}) {
            context->profile = modern ? ZenProfile::unreal_5_5 : ZenProfile::unreal_5_3;
            zen.profile = context->profile;
            auto package = [&](Bytes payload) {
                return ZenPackage::parse(collection_zen_fixture(collection_single(payload), modern).data,
                                         zen);
            };
            const auto before = package(calendar_text_value()),
                       after = package(
                           calendar_text_value(9, 638980123456789013LL, 5, 4, "UTC", "en-US", "yyyy"));
            check(before.property_exports_decoded == 2 && after.property_exports_decoded == 2,
                  "Both certified profiles read calendar values");
            auto diff = compare_zen_packages(before, after);
            bool tick = false, zone = false, pattern = false;
            for (const auto &c : diff["changes"]) {
                auto path = c["path"].get<std::string>();
                tick |= path.find("/value/ticks") != std::string::npos && c["after"] == 638980123456789013LL;
                zone |= path.find("/value/timeZone") != std::string::npos;
                pattern |= path.find("/value/customPattern") != std::string::npos;
            }
            check(tick && zone && pattern, "Ticks, timezone and pattern changes have separate paths");
            auto bad = calendar_text_value();
            bad.data.pop_back();
            check(package(bad).exports[0].property_read["status"] == "opaque",
                  "Truncated mapped read is opaque as a whole");
        }
        fs::remove_all(root);
        std::cout << checks << " calendar checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
