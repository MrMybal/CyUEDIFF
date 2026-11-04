#include <cyuediff/schema.hpp>
#include <cyuediff/zen.hpp>
#include "text_format_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected text transform refusal");
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-text-transform-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const std::string field = "/Script/Test.Collections.Lookup";
        auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_type(11)).data);
        UnversionedReadOptions options;
        options.text_layout = TextLayout::ue5_history;
        auto decode = [&](const Bytes &payload, const UnversionedReadOptions &opts) {
            auto bytes = collection_single(payload);
            return decode_unversioned(bytes.data, 0, bytes.data.size(), schemas, "Collections", opts);
        };
        for (unsigned operation : {0u, 1u}) {
            auto payload = transform_text_value(text_value("MiXeD source"), operation);
            const auto result = decode(payload, options);
            const auto expected = Json{{"flags", 0},
                                       {"history", 10},
                                       {"kind", "transform"},
                                       {"source",
                                        {{"flags", 0x40000000},
                                         {"history", 0},
                                         {"kind", "localized"},
                                         {"namespace", "Stored Namespace"},
                                         {"key", "StableKey"},
                                         {"source", "MiXeD source"}}},
                                       {"transformType", operation}};
            check(result.properties[field]["value"] == expected,
                  "Transform retains stored source and operation without applying case conversion");
            check(result.consumed_bytes == payload.data.size() + 2, "Exact recursive transform window");
            for (int version : {1009, 1013}) {
                const auto path =
                    save(root, "transform", text_property_fixture(payload, "TextProperty", version));
                const auto tagged = Package::open(path);
                check(tagged.exports[0].properties["Formatted"]["value"] == expected,
                      "Classic and cooked transform JSON agree");
                check(tagged.exports[0].properties["AfterFormat"]["value"] == 876,
                      "Following tagged property remains aligned");
            }
            for (std::size_t end = 0; end < payload.data.size(); ++end) {
                auto cut = payload;
                cut.data.resize(end);
                rejects([&] { decode(cut, options); }, "bounds");
                const auto tagged = Package::open(save(root, "cut", text_property_fixture(cut)));
                check(tagged.exports[0].properties["Formatted"].contains("opaque") &&
                          tagged.exports[0].properties["AfterFormat"]["value"] == 876,
                      "Every truncated tagged transform retains opaque bytes and next property");
            }
        }
        auto nested = transform_text_value(transform_text_value(invariant_text_value(), 1), 0);
        check(decode(nested, options).properties[field]["value"]["source"]["source"]["source"] ==
                  "Invariant source",
              "Nested transform chain retains original invariant source");
        for (unsigned operation : {2u, 255u}) {
            auto bad = transform_text_value(text_value(), operation);
            rejects([&] { decode(bad, options); }, "schema-value");
            auto tagged = Package::open(save(root, "operation", text_property_fixture(bad)));
            check(tagged.exports[0].properties["Formatted"]["opaque"]["reason"] == "text-value",
                  "Unknown transform operation never invents semantics");
        }
        auto unknown = text_value();
        unknown.data[4] = 12;
        rejects([&] { decode(transform_text_value(unknown), options); }, "schema-type");
        auto limited = options;
        limited.max_depth = 3;
        rejects([&] { decode(nested, limited); }, "schema-limit");
        limited = options;
        limited.max_values = 5;
        rejects([&] { decode(nested, limited); }, "schema-limit");
        limited = options;
        limited.max_text_bytes = field.size() + 3;
        rejects([&] { decode(nested, limited); }, "schema-limit");
        unsigned polls = 0;
        limited = options;
        limited.control.cancelled = [&] { return ++polls > 20; };
        rejects([&] { decode(nested, limited); }, "cancelled");
        auto deep = invariant_text_value(false);
        for (unsigned i = 0; i < 60; ++i)
            deep = transform_text_value(deep);
        rejects([&] { decode(deep, options); }, "schema-limit");
        auto taggedDeep = Package::open(save(root, "deep", text_property_fixture(deep)));
        check(taggedDeep.exports[0].properties["Formatted"]["opaque"]["reason"] == "limit",
              "Classic recursion also stays bounded");
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(schemas);
        context->bindings["/Script/Test.DemoAsset"] = {"Collections", 0};
        ZenReadOptions zen;
        zen.profile = context->profile;
        zen.schema_context = context;
        zen.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        const auto before =
            ZenPackage::parse(collection_zen_fixture(collection_single(transform_text_value())).data, zen);
        const auto after = ZenPackage::parse(
            collection_zen_fixture(collection_single(transform_text_value(text_value("Edited source"), 1)))
                .data,
            zen);
        const auto diff = compare_zen_packages(before, after);
        bool source = false, operation = false;
        for (const auto &change : diff["changes"]) {
            const auto path = change["path"].get<std::string>();
            if (path.find("/value/source/source") != std::string::npos)
                source = change["before"] == "Original source" && change["after"] == "Edited source";
            if (path.find("/value/transformType") != std::string::npos)
                operation = change["before"] == 0 && change["after"] == 1;
        }
        check(source && operation, "Nested text source and transform operation have distinct diff paths");
        auto invalid = ZenPackage::parse(
            collection_zen_fixture(collection_single(transform_text_value(text_value(), 9))).data, zen);
        check(invalid.exports[0].property_read["status"] == "opaque" &&
                  !invalid.exports[0].property_read.contains("properties"),
              "Invalid recursive text publishes no partial property tree");
        context->profile = ZenProfile::unreal_5_5;
        zen.profile = context->profile;
        check(ZenPackage::parse(collection_zen_fixture(collection_single(nested), true).data, zen)
                      .property_exports_decoded == 2,
              "Modern profile forwards recursive text layout");
        fs::remove_all(root);
        std::cout << checks << " text transform checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
