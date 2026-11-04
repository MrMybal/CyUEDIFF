#include <cyuediff/schema_context.hpp>
#include "schema_fixture.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
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
    } catch (const Error &error) {
        check(error.code == code, "Expected schema manifest error");
        return;
    }
    throw std::runtime_error("Expected schema manifest refusal");
}
void save(const fs::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary);
    if (!file.write(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Cannot write synthetic context");
}
} // namespace
int main() {
    const auto parent = fs::weakly_canonical(fs::current_path());
    const auto root = parent / (".test-tmp-schema-context-" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(root))
        return 1;
    auto cleanup = [&] {
        auto resolved = fs::weakly_canonical(root);
        if (resolved.parent_path() == parent &&
            resolved.filename().u8string().rfind(".test-tmp-schema-context-", 0) == 0)
            fs::remove_all(resolved);
    };
    try {
        auto mappings = schema_fixture();
        const auto mapping_path = root / fs::u8path("propri\u00e9t\u00e9s.usmap");
        const auto path = root / fs::u8path("sch\u00e9ma.json");
        mappings.save(mapping_path);
        const auto digest = sha256_file(mapping_path);
        Json json = {{"format", "cyuediff.zen-schema/1"},
                     {"profile", "ue5.3"},
                     {"usmap", mapping_path.filename().u8string()},
                     {"sha256", digest},
                     {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "DemoAsset"}, {"offset", 0}}}}},
                     {"reflectedStructs", Json::array({"Nested"})}};
        auto read = [&](const Json &value) {
            save(path, value.dump());
            return ZenSchemaManifest::read(path);
        };
        auto manifest = read(json);
        check(manifest.mapping_path() == mapping_path && manifest.source_path() == path,
              "Relative Unicode mapping resolves beside the manifest");
        check(manifest.sha256() == sha256_file(path) && manifest.to_json()["bindingCount"] == 1,
              "Manifest provenance and bounded metadata");
        auto context = manifest.load();
        check(context->bindings.at("/Script/Test.DemoAsset").property_offset == 0 &&
                  context->reflected_structs.count("Nested") && context->profile == ZenProfile::unreal_5_3,
              "Validated immutable context bindings");
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto package = ZenPackage::parse(schema_zen_fixture().data, zen);
        check(package.property_exports_decoded == 2 &&
                  package.exports[0].property_read["properties"]["DemoAsset.Health"]["value"] == 42,
              "Manifest context decodes named Zen values");
        auto other = json;
        other["profile"] = "ue5.5";
        check(read(other).load()->profile == ZenProfile::unreal_5_5, "Manifest profile stays explicit");
        other = json;
        other["usmap"] = "absent.usmap";
        auto missing = read(other);
        check(missing.to_json()["coverage"] == "manifest-only", "Manifest read never opens the USMAP");
        rejects([&] { missing.load(); }, "file-open");
        other = json;
        other["sha256"] = std::string(64, '0');
        rejects([&] { read(other).load(); }, "schema-context");
        for (const auto &field : {"format", "profile", "usmap", "sha256", "bindings"}) {
            other = json;
            other.erase(field);
            rejects([&] { read(other); }, "schema-context");
        }
        for (const auto &value : {Json(3), Json(nullptr), Json(""), Json(std::string("x\0y", 3))}) {
            other = json;
            other["usmap"] = value;
            rejects([&] { read(other); }, "schema-context");
        }
        other = json;
        other["extra"] = true;
        rejects([&] { read(other); }, "schema-context");
        other = json;
        other["profile"] = "automatic";
        rejects([&] { read(other); }, "schema-context");
        other = json;
        other["sha256"] = "invalid";
        rejects([&] { read(other); }, "schema-context");
        for (auto offset : {Json(-1), Json(1.5), Json("0"), Json(1e30)}) {
            other = json;
            other["bindings"]["/Script/Test.DemoAsset"]["offset"] = offset;
            rejects([&] { read(other); }, "schema-context");
        }
        other = json;
        other["bindings"]["/Script/Test.DemoAsset"]["extra"] = true;
        rejects([&] { read(other); }, "schema-context");
        other = json;
        other["bindings"] = {{"DemoAsset", {{"schema", "DemoAsset"}, {"offset", 0}}}};
        rejects([&] { read(other); }, "schema-context");
        other = json;
        other["bindings"]["/Script/Test.DemoAsset"]["schema"] = "Missing";
        rejects([&] { read(other).load(); }, "schema-missing");
        other = json;
        other["reflectedStructs"] = Json::array({"Nested", "Nested"});
        rejects([&] { read(other); }, "schema-context");
        other = json;
        other["reflectedStructs"] = "Nested";
        rejects([&] { read(other); }, "schema-context");
        save(path, "{\"format\":\"a\",\"format\":\"b\"}");
        rejects([&] { ZenSchemaManifest::read(path); }, "schema-context");
        save(path, "{");
        rejects([&] { ZenSchemaManifest::read(path); }, "schema-context");
        save(path, std::string("\xff", 1));
        rejects([&] { ZenSchemaManifest::read(path); }, "schema-context");
        manifest = read(json);
        ZenSchemaManifestOptions limits;
        limits.max_bytes = 1;
        rejects([&] { ZenSchemaManifest::read(path, limits); }, "schema-limit");
        limits = {};
        limits.max_text_bytes = 1;
        rejects([&] { ZenSchemaManifest::read(path, limits); }, "schema-limit");
        limits = {};
        limits.max_string_bytes = 3;
        rejects([&] { ZenSchemaManifest::read(path, limits); }, "schema-limit");
        limits = {};
        limits.max_entries = 1;
        rejects([&] { ZenSchemaManifest::read(path, limits); }, "schema-limit");
        limits = {};
        limits.max_depth = 1;
        rejects([&] { ZenSchemaManifest::read(path, limits); }, "schema-limit");
        limits = {};
        limits.control.cancelled = [] { return true; };
        rejects([&] { ZenSchemaManifest::read(path, limits); }, "cancelled");
        SchemaReadOptions options;
        options.control.cancelled = [] { return true; };
        rejects([&] { manifest.load(options); }, "cancelled");
        options = {};
        options.max_bytes = 1;
        rejects([&] { manifest.load(options); }, "schema-limit");
        check(sha256_file(mapping_path) == digest, "Reads and failures preserve mapping bytes");
        cleanup();
        std::cout << checks << " schema context checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        cleanup();
        return 1;
    }
}
