#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "text_format_fixture.hpp"
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
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected generator refusal");
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-generator-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_type(11)).data);
        std::vector<std::string> names = {"None", "Unused", "PluginGenerator"};
        UnversionedReadOptions options;
        options.names = &names;
        options.text_layout = TextLayout::ue5_history;
        auto decode = [&](const Bytes &payload, const UnversionedReadOptions &opt) {
            auto wire = collection_single(payload);
            return decode_unversioned(wire.data, 0, wire.data.size(), schemas, "Collections", opt);
        };
        for (int nameIndex : {0, 2})
            for (unsigned size : {0u, 1u, 31u, 65537u}) {
                std::vector<unsigned char> contents(size);
                for (unsigned i = 0; i < size; ++i)
                    contents[i] = static_cast<unsigned char>(i);
                auto wire = generator_text_value(nameIndex, contents);
                auto decoded = decode(wire, options);
                const auto &value = decoded.properties[field]["value"];
                check(value["kind"] == "text-generator" &&
                          value["generator"] == (nameIndex ? "PluginGenerator" : "None"),
                      "Generator identity resolved without registration or execution");
                if (nameIndex)
                    check(value["contents"] == Json{{"bytes", size}, {"sha256", sha256_bytes(contents)}},
                          "Unknown binary contents retain exact size and fingerprint");
                else
                    check(value["contents"].is_null(), "None generator has no buffer count");
                check(decoded.consumed_bytes == wire.data.size() + 2,
                      "Exact generator extent including empty payload");
                auto classic = Package::open(save(root, "generator", text_property_fixture(wire)));
                check(classic.exports[0].properties["Formatted"]["value"]["contents"] == value["contents"] &&
                          classic.exports[0].properties["AfterFormat"]["value"] == 876,
                      "Classic generator preserves bytes and following tags");
                if (size < 32)
                    for (std::size_t end = 0; end < wire.data.size(); ++end) {
                        auto cut = wire;
                        cut.data.resize(end);
                        rejects([&] { decode(cut, options); }, "bounds");
                    }
            }
        auto suffix = decode(generator_text_value(0, {7}, 1), options).properties[field]["value"];
        check(suffix["generator"] == "None_0" && suffix["contents"]["bytes"] == 1,
              "A numbered None is a nonempty generator identity");
        auto bad = generator_text_value();
        bad.patch(13, -1);
        rejects([&] { decode(bad, options); }, "schema-value");
        bad.patch(13, 10000001);
        rejects([&] { decode(bad, options); }, "schema-limit");
        rejects([&] { decode(generator_text_value(8), options); }, "schema-context");
        auto limited = options;
        limited.names = nullptr;
        rejects([&] { decode(generator_text_value(), limited); }, "schema-context");
        limited = options;
        limited.max_text_bytes = field.size() + names[2].size() + 65;
        rejects([&] { decode(generator_text_value(), limited); }, "schema-limit");
        limited = options;
        limited.max_values = 4;
        rejects([&] { decode(generator_text_value(), limited); }, "schema-limit");
        auto nested = ordered_text_value({text_argument(4, generator_text_value())});
        check(decode(nested, options).properties[field]["value"]["arguments"][0]["value"]["generator"] ==
                  "PluginGenerator",
              "Recursive generator histories");
        limited = options;
        limited.max_depth = 3;
        rejects([&] { decode(nested, limited); }, "schema-limit");
        limited = options;
        unsigned polls = 0;
        limited.control.cancelled = [&] { return ++polls > 14; };
        rejects([&] { decode(generator_text_value(2, std::vector<unsigned char>(200000)), limited); },
                "cancelled");
        auto context = std::make_shared<ZenSchemaContext>();
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        for (bool modern : {false, true}) {
            context->profile = modern ? ZenProfile::unreal_5_5 : ZenProfile::unreal_5_3;
            zen.profile = context->profile;
            auto package = [&](const Bytes &text) {
                return ZenPackage::parse(collection_zen_fixture(collection_single(text), modern).data, zen);
            };
            auto before = package(generator_text_value()),
                 after = package(generator_text_value(2, {0, 1, 254, 7}));
            check(before.property_exports_decoded == 2 &&
                      before.exports[0].property_read["properties"][field]["value"]["generator"] == "Example",
                  "Generator resolves package names in both profiles");
            const auto diff = compare_zen_packages(before, after);
            bool precise = false;
            for (const auto &c : diff["changes"])
                precise |= c["path"].get<std::string>().find("/value/contents/sha256") != std::string::npos;
            check(precise, "Same-size opaque generator changes remain visible");
            auto cut = generator_text_value();
            cut.data.pop_back();
            auto opaque = package(cut);
            check(opaque.exports[0].property_read["status"] == "opaque" &&
                      !opaque.exports[0].property_read.contains("properties"),
                  "Truncated generator discards entire mapped read");
        }
        fs::remove_all(root);
        std::cout << checks << " generator checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
