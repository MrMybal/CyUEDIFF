#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "native_property_fixture.hpp"
#include "schema_collection_fixture.hpp"
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool b, const char *m) {
    ++checks;
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected native matrix refusal");
}
} // namespace
int main() {
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_struct()).data);
        for (const char *token : {"matrix4-f32", "matrix4-f64", "plane-f32", "plane-f64"}) {
            const std::string name = token;
            const bool matrix = name.find("matrix") == 0, wide = name.find("f64") != std::string::npos;
            const auto layout = parse_native_struct_layout(name);
            check(std::string(native_struct_layout_name(layout)) == name,
                  "Native matrix/plane layout round trip");
            UnversionedReadOptions options;
            options.native_structs["Record"] = layout;
            auto decode = [&](const Bytes &value, const UnversionedReadOptions &opt) {
                const auto wire = collection_single(value);
                return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", opt);
            };
            const auto payload = geometry_value(matrix ? "Matrix" : "Plane", wide);
            const auto read = decode(payload, options);
            const auto value = read.properties[field]["value"];
            if (matrix)
                check(value.size() == 4 && value[0].size() == 4 &&
                          value[3][3] == (wide ? 1000000000015.125 : 11.5),
                      "Row-major matrix, no transposition or SIMD padding");
            else
                check(value["normal"][0] == (wide ? 1000000000000.125 : -3.5) &&
                          value["distance"] == (wide ? 1000000000003.125 : -.5),
                      "Plane coefficients retained without normalization");
            check(read.consumed_bytes == payload.data.size() + 2, "Exact native matrix window");
            for (std::size_t n = 0; n < payload.data.size(); ++n) {
                auto cut = payload;
                cut.data.resize(n);
                rejects([&] { decode(cut, options); }, "bounds");
            }
            Bytes zero;
            zero.put<std::uint16_t>(0x0380);
            zero.put<std::uint8_t>(1);
            const auto zeros =
                decode_unversioned(zero.data, 0, zero.data.size(), schemas, "Collections", options)
                    .properties[field]["value"];
            check(matrix ? zeros[3][3] == 0 : zeros["distance"] == 0,
                  "Zero wire matrix is not an identity matrix");
            auto invalid = payload;
            if (wide) {
                double v = std::numeric_limits<double>::quiet_NaN();
                std::memcpy(invalid.data.data(), &v, 8);
            } else {
                float v = std::numeric_limits<float>::infinity();
                std::memcpy(invalid.data.data(), &v, 4);
            }
            rejects([&] { decode(invalid, options); }, "schema-value");
            auto limited = options;
            limited.max_values = 4;
            rejects([&] { decode(payload, limited); }, "schema-limit");
            limited = options;
            limited.max_depth = 1;
            rejects([&] { decode(payload, limited); }, "schema-limit");
            limited = options;
            unsigned polls = 0;
            limited.control.cancelled = [&] { return ++polls > 8; };
            rejects([&] { decode(payload, limited); }, "cancelled");
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
                auto package = [&](const Bytes &b) {
                    return ZenPackage::parse(collection_zen_fixture(collection_single(b), modern).data, zen);
                };
                const auto before = package(payload),
                           after = package(geometry_value(matrix ? "Matrix" : "Plane", wide, true));
                check(before.property_exports_decoded == 2,
                      "Native matrices decode with both explicit profiles");
                const auto diff = compare_zen_packages(before, after);
                bool found = false;
                for (const auto &c : diff["changes"])
                    found |= c["path"].get<std::string>().find(matrix ? "/value/3/3" : "/value/distance") !=
                             std::string::npos;
                check(found, "Native matrix/plane diffs retain component paths");
            }
        }
        std::cout << checks << " matrix/plane checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
