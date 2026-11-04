#include <cyuediff/schema.hpp>
#include "schema_collection_fixture.hpp"
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
int main() {
    try {
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
        std::cout << checks << " native struct checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
