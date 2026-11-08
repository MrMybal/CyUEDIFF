#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "schema_native_fixture.hpp"
#include <iostream>
#include <fstream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    int checks = 0;
    try {
        const auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_struct()).data);
        const std::string field = "/Script/Test.Collections.Lookup";
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto root = fs::u8path(argv[2]);
            fs::create_directories(root);
            auto mapping = collection_schema_fixture(collection_struct());
            mapping.save(root / "native.usmap");
            auto global = script_fixture();
            global.save(root / "global.ucas");
            zen_toc(global, 5).save(root / "global.utoc");
            for (const auto &token :
                 {"per-platform-float-cooked", "per-platform-int-cooked", "per-platform-bool-cooked"}) {
                const std::string layout = token;
                Json manifest = {
                    {"format", "cyuediff.zen-schema/2"},
                    {"profile", "ue5.3"},
                    {"usmap", "native.usmap"},
                    {"sha256", sha256_bytes(mapping.data)},
                    {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "Collections"}, {"offset", 0}}}}},
                    {"reflectedStructs", Json::array()},
                    {"nativeStructs", {{"/Script/Test.Record", layout}}}};
                std::ofstream(root / (layout + ".json")) << manifest.dump(2);
                for (int mode = 0; mode < 3; ++mode) {
                    Bytes values;
                    values.i(mode == 2 ? 0 : 1);
                    if (layout == "per-platform-float-cooked")
                        values.put<float>(mode == 1 ? -3.5f : 1.25f);
                    else
                        values.i(mode == 1 ? 0 : 1);
                    auto bytes = collection_zen_fixture(collection_single(values));
                    const auto name = layout + (mode == 0 ? "-before" : mode == 1 ? "-after" : "-invalid");
                    bytes.save(root / (name + ".ucas"));
                    zen_toc(bytes).save(root / (name + ".utoc"));
                }
            }
            return 0;
        }

        auto check = [&](bool good, const char *message) {
            ++checks;
            if (!good)
                throw std::runtime_error(message);
        };
        auto rejected = [&](const Bytes &wire, const UnversionedReadOptions &options, const char *code) {
            try {
                decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", options);
            } catch (const Error &error) {
                check(error.code == code, "Expected native per-platform refusal");
                return;
            }
            throw std::runtime_error("Invalid per-platform input was accepted");
        };
        for (const auto &name :
             {"per-platform-float-cooked", "per-platform-int-cooked", "per-platform-bool-cooked"}) {
            const auto layout = parse_native_struct_layout(name);
            const bool floating = layout == NativeStructLayout::per_platform_float_cooked;
            const bool boolean = layout == NativeStructLayout::per_platform_bool_cooked;
            check(std::string(native_struct_layout_name(layout)) == name,
                  "Per-platform layout identity round trip");
            UnversionedReadOptions options;
            options.native_structs["/Script/Test.Record"] = layout;
            auto payload = [&](int edited = 0) {
                Bytes bytes;
                bytes.i(1);
                if (floating)
                    bytes.put<float>(edited ? -3.5f : 1.25f);
                else
                    bytes.i(boolean ? (edited ? 0 : 1)
                                    : (edited ? 17 : std::numeric_limits<std::int32_t>::min()));
                return bytes;
            };
            auto wire = collection_single(payload());
            auto decode = [&](const Bytes &data) {
                return decode_unversioned(data.data, 0, data.data.size(), schemas, "Collections", options);
            };
            const Json expected = floating  ? Json(1.25)
                                  : boolean ? Json(true)
                                            : Json(std::numeric_limits<std::int32_t>::min());
            const auto result = decode(wire);
            check(result.properties[field]["value"] == Json{{"platformValue", expected}},
                  "Stored cooked platform value is typed");
            check(result.properties[field]["nativeLayout"] == name && result.consumed_bytes == 10,
                  "Cooked bool32 marker and value consume exactly eight payload bytes");
            auto tail = wire;
            tail.zeros(11);
            check(decode(tail).consumed_bytes == wire.data.size(), "Remaining native data is not consumed");
            for (std::size_t size = 0; size < wire.data.size(); ++size) {
                auto truncated = wire;
                truncated.data.resize(size);
                rejected(truncated, options, "bounds");
            }
            for (int flag : {0, 2, -1}) {
                auto invalid = wire;
                invalid.patch(2, flag);
                rejected(invalid, options, "schema-value");
            }
            if (floating) {
                for (float value :
                     {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                    auto invalid = wire;
                    std::memcpy(invalid.data.data() + 6, &value, 4);
                    rejected(invalid, options, "schema-value");
                }
            }
            if (boolean) {
                auto invalid = wire;
                invalid.patch(6, 2);
                rejected(invalid, options, "schema-value");
            }
            Bytes zero;
            zero.put<std::uint16_t>(0x0380);
            zero.put<std::uint8_t>(1);
            auto cleared = decode(zero);
            check(cleared.consumed_bytes == 3 && cleared.properties[field]["serializedZero"] &&
                      cleared.properties[field]["value"]["platformValue"] ==
                          (boolean ? Json(false) : Json(0)),
                  "Zero mask supplies zero value without inventing a serialized marker");
            auto limited = options;
            limited.max_values = 2;
            rejected(wire, limited, "schema-limit");
            limited = options;
            limited.max_depth = 0;
            rejected(wire, limited, "schema-limit");
            auto cancelled = options;
            cancelled.control.cancelled = [] { return true; };
            rejected(wire, cancelled, "cancelled");
            for (const bool modern : {false, true}) {
                auto context = std::make_shared<ZenSchemaContext>();
                context->profile = modern ? ZenProfile::unreal_5_5 : ZenProfile::unreal_5_3;
                context->schemas = std::make_shared<UnversionedSchemas>(schemas);
                context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
                context->native_structs = options.native_structs;
                ZenReadOptions read;
                read.profile = context->profile;
                read.schema_context = context;
                read.script_objects =
                    std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
                const auto before = ZenPackage::parse(collection_zen_fixture(wire, modern).data, read);
                const auto after = ZenPackage::parse(
                    collection_zen_fixture(collection_single(payload(1)), modern).data, read);
                check(before.property_exports_decoded == 2 && before.property_exports_failed == 0,
                      "Explicit cooked context binds per-platform native values");
                const auto changes = compare_zen_packages(before, after)["changes"];
                bool named = false;
                for (const auto &change : changes)
                    if (change["path"].get<std::string>().find("/platformValue") != std::string::npos)
                        named = true;
                check(named, "General cooked diff names the stored platform value");
            }
        }
        std::cout << checks << " native per-platform value checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
