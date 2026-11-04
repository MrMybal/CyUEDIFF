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
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected native bounds refusal");
}
} // namespace
int main() {
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        const auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_struct()).data);
        auto decode = [&](const Bytes &b, const UnversionedReadOptions &o) {
            auto wire = collection_single(b);
            return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", o);
        };
        for (const char *token :
             {"box3-f32", "box3-f64", "sphere-f32", "sphere-f64", "box-sphere-f32", "box-sphere-f64"}) {
            const std::string name = token;
            const bool wide = name.find("f64") != std::string::npos, box = name.find("box3") == 0,
                       sphere = name.find("sphere") == 0;
            const unsigned count = box ? 6 : sphere ? 4 : 7;
            const auto layout = parse_native_struct_layout(name);
            check(std::string(native_struct_layout_name(layout)) == name, "Native bounds token round trip");
            UnversionedReadOptions options;
            options.native_structs["/Script/Test.Record"] = layout;
            Bytes payload;
            for (unsigned j = 0; j < count; ++j) {
                double v = wide ? 1000000000000.125 + j : -3.5 + j;
                if (wide)
                    payload.put<double>(v);
                else
                    payload.put<float>(float(v));
            }
            if (box)
                payload.put<std::uint8_t>(255);
            const auto decoded = decode(payload, options);
            const auto value = decoded.properties[field]["value"];
            const auto expected = wide ? 1000000000000.125 : -3.5;
            check(value[box      ? "min"
                        : sphere ? "center"
                                 : "origin"][0] == expected,
                  "Exact native coordinate precision");
            check(decoded.properties[field]["nativeLayout"] == name, "Native bounds provenance");
            if (box)
                check(value["isValid"] == 255 && value["max"][0] == expected + 3,
                      "Stored validity byte remains raw, rather than a normalized bool");
            else
                check(value[sphere ? "radius" : "sphereRadius"] == expected + count - 1,
                      "Radius is a named stored component");
            check(decoded.consumed_bytes == 2 + count * (wide ? 8 : 4) + (box ? 1 : 0),
                  "No SIMD or bool padding consumed");
            auto tail = payload;
            tail.zeros(9);
            check(decode(tail, options).consumed_bytes == decoded.consumed_bytes, "Exact bounded window");
            for (std::size_t n = 0; n < payload.data.size(); ++n) {
                auto cut = payload;
                cut.data.resize(n);
                rejects([&] { decode(cut, options); }, "bounds");
            }
            Bytes zeros;
            zeros.put<std::uint16_t>(0x0380);
            zeros.put<std::uint8_t>(1);
            const auto zero =
                decode_unversioned(zeros.data, 0, zeros.data.size(), schemas, "Collections", options);
            check(zero.consumed_bytes == 3 && zero.properties[field]["serializedZero"] == true,
                  "Zero bound consumes no payload");
            check(zero.properties[field]["value"][box      ? "min"
                                                  : sphere ? "center"
                                                           : "origin"] == Json::array({0, 0, 0}),
                  "Zero components are not synthesized defaults");
            auto bad = payload;
            if (wide) {
                double nan = std::numeric_limits<double>::quiet_NaN();
                std::memcpy(bad.data.data(), &nan, 8);
            } else {
                float inf = std::numeric_limits<float>::infinity();
                std::memcpy(bad.data.data(), &inf, 4);
            }
            rejects([&] { decode(bad, options); }, "schema-value");
            auto limited = options;
            limited.max_values = 5;
            rejects([&] { decode(payload, limited); }, "schema-limit");
            limited = options;
            limited.max_depth = 1;
            rejects([&] { decode(payload, limited); }, "schema-limit");
            limited = options;
            limited.max_text_bytes = 1;
            rejects([&] { decode(payload, limited); }, "schema-limit");
            limited = options;
            unsigned polls = 0;
            limited.control.cancelled = [&] { return ++polls > 8; };
            rejects([&] { decode(payload, limited); }, "cancelled");
            auto arrays = UnversionedSchemas::parse(
                collection_schema_fixture(collection_type(8, collection_struct())).data);
            Bytes values;
            values.i(2);
            values.append(payload);
            values.append(payload);
            auto wire = collection_single(values);
            check(decode_unversioned(wire.data, 0, wire.data.size(), arrays, "Collections", options)
                          .properties[field]["value"][1] == value,
                  "Native bounds compose inside arrays");
            auto context = std::make_shared<ZenSchemaContext>();
            context->schemas = std::make_shared<UnversionedSchemas>(schemas);
            context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
            context->native_structs = options.native_structs;
            ZenReadOptions zen;
            zen.schema_context = context;
            zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
            for (bool modern : {false, true}) {
                context->profile = modern ? ZenProfile::unreal_5_5 : ZenProfile::unreal_5_3;
                zen.profile = context->profile;
                auto package = [&](Bytes p) {
                    return ZenPackage::parse(collection_zen_fixture(collection_single(p), modern).data, zen);
                };
                auto changed = payload;
                if (box)
                    changed.data.back() = 0;
                else {
                    if (wide) {
                        double v = -9.25;
                        std::memcpy(changed.data.data() + (count - 1) * 8, &v, 8);
                    } else {
                        float v = -9.25f;
                        std::memcpy(changed.data.data() + (count - 1) * 4, &v, 4);
                    }
                }
                const auto before = package(payload), after = package(changed);
                check(before.property_exports_decoded == 2 && after.property_exports_decoded == 2,
                      "Explicit bounds work in both profiles");
                const auto diff = compare_zen_packages(before, after);
                bool precise = false;
                const std::string suffix = box      ? "/value/isValid"
                                           : sphere ? "/value/radius"
                                                    : "/value/sphereRadius";
                for (const auto &change : diff["changes"])
                    precise |= change["path"].get<std::string>().find(suffix) != std::string::npos;
                check(precise && diff["binaryChanged"] == true,
                      "Geometry bounds differences have named component paths");
                auto opaque = package(bad);
                check(opaque.exports[0].property_read["status"] == "opaque" &&
                          !opaque.exports[0].property_read.contains("properties"),
                      "Non-finite bound discards partial mapped values");
            }
        }
        std::cout << checks << " native bounds checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
