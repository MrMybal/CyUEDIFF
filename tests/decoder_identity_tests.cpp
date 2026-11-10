#include <cyuediff/binary_layout.hpp>
#include "tagged_hybrid_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected rejected identity");
}
Json scalar(const char *type) { return {{"type", type}}; }
Json catalogue(Json program) {
    return {{"format", "cyuediff.binary-layouts/1"},
            {"layouts", {{"Root", std::move(program)}}},
            {"decoders", Json::array({{{"id", "explicit.native/1"},
                                       {"kind", "native-export"},
                                       {"target", "/Script/CoreUObject.DataAsset"},
                                       {"profile", {{"ue4", {522, 522}}, {"ue5", {0, 1018}}}},
                                       {"layout", "Root"}}})}};
}
std::string identity(const Json &doc, const BinaryLayoutReadOptions &o = {}) {
    PackageSummary summary;
    summary.ue4_version = 522;
    summary.ue5_version = 1013;
    auto definitions = BinaryLayouts::parse(doc).decoder_registry(o)->definitions(summary);
    return definitions.at("explicit.native/1");
}
Package open(const fs::path &file, const Json &doc) {
    ReadOptions o;
    o.decoders = BinaryLayouts::parse(doc).decoder_registry();
    return Package::open(file, o);
}
bool changed(const DiffResult &d, const std::string &suffix) {
    for (const auto &row : d.changes) {
        const auto path = row.at("path").get<std::string>();
        if (path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
            return true;
    }
    return false;
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-decoder-identity-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        const auto c = catalogue(scalar("u32"));
        const auto original = identity(c);
        check(original.size() == 64 && original.find_first_not_of("0123456789abcdef") == std::string::npos,
              "Registry supplies canonical per-binding definition hashes");
        check(identity(Json::parse(c.dump(4))) == original, "JSON formatting is not interpretation");
        auto other = c;
        other["layouts"]["Unrelated"] = scalar("f64");
        other["decoders"].push_back({{"id", "unrelated/1"},
                                     {"kind", "struct"},
                                     {"target", "Other"},
                                     {"profile", {{"ue4", {522, 522}}, {"ue5", {0, 1018}}}},
                                     {"layout", "Unrelated"}});
        check(identity(other) == original &&
                  BinaryLayouts::parse(c).sha256() != BinaryLayouts::parse(other).sha256(),
              "Unrelated catalogue entries change provenance without changing this definition");
        other = c;
        other["layouts"]["Renamed"] = other["layouts"]["Root"];
        other["layouts"].erase("Root");
        other["decoders"][0]["layout"] = "Renamed";
        check(identity(other) == original, "Local layout renaming does not change the wire program");
        other = c;
        other["layouts"]["Leaf"] = scalar("u32");
        other["layouts"]["Root"] = {{"type", "ref"}, {"layout", "Leaf"}};
        check(identity(other) == original, "Factoring a shared reference preserves interpretation");
        other["layouts"]["Leaf"] = scalar("i32");
        check(identity(other) != original, "A referenced leaf width/sign definition is tracked");
        auto record = catalogue({{"type", "record"},
                                 {"fields", Json::array({{{"name", "a"}, {"value", scalar("u32")}},
                                                         {{"name", "b"}, {"value", scalar("f32")}}})}});
        other = record;
        other["layouts"]["Root"]["fields"][0]["name"] = "renamed";
        check(identity(record) != identity(other), "Output member names belong to the decoder schema");
        other = record;
        std::swap(other["layouts"]["Root"]["fields"][0], other["layouts"]["Root"]["fields"][1]);
        check(identity(record) != identity(other), "Record wire order is significant");
        for (const auto &key : {"target", "prefix", "kind"}) {
            other = c;
            if (std::string(key) == "target")
                other["decoders"][0][key] = "/Script/Other.Owner";
            else {
                other["decoders"][0]["kind"] = "struct";
                other["decoders"][0]["target"] = "Other";
                if (std::string(key) == "prefix")
                    other["decoders"][0][key] = "tagged-properties";
            }
            check(identity(other) != original, "Binding ownership and prefix mode are certified context");
        }
        other = c;
        other["decoders"][0]["profile"]["ue5"] = {1000, 1018};
        check(identity(other) != original, "Changed certified profiles retain a distinct definition");
        auto strings = catalogue(scalar("string"));
        other = strings;
        other["narrowStringEncoding"] = "ascii";
        check(identity(strings) == identity(other), "Explicit ASCII equals the implicit reader default");
        other["narrowStringEncoding"] = "utf8";
        check(identity(strings) != identity(other), "Effective narrow string interpretation is tracked");
        BinaryLayoutReadOptions opts;
        opts.narrow_string_encoding = NarrowStringEncoding::ascii;
        check(identity(strings) == identity(other, opts),
              "Host encoding override controls the effective identity");
        other = c;
        other["narrowStringEncoding"] = "utf8";
        check(identity(other) == original, "Unused string options do not change integer interpretation");
        opts = {};
        opts.max_values = 123;
        opts.max_read_bytes = 4096;
        check(identity(c, opts) == original, "Availability budgets are not stored/wire changes");
        opts.reference = [](std::int32_t n) -> Json { return n; };
        rejects([&] { identity(c, opts); }, "layout-reference-identity");
        opts.reference_identity = "host.object/1";
        auto objects = catalogue(scalar("object"));
        check(identity(objects, opts) != identity(objects),
              "Host and package object identities differ explicitly");
        const auto host1 = identity(objects, opts);
        opts.reference_identity = "host.object/2";
        check(identity(objects, opts) != host1, "Host reference resolver versions are tracked");
        check(identity(c, opts) == original, "An unused resolver does not change scalar output");
        opts.reference_identity = "bad identity";
        rejects([&] { identity(c, opts); }, "layout-reference-identity");
        opts = {};
        opts.reference_identity = "host.object/1";
        rejects([&] { identity(c, opts); }, "layout-reference-identity");
        auto dag = c;
        std::string previous = "Root";
        for (unsigned i = 0; i < 36; ++i) {
            const auto name = "L" + std::to_string(i);
            dag["layouts"][name] = {
                {"type", "record"},
                {"fields",
                 Json::array({{{"name", "a"}, {"value", {{"type", "ref"}, {"layout", previous}}}},
                              {{"name", "b"}, {"value", {{"type", "ref"}, {"layout", previous}}}}})}};
            previous = name;
        }
        dag["decoders"][0]["layout"] = previous;
        check(identity(dag).size() == 64,
              "Shared-reference identities do not expand exponential output trees");
        opts = {};
        unsigned work = 0;
        opts.control.cancelled = [&] { return ++work > 20; };
        rejects([&] { identity(dag, opts); }, "cancelled");
        auto callback = AssetDecoder{};
        callback.id = "host/1";
        callback.target = "ExampleStruct";
        callback.decode = [](DecoderInput &i, const DecoderContext &) -> Json {
            return i.get<std::int32_t>();
        };
        callback.definition_sha256 = std::string(64, 'a');
        PackageSummary summary;
        check(DecoderRegistry({callback}).definitions(summary).at("host/1") == callback.definition_sha256,
              "Explicit callback definition hashes remain separate from callback output");
        callback.definition_sha256 = std::string(64, 'A');
        rejects([&] { DecoderRegistry({callback}); }, "decoder-registration");
        callback.definition_sha256.clear();
        check(DecoderRegistry({callback}).definitions(summary).empty(),
              "Legacy callbacks retain their stable-ID contract");
        for (int version : {0, 1004, 1010, 1011, 1012, 1013}) {
            const auto file = save(root, "native" + std::to_string(version), fixture(version, 1, false, 9),
                                   version == 1013);
            auto a = open(file, c), b = open(file, c);
            check(a.decoder_definitions.at("explicit.native/1") == original,
                  "Package preserves matched decoder provenance");
            check(compare(a, b).status() == "identical", "Same bytes and definition remain comparable");
            other = c;
            other["layouts"]["Root"] = scalar("i32");
            b = open(file, other);
            auto diff = compare(a, b);
            check(diff.changes.empty() && diff.unavailable_scopes.size() == 1 && !diff.binary_changed &&
                      diff.unavailable_scopes[0]["reason"] == "native-definition-incompatible",
                  "Same-ID changed native definition is unknown even when output numbers coincide");
            check(diff.unavailable_scopes[0].contains("beforeDefinitionSha256") &&
                      diff.unavailable_scopes[0].contains("afterDefinitionSha256"),
                  "Unknown report retains both identities");
            b = a;
            b.decoder_definitions.clear();
            check(compare(a, b).unavailable_scopes.size() == 1,
                  "Missing peer definition cannot establish correspondence");
            b = a;
            b.decoder_definitions["unrelated/1"] = std::string(64, 'b');
            diff = compare(a, b);
            check(diff.changes.empty() && diff.unavailable_scopes.empty() &&
                      diff.source_contexts["changed"] == true,
                  "Unrelated available identity remains context only");
            b = a;
            a.exports[0].native_data["data"] = {{"definitionSha256", 1}};
            b.exports[0].native_data["data"] = {{"definitionSha256", 2}};
            check(changed(compare(a, b), "/native/data/definitionSha256"),
                  "Native data with metadata-like names remains real data");
        }
        const auto map_file = root / "native1013.uasset";
        auto map_before = open(map_file, c);
        auto signed_definition = c;
        signed_definition["layouts"]["Root"] = scalar("i32");
        auto map_after = open(map_file, signed_definition);
        for (auto *package : {&map_before, &map_after}) {
            package->exports[0].native_data = Json::object();
            package->exports[0].native_coverage = "unavailable";
        }
        const Json native_key = {{"decoder", "explicit.native/1"}, {"data", 9}};
        const auto map_value = [&](const char *key) {
            return Json{{"type", {{"name", "MapProperty"}}},
                        {"value",
                         {{"kind", "stored-map-delta"},
                          {"removed", Json::object()},
                          {"entries", {{key, {{"key", native_key}, {"value", 99}}}}}}}};
        };
        map_before.exports[0].properties["Lookup"] = map_value("before-key");
        map_after.exports[0].properties["Lookup"] = map_value("after-key");
        auto map_diff = compare(map_before, map_after);
        check(map_diff.changes.empty() && map_diff.unavailable_scopes.size() == 1 &&
                  map_diff.unavailable_scopes[0]["reason"] == "map-key-definition-incompatible",
              "Native map-key definition changes cannot invent logical key removal/addition");
        for (auto *package : {&map_before, &map_after}) {
            auto &map = package->exports[0].properties["Lookup"]["value"];
            map["removed"] = map["entries"].begin().value()["key"];
            map["removed"] = {{"removed-key", map["removed"]}};
            map["entries"] = Json::object();
        }
        check(compare(map_before, map_after).unavailable_scopes.size() == 1,
              "Native removed-map-key definitions need the same correspondence guard");
        auto h = hybrid_catalogue(), altered = h;
        altered["layouts"]["OuterTail"]["fields"][0]["name"] = "renamedSamples";
        save_tagged_hybrid(root, "hybrid-before", tagged_hybrid_fixture());
        save_tagged_hybrid(root, "hybrid-after", tagged_hybrid_fixture(1013, true));
        auto a = open(root / "hybrid-before.uasset", h), b = open(root / "hybrid-after.uasset", altered);
        auto diff = compare(a, b);
        check(!diff.unavailable_scopes.empty() &&
                  diff.unavailable_scopes[0]["reason"] == "native-struct-definition-incompatible",
              "A changed hybrid tail schema is unknown");
        check(changed(diff, "/data/properties/Mode/value") &&
                  changed(diff, "/data/properties/Nested/value/data/native/stamp"),
              "Independent reflected prefix and unchanged nested native schemas retain known changes");
        check(!changed(diff, "/native/samples") && !changed(diff, "/native/renamedSamples"),
              "No native member removal is invented");
        b = a;
        b.decoder_definitions.begin()->second = "bad";
        rejects([&] { compare(a, b); }, "source-coverage-identity");
        fs::remove_all(root);
        std::cout << checks << " native definition identity checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
