#include "tagged_hybrid_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool v, const char *m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-tagged-hybrid-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        const auto catalog = hybrid_catalogue();
        const auto layouts = BinaryLayouts::parse(catalog);
        ReadOptions options;
        options.decoders = layouts.decoder_registry();
        for (int version : {0, 1004, 1010, 1011, 1012, 1013}) {
            save_tagged_hybrid(root, "before", tagged_hybrid_fixture(version));
            save_tagged_hybrid(root, "after", tagged_hybrid_fixture(version, true));
            auto before = Package::open(root / "before.uasset", options),
                 after = Package::open(root / "after.uasset", options);
            const auto &props = before.exports.at(0).properties;
            const auto &value = props.at("Native").at("value");
            check(value.at("prefix") == "tagged-properties" &&
                      value.at("data").at("properties").at("Mode").at("value") == 7,
                  "Tagged prefix decoded explicitly");
            check(value["data"]["native"]["samples"] == Json::array({.25f, .5f, .75f}) &&
                      value["data"]["native"]["name"] == "TailName",
                  "Tail begins at exact None boundary with package name resolution");
            check(value["data"]["properties"]["Nested"]["value"]["data"]["native"]["stamp"] ==
                      9007199254741004ll,
                  "Nested hybrid retains exact int64 native fields");
            check(value["data"]["properties"]["Records"]["value"].size() == 2 &&
                      value["data"]["properties"]["Records"]["value"][1]["data"]["properties"]["Marker"]
                           ["value"] == 22,
                  "Repeated hybrids preserve element boundaries");
            check(props.at("AfterNative").at("value") == 987, "Following property is preserved");
            auto differences = compare(before, after).to_json().dump();
            check(differences.find("native/samples/1") != std::string::npos &&
                      differences.find("properties/Mode/value") != std::string::npos,
                  "Both prefix and tail have named differences");
            auto unbound = Package::open(root / "before.uasset");
            check(unbound.exports[0].properties["Native"].contains("opaque") &&
                      !unbound.exports[0].properties["Native"].contains("value"),
                  "No implicit native-tail discovery");
            save_tagged_hybrid(root, "unknown", tagged_hybrid_fixture(version, false, SIZE_MAX, true));
            auto unknown = Package::open(root / "unknown.uasset", options);
            check(unknown.exports[0].properties["Native"]["opaque"]["reason"] == "hybrid-prefix" &&
                      !unknown.exports[0].properties["Native"].contains("value") &&
                      unknown.exports[0].properties["AfterNative"]["value"] == 987,
                  "Unknown prefix discards the entire hybrid and preserves next property");
            auto empty = catalog;
            empty["layouts"]["InnerTail"]["fields"] = Json::array();
            auto eo = options;
            eo.decoders = BinaryLayouts::parse(empty).decoder_registry();
            save_tagged_hybrid(root, "empty", tagged_hybrid_fixture(version, false, SIZE_MAX, false, true));
            auto ep = Package::open(root / "empty.uasset", eo);
            check(ep.exports[0].properties["Native"].contains("value") &&
                      ep.exports[0].properties["AfterNative"]["value"] == 987,
                  "Empty native tails remain valid when a tagged prefix made progress");
        }
        save_tagged_hybrid(root, "collections",
                           tagged_hybrid_fixture(1013, false, SIZE_MAX, false, false, true));
        auto col = Package::open(root / "collections.uasset", options);
        auto &cp = col.exports[0].properties["Native"]["value"]["data"]["properties"];
        check(cp["Lookup"]["value"]["entries"]["\"TailName\""]["value"]["data"]["properties"]["Marker"]
                ["value"] == 31,
              "Full-type map hybrid value");
        check(cp["Selected"]["value"]["entries"].size() == 1, "Full-type set hybrid identity");
        save_tagged_hybrid(root, "legacy-map",
                           tagged_hybrid_fixture(1004, false, SIZE_MAX, false, false, true));
        auto legacy = Package::open(root / "legacy-map.uasset", options);
        check(legacy.exports[0].properties["Native"].contains("opaque"),
              "Missing legacy map struct identity is not inferred");
        // Every truncated serialized prefix/tail is a whole opaque property, never a usable partial hybrid.
        const auto complete = tagged_hybrid_fixture();
        std::size_t valid_length = 0;
        for (std::size_t n = 0; n < 4096; ++n) {
            auto f = tagged_hybrid_fixture(1013, false, n);
            if (f.script.data == complete.script.data) {
                valid_length = n;
                break;
            }
            save_tagged_hybrid(root, "truncated", std::move(f));
            auto p = Package::open(root / "truncated.uasset", options);
            check(p.exports[0].properties["Native"].contains("opaque") &&
                      p.exports[0].properties["AfterNative"]["value"] == 987,
                  "Truncation remains opaque and local");
        }
        check(valid_length > 100, "All native and tagged byte prefixes exercised");
        save_tagged_hybrid(root, "good", complete);
        auto normal = Package::open(root / "good.uasset", options);
        auto conditional_catalog = catalog;
        conditional_catalog["layouts"]["AfterTail"] = conditional_catalog["layouts"]["OuterTail"];
        conditional_catalog["layouts"]["AfterTail"]["fields"][0]["name"] = "afterSamples";
        const auto conditional_layouts = BinaryLayouts::parse(conditional_catalog);
        auto conditional = *options.decoders->find(DecoderKind::struct_value, "HybridOuter", normal.summary);
        conditional.id = "original.prefix-conditioned/1";
        unsigned callbacks = 0;
        conditional.decode = [&](DecoderInput &input, const DecoderContext &context) {
            ++callbacks;
            check(context.tagged_properties != nullptr,
                  "Host tail receives a borrowed complete tagged prefix");
            BinaryLayoutReadOptions tail_options;
            tail_options.charge = context.charge;
            const auto mode = context.tagged_properties->at("Mode").at("value").get<int>();
            return conditional_layouts.decode(input, mode == 7 ? "OuterTail" : "AfterTail", tail_options);
        };
        auto conditional_options = options;
        const auto inner_decoder =
            *options.decoders->find(DecoderKind::struct_value, "HybridInner", normal.summary);
        conditional_options.decoders =
            std::make_shared<const DecoderRegistry>(std::vector<AssetDecoder>{inner_decoder, conditional});
        save_tagged_hybrid(root, "conditional-after", tagged_hybrid_fixture(1013, true));
        const auto conditional_before = Package::open(root / "good.uasset", conditional_options);
        const auto conditional_after = Package::open(root / "conditional-after.uasset", conditional_options);
        check(
            callbacks == 2 &&
                conditional_before.exports[0].properties["Native"]["value"]["data"]["native"]["samples"][1] ==
                    .5 &&
                conditional_after.exports[0]
                        .properties["Native"]["value"]["data"]["native"]["afterSamples"][1] == 1.5,
            "Known prefix fields can select a host's native-tail format");
        callbacks = 0;
        save_tagged_hybrid(root, "conditional-unknown", tagged_hybrid_fixture(1013, false, SIZE_MAX, true));
        const auto unavailable_prefix =
            Package::open(root / "conditional-unknown.uasset", conditional_options);
        check(callbacks == 0 && unavailable_prefix.exports[0].properties["Native"].contains("opaque"),
              "An incomplete prefix is never supplied to the tail callback");
        conditional.prefix = DecoderPrefix::none;
        conditional.decode = [&](DecoderInput &, const DecoderContext &context) -> Json {
            check(context.tagged_properties == nullptr,
                  "Whole-value callbacks do not receive a fabricated prefix");
            throw Error("original-whole", "Expected original whole-value rejection");
        };
        conditional_options.decoders =
            std::make_shared<const DecoderRegistry>(std::vector<AssetDecoder>{conditional});
        check(Package::open(root / "good.uasset", conditional_options)
                      .exports[0]
                      .properties["Native"]["opaque"]["reason"] == "original-whole",
              "Existing whole-value callbacks retain their boundary");
        auto shallow = options;
        shallow.max_depth = 2;
        check(Package::open(root / "good.uasset", shallow).exports[0].properties["Native"].contains("opaque"),
              "Nested prefix/tail depth remains bounded");
        auto limited = options;
        limited.max_properties = 100;
        auto bounded = Package::open(root / "good.uasset", limited);
        check(bounded.exports[0].properties.empty() ||
                  bounded.exports[0].properties["Native"].contains("opaque"),
              "Prefix and native work share enclosing limits");
        bool cancelled = false;
        auto cancel = options;
        cancel.control.cancelled = [] { return true; };
        try {
            Package::open(root / "good.uasset", cancel);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Cancellation propagates");
        for (const auto &bad : std::vector<Json>{Json("guess"), Json(true), Json(nullptr)}) {
            auto d = catalog;
            d["decoders"][0]["prefix"] = bad;
            bool rejected = false;
            try {
                BinaryLayouts::parse(d);
            } catch (const Error &) {
                rejected = true;
            }
            check(rejected, "Unknown prefixes rejected before reading");
        }
        auto invalid = catalog;
        invalid["decoders"][0]["kind"] = "native-export";
        bool rejected = false;
        try {
            BinaryLayouts::parse(invalid);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected, "Native export cannot re-read a tagged prefix");
        auto overlaps = catalog;
        auto whole = overlaps["decoders"][0];
        whole.erase("prefix");
        whole["id"] = "whole-conflict/1";
        overlaps["decoders"].push_back(whole);
        auto ro = options;
        ro.decoders = BinaryLayouts::parse(overlaps).decoder_registry();
        auto ambiguous = Package::open(root / "good.uasset", ro);
        check(ambiguous.exports[0].properties["Native"].contains("opaque"),
              "Whole/hybrid overlap never depends on registration order");
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            std::ofstream(out / "layouts.json") << catalog.dump(2);
            auto empty = Json{{"format", "cyuediff.binary-layouts/1"}, {"layouts", Json::object()}};
            std::ofstream(out / "empty-layouts.json") << empty.dump(2);
            for (int version : {0, 1004, 1010, 1011, 1012, 1013}) {
                const auto stem = std::to_string(version);
                save_tagged_hybrid(out, "before-" + stem, tagged_hybrid_fixture(version));
                save_tagged_hybrid(out, "after-" + stem, tagged_hybrid_fixture(version, true));
                save_tagged_hybrid(out, "bad-" + stem, tagged_hybrid_fixture(version, false, 80));
            }
        }
        const auto resolved = fs::weakly_canonical(root);
        if (resolved.parent_path() != fs::weakly_canonical(fs::current_path()) ||
            resolved.filename().u8string().rfind(".test-tmp-tagged-hybrid-", 0) != 0)
            throw std::runtime_error("Cleanup root escaped");
        fs::remove_all(resolved);
        std::cout << checks << " tagged hybrid checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << "\nFixtures: " << root << '\n';
        return 1;
    }
}
