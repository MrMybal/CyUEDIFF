#include <cyuediff/schema_context.hpp>
#include "schema_native_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool b, const char *m) {
    ++checks;
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f, const char *code = nullptr) {
    try {
        f();
    } catch (const Error &e) {
        check(!code || e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected rejection");
}
Json field(const char *name, Json value) {
    return {{"name", name}, {"value", value}};
}
Json array(unsigned count) {
    return {{"type", "array"}, {"count", count}, {"element", {{"type", "f64"}}}};
}
Json description() {
    Json record = {{"type", "record"},
                   {"fields", Json::array({field("rotation", array(4)), field("translation", array(3)),
                                           field("scale", array(3))})}};
    return {{"format", "cyuediff.binary-layouts/1"}, {"layouts", {{"Transform", record}}}};
}
} // namespace
int main(int argc, char **argv) {
    auto root =
        fs::current_path() / (".test-tmp-schema-layouts-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        write_native_fixtures(root);
        const auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_struct()).data);
        const auto described = std::make_shared<const BinaryLayouts>(BinaryLayouts::parse(description()));
        UnversionedReadOptions options;
        options.binary_layouts = described;
        options.struct_layouts["/Script/Test.Record"] = "Transform";
        auto decode = [&](const Bytes &b, const UnversionedReadOptions &o) {
            return decode_unversioned(b.data, 0, b.data.size(), schemas, "/Script/Test.Collections", o);
        };
        const auto bytes = native_transform_values();
        const auto value = decode(bytes, options);
        const auto key = "/Script/Test.Collections.Lookup";
        check(value.properties[key]["value"]["translation"][0] == 1000000000000.125,
              "Cooked declarative native fields");
        check(value.properties[key]["binaryLayout"] == "Transform" &&
                  value.properties[key]["binaryLayoutsSha256"] == described->sha256(),
              "Layout provenance");
        check(value.consumed_bytes == bytes.data.size(), "Exact described consumption");
        for (std::size_t i = 0; i < bytes.data.size(); ++i) {
            auto b = bytes;
            b.data.resize(i);
            rejects([&] { decode(b, options); }, "bounds");
        }
        Bytes zero;
        zero.put<std::uint16_t>(0x0380);
        zero.put<std::uint8_t>(1);
        rejects([&] { decode(zero, options); }, "schema-type");
        auto conflict = options;
        conflict.reflected_structs.insert("Record");
        rejects([&] { decode(bytes, conflict); }, "schema-context");
        conflict = options;
        conflict.native_structs["Record"] = NativeStructLayout::transform_f64;
        rejects([&] { decode(bytes, conflict); }, "schema-context");
        conflict = options;
        conflict.struct_layouts["Record"] = "Transform";
        rejects([&] { decode(bytes, conflict); }, "schema-context");
        conflict = options;
        conflict.binary_layouts.reset();
        rejects([&] { decode(bytes, conflict); }, "schema-context");
        auto limits = options;
        limits.max_values = 7;
        rejects([&] { decode(bytes, limits); }, "schema-limit");
        limits = options;
        limits.max_text_bytes = 100;
        rejects([&] { decode(bytes, limits); }, "schema-limit");
        limits = options;
        limits.control.cancelled = [] { return true; };
        rejects([&] { decode(bytes, limits); }, "cancelled");
        auto references = description();
        references["layouts"]["References"] = {
            {"type", "record"},
            {"fields", Json::array({field("name", {{"type", "name"}}), field("object", {{"type", "object"}}),
                                    field("guid", {{"type", "guid"}})})}};
        auto ref_options = options;
        ref_options.binary_layouts = std::make_shared<const BinaryLayouts>(BinaryLayouts::parse(references));
        ref_options.struct_layouts["/Script/Test.Record"] = "References";
        const std::vector<std::string> names = {"None", "Preset"};
        ref_options.names = &names;
        ref_options.reference = [](std::int32_t index) -> Json {
            return {{"kind", "export"}, {"index", index}};
        };
        Bytes ref_bytes;
        collection_name(ref_bytes, 1);
        ref_bytes.i(-1);
        for (unsigned i = 1; i <= 4; ++i)
            ref_bytes.i(i);
        const auto ref_value = decode(collection_single(ref_bytes), ref_options).properties[key]["value"];
        check(ref_value["name"] == "Preset" && ref_value["object"]["index"] == -1 &&
                  ref_value["guid"] == "00000001000000020000000300000004",
              "Cooked layout names, JSON identities and GUIDs");
        auto null_bytes = ref_bytes;
        null_bytes.patch(8, 0);
        ref_options.reference = [](std::int32_t) -> Json {
            throw Error("test", "Null resolver must not run");
        };
        check(decode(collection_single(null_bytes), ref_options).properties[key]["value"]["object"].is_null(),
              "Null layout reference");
        ref_options.names = nullptr;
        rejects([&] { decode(collection_single(ref_bytes), ref_options); }, "schema-context");
        ref_options.names = &names;
        ref_options.reference = [](std::int32_t) -> Json { return Json::binary({1, 2}); };
        rejects([&] { decode(collection_single(ref_bytes), ref_options); }, "layout-value");
        const auto mapping = UnversionedSchemas::read(root / "native.usmap");
        auto manifest = [&](const char *profile) {
            return Json{
                {"format", "cyuediff.zen-schema/3"},
                {"profile", profile},
                {"usmap", "native.usmap"},
                {"sha256", mapping.sha256()},
                {"bindings",
                 {{"/Script/Test.DemoAsset", {{"schema", "/Script/Test.Collections"}, {"offset", 0u}}}}},
                {"nativeStructs", Json::object()},
                {"binaryLayouts", description()},
                {"structLayouts", {{"/Script/Test.Record", "Transform"}}}};
        };
        auto load = [&](const Json &j) {
            const auto path = root / "described.json";
            std::ofstream(path) << j.dump();
            return ZenSchemaManifest::read(path);
        };
        for (const auto *profile : {"ue5.3", "ue5.5"}) {
            auto m = load(manifest(profile));
            auto context = m.load();
            check(m.to_json()["structLayoutCount"] == 1 &&
                      context->struct_layouts.at("/Script/Test.Record") == "Transform",
                  "Version three manifest bindings");
            ZenReadOptions zen;
            zen.profile = context->profile;
            zen.schema_context = context;
            zen.script_objects =
                std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data, zen));
            const auto raw = collection_zen_fixture(bytes, profile == std::string("ue5.5"));
            auto p = ZenPackage::parse(raw.data, zen);
            check(p.property_exports_decoded == 2 &&
                      p.exports[0].property_read["properties"][key]["value"]["translation"][1] == 2.5,
                  "Zen packages route through descriptions");
            auto changed =
                collection_zen_fixture(native_transform_values(true), profile == std::string("ue5.5"));
            auto q = ZenPackage::parse(changed.data, zen);
            auto diff = compare_zen_packages(p, q);
            check(diff.dump().find("translation/1") != std::string::npos,
                  "Named field diff for declared cooked structures");
            auto broken = bytes;
            broken.data.pop_back();
            auto b =
                ZenPackage::parse(collection_zen_fixture(broken, profile == std::string("ue5.5")).data, zen);
            check(b.property_exports_failed == 1 && b.exports[0].property_read["status"] == "opaque" &&
                      !b.exports[0].property_read.contains("properties"),
                  "Malformed cooked structure retains full opaque body");
            auto other = *context;
            other.struct_layouts.clear();
            check(other.fingerprint() != context->fingerprint(), "Layout binding changes context identity");
            zen.max_property_values = 5;
            rejects([&] { ZenPackage::parse(raw.data, zen); }, "schema-limit");
        }
        auto invalid = manifest("ue5.3");
        invalid["structLayouts"]["Record"] = "Missing";
        rejects([&] { load(invalid); }, "schema-context");
        invalid = manifest("ue5.3");
        invalid["nativeStructs"]["Record"] = "guid";
        rejects([&] { load(invalid).load(); }, "schema-context");
        invalid = manifest("ue5.3");
        invalid["reflectedStructs"] = Json::array({"Record"});
        rejects([&] { load(invalid).load(); }, "schema-context");
        invalid = manifest("ue5.3");
        invalid["format"] = "cyuediff.zen-schema/2";
        rejects([&] { load(invalid); }, "schema-context");
        for (const auto *name : {"binaryLayouts", "structLayouts"}) {
            invalid = manifest("ue5.3");
            invalid.erase(name);
            rejects([&] { load(invalid); }, "schema-context");
        }
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures") {
            auto out = fs::u8path(argv[2]);
            write_native_fixtures(out);
            for (const auto *profile : {"ue5.3", "ue5.5"})
                std::ofstream(out / (std::string("described-") + profile + ".json"))
                    << manifest(profile).dump(2);
        }
        fs::remove_all(root);
        std::cout << checks << " schema layout checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks\nFixtures: " << root << '\n';
        return 1;
    }
}
