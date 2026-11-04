#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "schema_native_fixture.hpp"
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code = nullptr) {
    try {
        f();
    } catch (const Error &e) {
        check(!code || e.code == code, "Expected native struct refusal");
        return;
    }
    throw std::runtime_error("Expected native struct refusal");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            write_native_fixtures(fs::u8path(argv[2]));
            return 0;
        }
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_struct()).data);
        auto decode = [&](const Bytes &wire, const UnversionedReadOptions &options) {
            return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", options);
        };
        struct Case {
            const char *name;
            unsigned width, count;
            bool integral = false;
        };
        const Case cases[] = {
            {"vector2-f32", 4, 2},     {"vector2-f64", 8, 2},      {"vector3-f32", 4, 3},
            {"vector3-f64", 8, 3},     {"vector4-f32", 4, 4},      {"vector4-f64", 8, 4},
            {"quat-f32", 4, 4},        {"quat-f64", 8, 4},         {"rotator-f32", 4, 3},
            {"rotator-f64", 8, 3},     {"transform-f32", 4, 10},   {"transform-f64", 8, 10},
            {"guid", 4, 4, true},      {"linear-color-f32", 4, 4}, {"color-bgra8", 1, 4, true},
            {"int-point", 4, 2, true}, {"int-vector", 4, 3, true}};
        for (const auto &c : cases) {
            auto layout = parse_native_struct_layout(c.name);
            check(std::string(native_struct_layout_name(layout)) == c.name, "Native layout name round trip");
            UnversionedReadOptions options;
            options.native_structs["/Script/Test.Record"] = layout;
            Bytes values;
            Json expected = Json::array();
            for (unsigned i = 0; i < c.count; ++i) {
                if (c.integral) {
                    if (c.width == 1) {
                        values.put<std::uint8_t>(11 + i * 11);
                        expected.push_back(11 + i * 11);
                    } else if (std::string(c.name) == "guid")
                        values.put<std::uint32_t>(0x01234567u + i);
                    else {
                        values.i(-12 + int(i));
                        expected.push_back(-12 + int(i));
                    }
                } else {
                    const double value = i + .125;
                    if (c.width == 4)
                        values.put<float>(float(value));
                    else
                        values.put<double>(value);
                    expected.push_back(value);
                }
            }
            if (std::string(c.name) == "guid")
                expected = "0123456701234568012345690123456a";
            if (std::string(c.name).find("transform-") == 0)
                expected = {{"rotation", {.125, 1.125, 2.125, 3.125}},
                            {"translation", {4.125, 5.125, 6.125}},
                            {"scale", {7.125, 8.125, 9.125}}};
            auto wire = collection_single(values);
            rejects([&] { decode(wire, {}); }, "schema-type");
            auto result = decode(wire, options);
            check(result.properties[field]["value"] == expected,
                  "Native components retain serialized order and signedness");
            check(result.properties[field]["nativeLayout"] == c.name &&
                      result.properties[field]["typeName"] == "/Script/Test.Record",
                  "Native provenance retains exact schema identity and explicit wire layout");
            check(result.consumed_bytes == 2 + c.width * c.count,
                  "Native property consumes only wire components");
            auto trailing = wire;
            trailing.zeros(13);
            check(decode(trailing, options).consumed_bytes == wire.data.size(),
                  "Trailing native bytes are not consumed");
            for (std::size_t end = 0; end < wire.data.size(); ++end) {
                auto cut = wire;
                cut.data.resize(end);
                rejects([&] { decode(cut, options); }, "bounds");
            }
            Bytes zero;
            zero.put<std::uint16_t>(0x0380);
            zero.put<std::uint8_t>(1);
            auto cleared = decode(zero, options);
            check(cleared.consumed_bytes == 3 && cleared.properties[field]["serializedZero"] == true,
                  "Zero native struct consumes no payload");
            auto &value = cleared.properties[field]["value"];
            if (std::string(c.name) == "guid")
                check(value == std::string(32, '0'), "Zero GUID remains zero");
            else if (std::string(c.name).find("transform-") == 0)
                check(value["rotation"] == Json::array({0, 0, 0, 0}) &&
                          value["scale"] == Json::array({0, 0, 0}),
                      "Zero transform is not synthesized as identity");
            else
                check(value == Json(std::vector<int>(c.count, 0)), "Zero scalar components remain zero");
            if (!c.integral) {
                auto bad = wire;
                if (c.width == 4) {
                    float v = std::numeric_limits<float>::infinity();
                    std::memcpy(bad.data.data() + 2, &v, 4);
                } else {
                    double v = std::numeric_limits<double>::quiet_NaN();
                    std::memcpy(bad.data.data() + 2, &v, 8);
                }
                rejects([&] { decode(bad, options); }, "schema-value");
            }
        }
        rejects([&] { parse_native_struct_layout("Vector"); }, "schema-context");
        rejects([&] { native_struct_layout_name(static_cast<NativeStructLayout>(999)); }, "schema-context");
        UnversionedReadOptions options;
        options.native_structs["Record"] = NativeStructLayout::transform_f64;
        Bytes numbers;
        for (int i = 0; i < 10; ++i)
            numbers.put<double>(i + .25);
        auto wire = collection_single(numbers);
        auto conflict = options;
        conflict.reflected_structs.insert("/Script/Test.Record");
        rejects([&] { decode(wire, conflict); }, "schema-context");
        conflict = options;
        conflict.native_structs["/Script/Test.Record"] = NativeStructLayout::vector3_f32;
        rejects([&] { decode(wire, conflict); }, "schema-context");
        auto limited = options;
        limited.max_values = 5;
        rejects([&] { decode(wire, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 2;
        rejects([&] { decode(wire, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = 2;
        rejects([&] { decode(wire, limited); }, "schema-limit");
        limited = options;
        int polls = 0;
        limited.control.cancelled = [&] { return ++polls > 8; };
        rejects([&] { decode(wire, limited); }, "cancelled");
        limited = options;
        unsigned work = 0;
        limited.charge = [&](unsigned n, std::uint64_t) { work += n; };
        auto read = decode(wire, limited);
        check(read.values_read == work && work >= 13,
              "Native components charge shared work including binding validation");
        auto nested = UnversionedSchemas::parse(
            collection_schema_fixture(collection_type(8, collection_struct())).data);
        Bytes array;
        array.i(2);
        array.append(numbers);
        array.append(numbers);
        auto body = collection_single(array);
        auto list = decode_unversioned(body.data, 0, body.data.size(), nested, "Collections", options);
        check(list.properties[field]["value"].size() == 2 && list.consumed_bytes == body.data.size(),
              "Native layouts work inside bounded arrays");
        auto map_schemas = UnversionedSchemas::parse(
            collection_schema_fixture(map_type(collection_struct(), collection_type(2))).data);
        auto map_values = [&](bool reorder, bool duplicate) {
            Bytes data;
            data.i(0);
            data.i(2);
            for (int i = 0; i < 2; ++i) {
                auto key = numbers;
                double first = duplicate ? 0.25 : double(reorder ? 1 - i : i) + .25;
                std::memcpy(key.data.data(), &first, 8);
                data.append(key);
                data.i(reorder ? 1 - i : i);
            }
            return collection_single(data);
        };
        auto first_map = map_values(false, false), reordered_map = map_values(true, false),
             duplicate_map = map_values(false, true);
        auto native_map = [&](const Bytes &data) {
            return decode_unversioned(data.data, 0, data.data.size(), map_schemas, "Collections", options);
        };
        check(native_map(first_map).properties == native_map(reordered_map).properties,
              "Native struct map keys use stable canonical component identities");
        rejects([&] { native_map(duplicate_map); }, "schema-identity");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        context->native_structs = options.native_structs;
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        const auto package = ZenPackage::parse(collection_zen_fixture(wire).data, zen);
        check(package.property_exports_decoded == 2 &&
                  package.exports[0].property_read["properties"][field]["value"]["translation"][1] == 5.25,
              "Zen context reads explicit native transform components");
        auto changed_numbers = numbers;
        double changed_component = 99.5;
        std::memcpy(changed_numbers.data.data() + 5 * 8, &changed_component, 8);
        auto changed =
            ZenPackage::parse(collection_zen_fixture(collection_single(changed_numbers)).data, zen);
        auto diff = compare_zen_packages(package, changed);
        bool precise = false;
        for (const auto &change : diff["changes"])
            if (change["path"] == "/objects/~1Game~1Example.Example/properties/"
                                  "~1Script~1Test.Collections.Lookup/value/translation/1")
                precise = change["before"] == 5.25 && change["after"] == 99.5;
        check(precise && diff["binaryChanged"] == true,
              "Native transform diff identifies its changed component and retains binary evidence");
        const auto original_fingerprint = context->fingerprint();
        context->native_structs["Record"] = NativeStructLayout::transform_f32;
        auto other = ZenPackage::parse(collection_zen_fixture(wire).data, zen);
        check(context->fingerprint() != original_fingerprint &&
                  compare_zen_packages(package, other)["contextChanged"] == true,
              "Native wire layout participates in context identity");
        context->native_structs.clear();
        auto opaque = ZenPackage::parse(collection_zen_fixture(wire).data, zen);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties") &&
                  opaque.exports[0].sha256 == package.exports[0].sha256,
              "Unbound native value remains wholly opaque with its original hash");
        context->native_structs = options.native_structs;
        auto short_wire = wire;
        short_wire.data.pop_back();
        opaque = ZenPackage::parse(collection_zen_fixture(short_wire).data, zen);
        check(opaque.exports[0].property_read["status"] == "opaque" &&
                  !opaque.exports[0].property_read.contains("properties"),
              "Truncated native body discards partial property values");
        zen.max_property_values = 8;
        rejects([&] { ZenPackage::parse(collection_zen_fixture(wire).data, zen); }, "schema-limit");
        zen.max_property_values = 100000;
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(wire, true).data, zen).property_exports_decoded == 2,
              "Explicit native layout works with UE5.5 Zen profile");
        std::cout << checks << " native struct checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
