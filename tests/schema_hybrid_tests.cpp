#include <cyuediff/schema_context.hpp>
#include "schema_hybrid_fixture.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F function, const char *code = nullptr) {
    try {
        function();
    } catch (const Error &e) {
        check(!code || e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected rejection");
}
constexpr const char *root_name = "/Script/Test.Collections";
constexpr const char *field_name = "/Script/Test.Collections.Lookup";
constexpr const char *record_name = "/Script/Test.Record";
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() /
        (".test-tmp-hybrid-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const auto bytes = hybrid_values();
        const auto schemas = UnversionedSchemas::parse(hybrid_mapping().data);
        check(schemas.schema(record_name).struct_kind == 2, "Fixture declares a native serializer");
        UnversionedReadOptions options;
        options.binary_layouts =
            std::make_shared<const BinaryLayouts>(BinaryLayouts::parse(hybrid_layouts()));
        options.struct_tails[record_name] = "Tethers";
        auto decode = [&](const Bytes &b, const UnversionedReadOptions &o) {
            return decode_unversioned(b.data, 0, b.data.size(), schemas, root_name, o);
        };
        const auto decoded = decode(bytes, options);
        const auto &field = decoded.properties.at(field_name);
        const auto &value = field.at("value");
        check(value["properties"]["/Script/Test.Record.Code"]["value"] == 17,
              "Hybrid prefix retains qualified typed properties");
        check(value["native"]["batches"][0][0]["length"] == 2.5 &&
                  value["native"]["batches"][0][1]["particle"] == 2 && value["native"]["batches"][1].empty(),
              "Nested native batches decode after prefix");
        check(decoded.properties["/Script/Test.Collections.After"]["value"] == 123 &&
                  decoded.consumed_bytes == bytes.data.size(),
              "Following property and exact native window");
        check(field["structTailLayout"] == "Tethers" &&
                  field["binaryLayoutsSha256"] == options.binary_layouts->sha256(),
              "Hybrid provenance");
        auto without = options;
        without.struct_tails.clear();
        rejects([&] { decode(bytes, without); }, "schema-type");
        without.reflected_structs.insert(record_name);
        rejects([&] { decode(bytes, without); }, "schema-type");
        for (std::size_t length = 0; length < bytes.data.size(); ++length) {
            auto truncated = bytes;
            truncated.data.resize(length);
            rejects([&] { decode(truncated, options); });
        }
        auto empty_prefix = collection_single(hybrid_record(0, 2.5f, true));
        check(decode(empty_prefix, options).properties[field_name]["value"]["properties"].empty(),
              "Known omitted prefix still has stored native data");
        const auto array_schemas = UnversionedSchemas::parse(hybrid_mapping(true).data);
        Bytes array;
        array.i(2);
        array.append(hybrid_record(11, 1.f));
        array.append(hybrid_record(22, 2.f));
        auto array_bytes = collection_single(array);
        auto array_decoded = decode_unversioned(array_bytes.data, 0, array_bytes.data.size(), array_schemas,
                                                root_name, options);
        check(array_decoded.consumed_bytes == array_bytes.data.size() &&
                  array_decoded.properties[field_name]["value"][1]["properties"]["/Script/Test.Record.Code"]
                                          ["value"] == 22 &&
                  array_decoded.properties[field_name]["value"][1]["native"]["batches"][0][0]["length"] == 2.,
              "Repeated hybrid prefixes/tails do not consume each other");
        for (const auto *kind : {"native", "described", "reflected", "alias", "missing", "catalogue"}) {
            auto bad = options;
            if (std::string(kind) == "native")
                bad.native_structs["Record"] = NativeStructLayout::guid;
            else if (std::string(kind) == "described")
                bad.struct_layouts["Record"] = "Tethers";
            else if (std::string(kind) == "reflected")
                bad.reflected_structs.insert("Record");
            else if (std::string(kind) == "alias")
                bad.struct_tails["Record"] = "Tethers";
            else if (std::string(kind) == "missing")
                bad.struct_tails[record_name] = "Unknown";
            else
                bad.binary_layouts.reset();
            rejects([&] { decode(bytes, bad); }, "schema-context");
        }
        auto broken = bytes;
        broken.patch(9, -1); // Native outer count after enclosing header and prefix.
        rejects([&] { decode(broken, options); });
        broken = bytes;
        broken.patch(13, -1); // First inner count.
        rejects([&] { decode(broken, options); });
        auto nonfinite = hybrid_values(std::numeric_limits<float>::infinity());
        rejects([&] { decode(nonfinite, options); }, "layout-value");
        Bytes zero;
        zero.put<std::uint16_t>(0x0380);
        zero.put<std::uint8_t>(1);
        rejects([&] { decode(zero, options); }, "schema-type");
        auto limited = options;
        limited.max_values = decoded.values_read - 1;
        rejects([&] { decode(bytes, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = decoded.text_bytes - 1;
        rejects([&] { decode(bytes, limited); }, "schema-limit");
        limited = options;
        limited.max_depth = 4;
        rejects([&] { decode(bytes, limited); }, "schema-limit");
        unsigned charged = 0;
        limited = options;
        limited.charge = [&](std::uint32_t work, std::uint64_t) { charged += work; };
        rejects([&] { decode(nonfinite, limited); });
        check(charged > 5, "Enclosing work budget is charged for failed hybrid reads");
        unsigned observations = 0;
        limited = options;
        limited.control.cancelled = [&] { return ++observations > 25; };
        rejects([&] { decode(bytes, limited); }, "cancelled");
        write_hybrid_fixtures(root);
        auto load = [&](const Json &json) {
            const auto file = root / "case.json";
            std::ofstream(file, std::ios::binary) << json.dump();
            return ZenSchemaManifest::read(file);
        };
        for (const auto *profile : {"ue5.3", "ue5.5"}) {
            auto manifest = hybrid_manifest(schemas.sha256(), profile);
            const auto parsed = load(manifest);
            const auto context = parsed.load();
            check(parsed.to_json()["structTailCount"] == 1 &&
                      context->struct_tails.at(record_name) == "Tethers",
                  "Version-four manifest routes hybrid tails");
            auto alternate = *context;
            alternate.struct_tails[record_name] = "Word";
            check(alternate.fingerprint() != context->fingerprint(),
                  "Tail selection changes context identity");
            alternate.struct_tails.clear();
            check(alternate.fingerprint() != context->fingerprint(),
                  "Tail presence changes context identity");
            ZenReadOptions zen;
            zen.profile = context->profile;
            zen.schema_context = context;
            zen.script_objects =
                std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data, zen));
            const bool modern = std::string(profile) == "ue5.5";
            const auto before = ZenPackage::parse(collection_zen_fixture(bytes, modern).data, zen);
            const auto after =
                ZenPackage::parse(collection_zen_fixture(hybrid_values(4.5f), modern).data, zen);
            check(before.property_exports_decoded == 2 &&
                      before.exports[0].property_read["properties"][field_name]["value"] == value,
                  "Zen hybrid values preserve complete prefix and native tail");
            check(compare_zen_packages(before, after).dump().find("native/batches/0/0/length") !=
                      std::string::npos,
                  "Named hybrid tail changes flow through Zen differences");
            auto damaged = bytes;
            damaged.data.resize(20);
            const auto failed = ZenPackage::parse(collection_zen_fixture(damaged, modern).data, zen);
            check(failed.property_exports_failed == 1 &&
                      failed.exports[0].property_read["status"] == "opaque" &&
                      !failed.exports[0].property_read.contains("properties"),
                  "Damaged tail discards whole property result");
            for (const auto *kind :
                 {"version", "missing", "alias", "native", "described", "reflected", "unknown", "type"}) {
                auto bad = manifest;
                const std::string mode = kind;
                if (mode == "version")
                    bad["format"] = "cyuediff.zen-schema/3";
                else if (mode == "missing")
                    bad.erase("structTails");
                else if (mode == "alias")
                    bad["structTails"]["Record"] = "Tethers";
                else if (mode == "native")
                    bad["nativeStructs"]["Record"] = "guid";
                else if (mode == "described")
                    bad["structLayouts"]["Record"] = "Tethers";
                else if (mode == "reflected")
                    bad["reflectedStructs"] = Json::array({"Record"});
                else if (mode == "unknown")
                    bad["structTails"][record_name] = "Unknown";
                else
                    bad["structTails"] = Json::array();
                rejects([&] { load(bad).load(); }, "schema-context");
            }
        }
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures")
            write_hybrid_fixtures(fs::u8path(argv[2]));
        fs::remove_all(root);
        std::cout << checks << " hybrid schema checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks; fixtures: " << root << '\n';
        return 1;
    }
}
