#include "tagged_hybrid_fixture.hpp"
#include "schema_fixture.hpp"
#include <cyuediff/tagged_types.hpp>
#include <cyuediff/schema.hpp>
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool ok, const char *why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Expected rejected source metadata");
}
Json scalar(const char *name) {
    return {{"name", name}};
}
Json structure(const char *name) {
    return {{"name", "StructProperty"},
            {"parameters", Json::array({scalar(name)})},
            {"schema", std::string("/Script/Original.") + name}};
}
Json field(Json t, unsigned dim = 1) {
    return {{"type", std::move(t)}, {"arraySize", dim}};
}
Json context(const std::string &owner) {
    return {
        {"format", "cyuediff.tagged-types/1"},
        {"profile", {{"ue4", {522, 522}}, {"ue5", {0, 1013}}}},
        {"schemas",
         {{owner, {{"properties", {{"Native", field(structure("HybridOuter"))}}}}},
          {"/Script/Original.HybridOuter",
           {{"native", true},
            {"properties",
             {{"Mode", field(scalar("IntProperty"))},
              {"Label", field(scalar("StrProperty"))},
              {"Nested", field(structure("HybridInner"))},
              {"Records",
               field({{"name", "ArrayProperty"}, {"parameters", Json::array({structure("HybridInner")})}})},
              {"Lookup",
               field({{"name", "MapProperty"},
                      {"parameters", Json::array({scalar("NameProperty"), structure("HybridInner")})}})},
              {"Selected",
               field({{"name", "SetProperty"}, {"parameters", Json::array({structure("HybridInner")})}})}}}}},
          {"/Script/Original.HybridInner",
           {{"native", true}, {"properties", {{"Marker", field(scalar("IntProperty"))}}}}}}}};
}
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-tagged-types-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        save_tagged_hybrid(root, "legacy", tagged_hybrid_fixture(1004, false, SIZE_MAX, false, false, true));
        auto plain = Package::open(root / "legacy.uasset");
        const auto owner = plain.exports[0].class_name;
        auto metadata = context(owner);
        auto document = hybrid_catalogue();
        document["taggedTypes"] = metadata;
        auto cat = BinaryLayouts::parse(document);
        ReadOptions options;
        options.decoders = cat.decoder_registry();
        auto p = Package::open(root / "legacy.uasset", options);
        const auto &native = p.exports[0].properties["Native"];
        check(native.contains("value"),
              "Legacy nested map/set struct identities completed from explicit source metadata");
        const auto &props = native["value"]["data"]["properties"];
        check(props["Lookup"]["value"]["entries"]["\"TailName\""]["value"]["data"]["properties"]["Marker"]
                   ["value"] == 31,
              "Legacy map value preserves tagged prefix and native tail");
        check(props["Selected"]["value"]["entries"].size() == 1 &&
                  p.exports[0].properties["AfterNative"]["value"] == 987,
              "Legacy set and following property remain bounded");
        check(native["type"]["parameters"][0]["name"] == "HybridOuter" &&
                  native["resolvedType"]["schema"] == "/Script/Original.HybridOuter",
              "Stored tags remain separate from resolved type metadata");
        check(p.to_json()["sourceTypeContext"]["sha256"] == options.decoders->tagged_types()->sha256() &&
                  !p.to_json()["sourceTypeContext"]["defaultsResolved"].get<bool>(),
              "Explicit source context provenance never claims defaults");
        auto missing = document;
        missing.erase("taggedTypes");
        auto no = options;
        no.decoders = BinaryLayouts::parse(missing).decoder_registry();
        check(Package::open(root / "legacy.uasset", no).exports[0].properties["Native"].contains("opaque"),
              "Absent metadata does not guess legacy struct identity");
        auto bad = document;
        bad["taggedTypes"]["schemas"]["/Script/Original.HybridOuter"]["properties"]["Mode"]["type"]["name"] =
            "FloatProperty";
        auto mismatch = options;
        mismatch.decoders = BinaryLayouts::parse(bad).decoder_registry();
        auto rejected = Package::open(root / "legacy.uasset", mismatch);
        check(rejected.exports[0].properties["Native"].contains("opaque") &&
                  rejected.exports[0].properties["AfterNative"]["value"] == 987,
              "Conflicting scalar metadata discards the whole hybrid without reinterpreting bytes");
        bad = document;
        bad["taggedTypes"]["profile"]["ue5"] = {1013, 1013};
        mismatch.decoders = BinaryLayouts::parse(bad).decoder_registry();
        check(Package::open(root / "legacy.uasset", mismatch)
                  .exports[0]
                  .properties["Native"]
                  .contains("opaque"),
              "Wrong source metadata archive profile stays unavailable");
        bad = document;
        bad["decoders"] = Json::array();
        mismatch.decoders = BinaryLayouts::parse(bad).decoder_registry();
        check(Package::open(root / "legacy.uasset", mismatch)
                      .exports[0]
                      .properties["Native"]["opaque"]["reason"] == "tagged-types-native",
              "Native metadata traits cannot silently select a reflected serializer");
        for (int version : {0, 1004, 1010, 1011, 1012, 1013}) {
            const auto stem = "hinted-" + std::to_string(version);
            save_tagged_hybrid(root, stem,
                               tagged_hybrid_fixture(version, false, SIZE_MAX, false, false, true));
            auto selected = Package::open(root / (stem + ".uasset"), options);
            check(selected.exports[0].properties["Native"].contains("value"),
                  "Type completion agrees with six serialized tag profiles");
        }
        auto scalar_byte = TaggedTypes::complete({"ByteProperty", {}, {}},
                                                 {"EnumProperty", {{"/Script/Original.Mode", {}, {}}}, {}});
        check(scalar_byte.name == "ByteProperty" && scalar_byte.parameters[0].name == "/Script/Original.Mode",
              "Legacy enum metadata retains FName byte-enum serialization");
        rejects([&] {
            TaggedTypes::complete(
                {"StructProperty", {{"Wrong", {}, {}}}, {}},
                {"StructProperty", {{"HybridInner", {}, {}}}, "/Script/Original.HybridInner"});
        });
        auto vector_fixture = fixture(
            1004, 1, false, 0, {"Native", "AfterNative", "Vector"}, [&](Bytes &out, const NameWriter &name) {
                hybrid_tag(out, name, 1004, "Native",
                           hybrid_type("StructProperty", Json::array({hybrid_type("Vector")})), 24);
                out.put<double>(1);
                out.put<double>(2);
                out.put<double>(3);
                hybrid_tag(out, name, 1004, "AfterNative", hybrid_type("IntProperty"), 4);
                out.i(987);
                name(out, "None");
            });
        save_tagged_hybrid(root, "foreign-vector", vector_fixture);
        check(Package::open(root / "foreign-vector.uasset").exports[0].properties["Native"]["value"] ==
                  Json::array({1.0, 2.0, 3.0}),
              "Existing unqualified built-in vectors retain their source contract");
        auto foreign = context(owner);
        foreign["schemas"][owner]["properties"]["Native"] =
            field({{"name", "StructProperty"},
                   {"parameters", Json::array({scalar("Vector")})},
                   {"schema", "/Script/Foreign.Vector"}});
        foreign["schemas"]["/Script/Foreign.Vector"] = {{"properties", Json::object()}};
        auto foreign_catalog = Json{
            {"format", "cyuediff.binary-layouts/1"}, {"layouts", Json::object()}, {"taggedTypes", foreign}};
        auto foreign_options = options;
        foreign_options.decoders = BinaryLayouts::parse(foreign_catalog).decoder_registry();
        auto foreign_package = Package::open(root / "foreign-vector.uasset", foreign_options);
        check(!foreign_package.exports[0].properties["Native"].contains("value") &&
                  foreign_package.exports[0].properties["Native"]["opaque"]["bytes"] == 24 &&
                  foreign_package.exports[0].properties["Native"]["resolvedType"]["schema"] ==
                      "/Script/Foreign.Vector" &&
                  foreign_package.exports[0].properties["AfterNative"]["value"] == 987,
              "A foreign qualified basename never silently selects an Engine built-in serializer");
        auto qualified = document;
        for (auto &binding : qualified["decoders"])
            binding["target"] = "/Script/Original." + binding["target"].get<std::string>();
        auto qualified_options = options;
        qualified_options.decoders = BinaryLayouts::parse(qualified).decoder_registry();
        check(Package::open(root / "legacy.uasset", qualified_options)
                  .exports[0]
                  .properties["Native"]
                  .contains("value"),
              "Qualified native bindings work with explicitly completed source identities");
        auto overlapping = document;
        auto binding = qualified["decoders"][1];
        binding["id"] = "original.qualifiedOuter/1";
        overlapping["decoders"].push_back(binding);
        qualified_options.decoders = BinaryLayouts::parse(overlapping).decoder_registry();
        check(Package::open(root / "legacy.uasset", qualified_options)
                      .exports[0]
                      .properties["Native"]["opaque"]["reason"] == "decoder-ambiguous",
              "Overlapping short and qualified bindings never depend on registration order");
        auto types = TaggedTypes::parse(metadata);
        check(types.property(owner, "missing", 0, p.summary) == nullptr,
              "Partial contexts do not fabricate missing fields");
        rejects([&] { types.property("HybridInner", "Marker", 1, p.summary); });
        bad = metadata;
        bad["schemas"]["/Script/Other.HybridInner"] = {
            {"properties", {{"Marker", field(scalar("FloatProperty"))}}}};
        auto ambiguous = TaggedTypes::parse(bad);
        rejects([&] { ambiguous.property("HybridInner", "Marker", 0, p.summary); });
        check(ambiguous.property("/Script/Original.HybridInner", "Marker", 0, p.summary)->name ==
                  "IntProperty",
              "Qualified source type identity remains deterministic");
        bad = metadata;
        bad["schemas"]["/Script/Original.HybridOuter"]["super"] = "/missing";
        rejects([&] { TaggedTypes::parse(bad); });
        bad["schemas"]["/Script/Original.HybridOuter"]["super"] = "/Script/Original.HybridOuter";
        rejects([&] { TaggedTypes::parse(bad); });
        bad = metadata;
        bad["schemas"]["/Script/Original.HybridOuter"]["super"] = "/Script/Original.HybridInner";
        bad["schemas"]["/Script/Original.HybridOuter"]["properties"]["Marker"] = field(scalar("IntProperty"));
        rejects([&] { TaggedTypes::parse(bad); });
        bad = metadata;
        bad["schemas"]["/Script/Original.HybridInner"]["properties"]["Marker"]["arraySize"] = 0;
        rejects([&] { TaggedTypes::parse(bad); });
        bad = metadata;
        bad["schemas"]["/Script/Original.HybridInner"]["native"] = "yes";
        rejects([&] { TaggedTypes::parse(bad); });
        auto wire = TaggedType{"StructProperty", {{"HybridInner", {{"/Script/Other", {}, {}}}, {}}}, {}};
        auto declared =
            TaggedType{"StructProperty", {{"HybridInner", {}, {}}}, "/Script/Original.HybridInner"};
        rejects([&] { TaggedTypes::complete(wire, declared); });
        wire.parameters[0].parameters[0].name = "/Script/Original";
        check(TaggedTypes::complete(wire, declared).schema == declared.schema,
              "Full-type stored package qualifiers agree with explicit metadata");
        const auto db = UnversionedSchemas::parse(schema_fixture(4, false, true).data);
        SerializationProfile profile;
        profile.ue4 = {522, 522};
        profile.ue5 = {1004, 1004};
        auto mapped = TaggedTypes::from_schemas(db, {"DemoAsset"}, profile);
        PackageSummary summary;
        summary.ue4_version = 522;
        summary.ue5_version = 1004;
        check(mapped.to_json()["usmapSha256"] == db.sha256() &&
                  mapped.property("DemoAsset", "Enabled", 0, summary)->name == "BoolProperty",
              "USMAP conversion retains mapping provenance and inherited fields");
        check(mapped.property("DemoAsset", "Samples", 1, summary)->name == "FloatProperty",
              "USMAP static array dimensions preserved");
        rejects([&] { mapped.property("DemoAsset", "Samples", 2, summary); });
        TaggedTypesOptions small;
        small.max_nodes = 5;
        rejects([&] { TaggedTypes::parse(metadata, small); });
        small = {};
        small.max_text_bytes = 8;
        rejects([&] { TaggedTypes::parse(metadata, small); });
        small = {};
        small.control.cancelled = [] { return true; };
        rejects([&] { TaggedTypes::from_schemas(db, {"DemoAsset"}, profile, small); });
        small = {};
        small.max_depth = 129;
        rejects([&] { TaggedTypes::from_schemas(db, {"DemoAsset"}, profile, small); });
        small = {};
        small.max_nodes = 4'000'001;
        rejects([&] { TaggedTypes::from_schemas(db, {"DemoAsset"}, profile, small); });
        auto plain_report = plain.to_json();
        check(plain_report.find("sourceTypeContext") == plain_report.end(),
              "Unselected contexts keep the original report shape");
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures") {
            const auto dest = fs::u8path(argv[2]);
            fs::create_directories(dest);
            save_tagged_hybrid(dest, "before",
                               tagged_hybrid_fixture(1004, false, SIZE_MAX, false, false, true));
            save_tagged_hybrid(dest, "after",
                               tagged_hybrid_fixture(1004, true, SIZE_MAX, false, false, true));
            std::ofstream(dest / "types.json") << document.dump(2);
            std::ofstream(dest / "base.json") << hybrid_catalogue().dump(2);
            const auto mapping = schema_fixture();
            std::ofstream out(dest / "types.usmap", std::ios::binary);
            out.write(reinterpret_cast<const char *>(mapping.data.data()), mapping.data.size());
        }
        auto final = fs::weakly_canonical(root);
        if (final.parent_path() != fs::weakly_canonical(fs::current_path()) ||
            final.filename().u8string().rfind(".test-tmp-tagged-types-", 0) != 0)
            throw std::runtime_error("Cleanup escaped test root");
        fs::remove_all(final);
        std::cout << checks << " tagged type metadata checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << "\nFixtures: " << root << '\n';
        return 1;
    }
}
