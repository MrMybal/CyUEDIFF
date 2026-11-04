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
void check(bool b, const char *message) {
    ++checks;
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected argument-data refusal");
}
Bytes args(bool wide = true) {
    Bytes integer, small, number, gender;
    if (wide)
        integer.put<std::int64_t>(-9007199254740993LL);
    else
        integer.i(-2147483647);
    small.put<float>(1.25f);
    number.put<double>(1000000000000.125);
    gender.put<std::uint8_t>(255);
    return data_text_value({{"Count", text_argument(0, integer)},
                            {"Count", text_argument(5, gender)},
                            {"Label", text_argument(4, calendar_text_value())},
                            {"", text_argument(2, small)},
                            {"Double", text_argument(3, number)}});
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-text-data-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_type(11)).data);
        UnversionedReadOptions options;
        options.text_layout = TextLayout::ue5_history;
        auto decode = [&](const Bytes &payload, const UnversionedReadOptions &opt) {
            const auto wire = collection_single(payload);
            return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", opt);
        };
        rejects([&] { decode(args(), options); }, "schema-type");
        for (bool wide : {false, true}) {
            options.text_argument_data_layout =
                wide ? TextArgumentDataLayout::variant_int64 : TextArgumentDataLayout::variant_int32;
            const auto payload = args(wide);
            const auto decoded = decode(payload, options).properties[field]["value"];
            check(decoded["kind"] == "argument-data-format" && decoded["arguments"].size() == 5,
                  "Argument-data stores an ordered array");
            check(decoded["arguments"][0]["argumentType"] == (wide ? "int64" : "int32") &&
                      decoded["arguments"][0]["value"] == (wide ? -9007199254740993LL : -2147483647LL),
                  "Integer width is explicit and exact");
            check(decoded["arguments"][1] ==
                      Json{{"name", "Count"}, {"argumentType", "gender"}, {"value", 255}},
                  "Duplicate names and raw byte gender retained");
            check(decoded["arguments"][2]["value"]["kind"] == "date-time" &&
                      decoded["arguments"][3]["name"] == "",
                  "Recursive text and empty names");
            auto classic = Package::open(
                save(root, "data", text_property_fixture(payload, "TextProperty", 1013, 32, wide ? 12 : 11)));
            check(classic.exports[0].properties["Formatted"]["value"] == decoded &&
                      classic.exports[0].properties["AfterFormat"]["value"] == 876,
                  "Classic custom-version threshold agrees with explicit mapped layout");
            for (std::size_t n = 0; n < payload.data.size(); ++n) {
                auto cut = payload;
                cut.data.resize(n);
                rejects([&] { decode(cut, options); }, "bounds");
            }
            auto tail = payload;
            tail.zeros(16);
            check(decode(tail, options).consumed_bytes == payload.data.size() + 2,
                  "Byte gender does not consume the next entry");
        }
        auto old = data_text_value({{"Legacy", text_value("stored text")}});
        auto legacy =
            Package::open(save(root, "legacy", text_property_fixture(old, "TextProperty", 1009, 4)));
        check(legacy.exports[0].properties["Formatted"]["value"]["arguments"][0]["value"]["source"] ==
                      "stored text" &&
                  legacy.exports[0].properties["AfterFormat"]["value"] == 876,
              "Recorded pre-variant editor layout reads text without a type byte");
        Bytes integer;
        integer.i(-7);
        auto absentRelease = Package::open(
            save(root, "missing-release",
                 text_property_fixture(data_text_value({{"Count", text_argument(0, integer)}}))));
        check(absentRelease.exports[0].properties["Formatted"]["value"]["arguments"][0]["argumentType"] ==
                  "int32",
              "Classic missing release version follows the archived legacy width");
        for (unsigned id : {1u, 6u, 255u}) {
            auto bad = data_text_value({{"Bad", text_argument(id, {})}});
            rejects([&] { decode(bad, options); }, "schema-type");
            auto classic = Package::open(
                save(root, "unknown", text_property_fixture(bad, "TextProperty", 1013, 32, 12)));
            check(classic.exports[0].properties["Formatted"].contains("opaque") &&
                      classic.exports[0].properties["AfterFormat"]["value"] == 876,
                  "Unknown argument-data type remains opaque");
        }
        auto empty = decode(data_text_value({}), options).properties[field]["value"];
        check(empty["arguments"].is_array() && empty["arguments"].empty(),
              "Empty argument-data array remains explicit");
        auto badCount = data_text_value({}, text_value(""));
        badCount.patch(badCount.data.size() - 4, -1);
        rejects([&] { decode(badCount, options); }, "schema-value");
        badCount.patch(badCount.data.size() - 4, 100001);
        rejects([&] { decode(badCount, options); }, "schema-limit");
        auto limited = options;
        limited.max_values = 10;
        rejects([&] { decode(args(), limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 4;
        rejects([&] { decode(args(), limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = field.size() + 1;
        rejects([&] { decode(args(), limited); }, "schema-limit");
        limited = options;
        unsigned polls = 0;
        limited.control.cancelled = [&] { return ++polls > 30; };
        rejects([&] { decode(args(), limited); }, "cancelled");
        Bytes nan;
        nan.put<double>(std::numeric_limits<double>::quiet_NaN());
        rejects([&] { decode(data_text_value({{"Bad", text_argument(3, nan)}}), options); }, "schema-value");
        auto context = std::make_shared<ZenSchemaContext>();
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        for (bool modern : {false, true}) {
            context->profile = modern ? ZenProfile::unreal_5_5 : ZenProfile::unreal_5_3;
            zen.profile = context->profile;
            const auto payload = args();
            auto before =
                ZenPackage::parse(collection_zen_fixture(collection_single(payload), modern).data, zen);
            check(before.property_exports_decoded == 2,
                  "Profile supplies modern argument-data width when stripped");
            auto changed = payload;
            changed.data[changed.data.size() - 8] ^= 1;
            auto after =
                ZenPackage::parse(collection_zen_fixture(collection_single(changed), modern).data, zen);
            auto diff = compare_zen_packages(before, after);
            bool found = false;
            for (const auto &c : diff["changes"])
                found |= c["path"].get<std::string>().find("/value/arguments/4/value") != std::string::npos;
            check(found, "Argument-data change paths preserve array order");
            for (int version : {11, 12}) {
                auto bytes = collection_zen_fixture(collection_single(args(version >= 12)), modern, true);
                for (unsigned i = 0; i < 4; ++i) {
                    const std::uint32_t words[] = {0xd89b5e42u, 0x24bd4d46u, 0x8412aca8u, 0xdf641779u};
                    std::memcpy(bytes.data.data() + 72 + 4 * i, &words[i], 4);
                }
                bytes.patch(88, version);
                auto package = ZenPackage::parse(bytes.data, zen);
                check(package.property_exports_decoded == 2 &&
                          package.exports[0].property_read["properties"][field]["value"]["arguments"][0]["argumentType"] ==
                              (version >= 12 ? "int64" : "int32"),
                      "Recorded cooked release version selects integer width");
            }
        }
        fs::remove_all(root);
        std::cout << checks << " argument-data checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
