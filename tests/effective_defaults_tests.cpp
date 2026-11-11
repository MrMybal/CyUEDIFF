#include "default_snapshot_fixture.hpp"
#include "package_fixture.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <chrono>
using namespace cyuediff;
namespace fixture = cyuediff::test::default_snapshot;
namespace {
unsigned checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code = nullptr) {
    try {
        f();
    } catch (const Error &e) {
        check(!code || e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected rejected effective default operation");
}
void write(const fs::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    out.close();
    if (!out)
        throw std::runtime_error("Cannot write test input");
}
std::string hash(const Json &j) {
    auto s = j.dump();
    return sha256_bytes({s.begin(), s.end()});
}
constexpr const char *root = "/Script/Test.Child";
constexpr const char *record = "/Script/Test.Record";
constexpr const char *field = "/Script/Test.Child.Count";
struct Prepared {
    fs::path dir;
    Json capture, context;
    Prepared(fs::path d) : dir(std::move(d)), capture(fixture::capture()) {
        capture["engineVersion"] = "5.3.2-29314046+++UE5+Release-5.3";
        capture["declarations"][root]["properties"].erase(3);
        capture["fields"].erase(std::string(root) + ".Point");
        auto name = fixture::property("Selected", 3, {{"name", "NameProperty"}});
        capture["declarations"][root]["properties"].push_back(name);
        capture["fields"][std::string(root) + ".Selected"] = fixture::node(root, name, "0000000000000000");
        capture["fields"][std::string(root) + ".Selected"]["wire"]["names"] = Json::array({"Item_1"});
        capture["structures"][record]["postSerialize"] = false;
        write(dir / "input.dat", "Original recorded input");
        Json files = Json::object(), deps = Json::object();
        for (const auto *id : {"module:Test", "project", "executable", "engine-build"}) {
            const std::string role = std::string(id) == "module:Test" ? "module" : id;
            files[id] = {{"role", role},
                         {"file", (dir / "input.dat").u8string()},
                         {"present", true},
                         {"bytes", std::to_string(fs::file_size(dir / "input.dat"))},
                         {"sha256", sha256_file(dir / "input.dat")}};
            deps[id] = {{"file", (dir / "input.dat").u8string()},
                        {"bytes", fs::file_size(dir / "input.dat")},
                        {"sha256", sha256_file(dir / "input.dat")}};
        }
        auto missing = (dir / "absent" / "User.ini").u8string();
        files["config:missing"] = {{"role", "config"}, {"file", missing}, {"present", false}};
        deps["config:missing"] = {{"file", missing}, {"absent", true}};
        capture["environment"] = {{"format", "cyuediff.engine-default-environment/1"},
                                  {"platform", "Windows"},
                                  {"byteOrder", "little"},
                                  {"configurationFileIOFrozen", true},
                                  {"files", files},
                                  {"mergedConfigBlake3", Json::object()}};
        context = {{"format", "cyuediff.default-context/1"},
                   {"snapshot",
                    {{"file", (dir / "capture.json").u8string()},
                     {"sha256", hash(capture)},
                     {"engineVersion", capture["engineVersion"]},
                     {"filterEditorOnly", false}}},
                   {"sourceProfile",
                    {{"profile", "ue5.3.2-win64-tagged-ascii/1"},
                     {"savedEngine", "5.3.2-selected-source"},
                     {"ue4", 522},
                     {"ue5", 1009},
                     {"licensee", 0},
                     {"intraPropertyDelta", true},
                     {"customVersions", Json::object()}}},
                   {"dependencies", deps}};
        save();
    }
    void save() {
        context["snapshot"]["sha256"] = hash(capture);
        write(dir / "capture.json", capture.dump());
        write(dir / "context.json", context.dump());
    }
    DefaultContext read(const DefaultContextReadOptions &o = {}) {
        save();
        return DefaultContext::read(dir / "context.json", o);
    }
    Package package() const {
        Package p;
        p.summary.ue4_version = 522;
        p.summary.ue5_version = 1009;
        p.summary.saved_engine = "5.3.2-selected-source";
        ObjectExport e;
        e.path = "Asset";
        e.name = "Asset";
        e.class_name = root;
        e.coverage = "properties";
        e.flags = 3;
        p.exports.push_back(e);
        return p;
    }
};
Json tag(const char *type, Json v) { return {{"type", {{"name", type}}}, {"value", std::move(v)}}; }
void blueprint_checks(const fs::path &dir) {
    fs::create_directories(dir);
    Prepared f(dir);
    const std::string cls = "/Game/Test/BP.BP_C", pkg = "/Game/Test/BP",
                      guid = "0123456789abcdef0123456789abcdef";
    auto encoded = f.capture.dump();
    for (std::size_t at = 0; (at = encoded.find(root, at)) != std::string::npos; at += cls.size())
        encoded.replace(at, std::string(root).size(), cls);
    f.capture = Json::parse(encoded);
    Json target = {{"path", pkg + ".Default__BP_C"}, {"class", cls}, {"flags", "0000000000000010"}};
    Json parent = {{"path", "/Script/Test.Default__Base"},
                   {"class", "/Script/Test.Base"},
                   {"flags", "0000000000000010"}};
    Json generated = {
        {"path", pkg + ".BP"}, {"class", "/Script/Engine.Blueprint"}, {"flags", "0000000000000003"}};
    f.capture["object"] = target;
    f.capture["blueprintClass"] = true;
    f.capture["classGeneratedBy"] = generated;
    f.capture["archetype"] = parent;
    for (auto &d : f.capture["declarations"][cls]["properties"])
        d["propertyGuid"] = guid;
    for (auto &v : f.capture["fields"])
        if (v["owner"] == cls)
            v["propertyGuid"] = guid;
    const auto id = "package:" + pkg;
    auto &files = f.capture["environment"]["files"];
    files[id] = files["module:Test"];
    files[id]["role"] = "asset-package";
    f.context["dependencies"][id] = f.context["dependencies"]["module:Test"];
    Json companions = Json::array();
    for (const char *ext : {"uexp", "ubulk", "uptnl", "m.ubulk", "o.ubulk", "upayload"}) {
        const auto key = id + ":" + ext;
        const auto path = (dir / (std::string("absent.") + ext)).u8string();
        files[key] = {{"role", "asset-companion"}, {"file", path}, {"present", false}};
        f.context["dependencies"][key] = {{"file", path}, {"absent", true}};
        companions.push_back(key);
    }
    f.capture["environment"]["blueprintDependencies"] = {
        {"format", "cyuediff.blueprint-default-dependencies/1"},
        {"complete", true},
        {"classes", Json::array({Json{{"class", cls},
                                      {"super", "/Script/Test.Base"},
                                      {"defaultObject", target},
                                      {"generatedBy", generated},
                                      {"blueprint", true}},
                                 Json{{"class", "/Script/Test.Base"},
                                      {"super", ""},
                                      {"defaultObject", parent},
                                      {"generatedBy", nullptr},
                                      {"blueprint", false}}})},
        {"packages",
         {{pkg,
           {{"fileId", id}, {"companionIds", companions}, {"imports", Json::array({"/Script/Test"})}}}}}};
    f.context["format"] = "cyuediff.default-context/2";
    f.context["sourceProfile"]["profile"] = "ue5.3.2-win64-blueprint-tagged-ascii/1";
    f.context["sourceProfile"]["objectKind"] = "instance";
    f.context["sourceProfile"]["blueprintPackageSha256"] = files[id]["sha256"];
    auto context = f.read();
    auto p = f.package();
    p.exports[0].class_name = cls;
    check(default_root_exports(p, context) == std::vector<std::string>{"Asset"},
          "Blueprint instance root is selected through its bound generated class");
    check(resolve_source_defaults(p, "Asset", context).complete,
          "Omitted Blueprint instance fields use the actual selected CDO");
    p.exports[0].properties["Count"] = tag("IntProperty", 99);
    p.exports[0].properties["Count"]["guid"] = guid;
    check(resolve_source_defaults(p, "Asset", context).fields.at(cls + ".Count").at("value") == 99,
          "Stored Blueprint GUID and qualified owner retain override values");
    p.exports[0].properties["Count"]["guid"] = std::string(32, 'f');
    check(!resolve_source_defaults(p, "Asset", context).complete,
          "A renamed/mismatched Blueprint GUID never becomes a guessed field value");
    p.exports[0].properties["Count"].erase("guid");
    check(!resolve_source_defaults(p, "Asset", context).complete,
          "Missing Blueprint field identity is unavailable");
    const auto saved_capture = f.capture, saved_context = f.context;
    f.capture["environment"]["blueprintDependencies"]["complete"] = false;
    rejects([&] { f.read(); });
    f.capture = saved_capture;
    f.capture["environment"]["blueprintDependencies"]["packages"][pkg]["imports"].push_back("/Game/Missing");
    rejects([&] { f.read(); });
    f.capture = saved_capture;
    f.capture["environment"]["blueprintDependencies"]["classes"][0]["super"] = "/Script/Test.Wrong";
    rejects([&] { f.read(); });
    f.capture = saved_capture;
    f.context["sourceProfile"]["blueprintPackageSha256"] = std::string(64, '0');
    rejects([&] { f.read(); });
    f.context = saved_context;
    f.context["format"] = "cyuediff.default-context/1";
    rejects([&] { f.read(); });
    f.context = saved_context;
    f.capture["fields"][cls + ".Count"]["propertyGuid"] = std::string(32, '0');
    rejects([&] { f.read(); });
    f.capture = saved_capture;
    write(dir / "absent.uexp", "unexpected companion");
    rejects([&] { f.read(); });
    fs::remove(dir / "absent.uexp");
    f.context["sourceProfile"]["objectKind"] = "class-default";
    f.capture["factory"] = {
        {"format", "cyuediff.blueprint-cdo-factory/1"},
        {"operation", "construct-cdo-from-explicit-parent-template"},
        {"class",
         {{"path", cls}, {"class", "/Script/Engine.BlueprintGeneratedClass"}, {"flags", "0000000000000001"}}},
        {"targetDefault", target},
        {"parentDefault", parent}};
    f.capture["object"]["path"] = "/Temp/CyUEDiffFactory_Test.Default__BP_C";
    context = f.read();
    p = f.package();
    p.exports[0].path = p.exports[0].name = "Default__BP_C";
    p.exports[0].flags = 0x10;
    p.exports[0].class_name = "BP_C";
    p.exports[0].class_index = 2;
    ObjectExport generated_class;
    generated_class.path = generated_class.name = "BP_C";
    generated_class.class_name = "/Script/Engine.BlueprintGeneratedClass";
    p.exports.push_back(generated_class);
    p.fingerprints["package"] = {{"bytes", fs::file_size(dir / "input.dat")},
                                 {"sha256", sha256_file(dir / "input.dat")}};
    check(default_root_exports(p, context) == std::vector<std::string>{"Default__BP_C"},
          "Blueprint file root uses its local generated-class export, not the editor Blueprint object");
    check(resolve_source_defaults(p, "Default__BP_C", context).complete,
          "Blueprint CDO applies stored fields to an explicit independent factory seed");
    p.fingerprints["package"]["sha256"] = std::string(64, 'f');
    rejects([&] { resolve_source_defaults(p, "Default__BP_C", context); });
    p.fingerprints["package"]["sha256"] = sha256_file(dir / "input.dat");
    p.fingerprints[".uexp"] = {{"bytes", 1}, {"sha256", std::string(64, 'f')}};
    rejects([&] { resolve_source_defaults(p, "Default__BP_C", context); });
    p.fingerprints.erase(".uexp");
    p.exports[0].class_index = 1;
    check(default_root_exports(p, context).empty(),
          "A local class-name lookalike without its actual generated-class export is rejected");
    f.capture["factory"]["parentDefault"]["path"] = "/Script/Test.WrongDefault";
    rejects([&] { f.read(); });
}
} // namespace
int main(int argc, char **argv) {
    const auto dir =
        fs::current_path() / (".test-tmp-effective-defaults-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    try {
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures") {
            const auto out = fs::absolute(fs::u8path(argv[2]));
            fs::create_directories(out);
            Prepared generated(out);
            auto asset = [&](const char *file, int mode) {
                const std::vector<std::string> names = {"Count", "Selected", "NameProperty", "Item",
                                                        "Item_1"};
                auto data = cyuediff::test::fixture(
                    1009, 1, false, 0, names,
                    [&](cyuediff::test::Bytes &script, const cyuediff::test::NameWriter &name) {
                        script.data.clear();
                        if (mode) {
                            name(script, mode == 1 ? "Count" : "Selected");
                            name(script, mode == 1 ? "IntProperty" : "NameProperty");
                            script.i(mode == 1 ? 4 : 8);
                            script.i(0);
                            script.put<std::uint8_t>(0);
                            if (mode == 1)
                                script.i(99);
                            else {
                                name(script, "Item");
                                script.patch(script.data.size() - 4, 2);
                            }
                        }
                    },
                    -1, -1, -1, -1, -1, -1, -1, "Child", "/Script/Test");
                return cyuediff::test::save(out, file, std::move(data));
            };
            auto source = Package::open(asset("before", 0));
            asset("after", 1);
            asset("named", 2);
            generated.context["sourceProfile"]["savedEngine"] = source.summary.saved_engine;
            generated.context["sourceProfile"]["customVersions"] = source.summary.custom_versions;
            generated.save();
            fs::remove_all(dir);
            return 0;
        }
        Prepared f(dir);
        auto context = f.read();
        check(context.input_files().size() == 7 &&
                  context.input_files().front() == fs::absolute(dir / "context.json").lexically_normal(),
              "Verified manifest, capture and all recorded inputs are exposed to host guards");
        auto p = f.package();
        const auto original = p.semantic_snapshot();
        auto result = resolve_source_defaults(p, "Asset", context);
        check(result.complete && result.fields.size() == 5,
              "Prepared CDO reconstructs all selected omitted roots");
        check(result.fields.at(field).at("value") == 41, "Omitted root uses captured class default");
        check(result.fields.at(std::string(root) + ".Large").at("value") == 9007199254740993ll,
              "Captured integer above 2^53 stays exact");
        check(result.fields.at("/Script/Test.Base.Inherited").at("value") == 27,
              "Inherited value comes from derived CDO");
        check(result.fields.at(std::string(root) + ".Record")
                      .at("value")
                      .at(std::string(record) + ".Count")
                      .at("value") == 19,
              "Member defaults differ from struct initializer");
        check(p.semantic_snapshot() == original, "Effective resolution never mutates stored package data");
        check(result.to_json().at("scope") == "effective-reflected-properties",
              "Result explicitly scopes its effective values");
        p.exports[0].properties["Count"] = tag("IntProperty", 99);
        result = resolve_source_defaults(p, "Asset", context);
        check(result.complete && result.fields.at(field).at("value") == 99,
              "Stored scalar override replaces class default");
        p.exports[0].properties["Record"] = {{"type",
                                              {{"name", "StructProperty"},
                                               {"parameters", Json::array({{{"name", "Record"}}})},
                                               {"structGuid", std::string(32, '0')}}},
                                             {"value", {{"Count", tag("IntProperty", 29)}}}};
        result = resolve_source_defaults(p, "Asset", context);
        check(result.complete && result.fields.at(std::string(root) + ".Record")
                                         .at("value")
                                         .at(std::string(record) + ".Count")
                                         .at("value") == 29,
              "Partial reflected member overrides use qualified type metadata");
        p.exports[0].properties["Count"] = {{"type", {{"name", "IntProperty"}}},
                                            {"opaque", {{"reason", "unread"}}}};
        result = resolve_source_defaults(p, "Asset", context);
        check(!result.complete && !result.fields.at(field).at("available").get<bool>() &&
                  result.fields.at("/Script/Test.Base.Inherited").at("available") == true,
              "Unread stored overrides never become fabricated defaults");
        p.exports[0].properties["Count"] = tag("FloatProperty", 1.0);
        check(!resolve_source_defaults(p, "Asset", context).fields.at(field).at("available").get<bool>(),
              "Incompatible property types stay unavailable");
        p.exports[0].properties["Count"] = tag("IntProperty", "wrong");
        check(!resolve_source_defaults(p, "Asset", context).fields.at(field).at("available").get<bool>(),
              "Malformed numeric override stays unavailable");
        p.exports[0].properties["Count"] = tag("IntProperty", 2147483648ll);
        check(!resolve_source_defaults(p, "Asset", context).fields.at(field).at("available").get<bool>(),
              "Overflowing typed integer does not acquire a default");
        p.exports[0].properties["Count"] = tag("IntProperty", nullptr);
        p.exports[0].properties["Count"]["serializationFlags"] = 32;
        check(!resolve_source_defaults(p, "Asset", context).fields.at(field).at("available").get<bool>(),
              "Skipped stored marker remains explicitly unavailable");
        p = f.package();
        p.exports[0].properties["Selected"] = tag("NameProperty", "Item_1");
        check(!resolve_source_defaults(p, "Asset", context).complete,
              "Display text alone cannot resolve typed name components");
        p.value_identities = {
            {"names", {{"/objects/Asset/properties/Selected/value", {{"base", "Item"}, {"number", 2}}}}}};
        result = resolve_source_defaults(p, "Asset", context);
        check(result.complete && result.fields.at(std::string(root) + ".Selected").at("value") ==
                                     Json({{"base", "Item"}, {"number", 2}}),
              "Effective names preserve hidden actual base/number components");
        p.value_identities["names"]["/objects/Asset/properties/Selected/value"]["number"] = 3;
        check(!resolve_source_defaults(p, "Asset", context).complete,
              "Name component observation must dereference its actual display");
        p = f.package();
        p.exports[0].properties["Foreign"] = tag("IntProperty", 5);
        check(!resolve_source_defaults(p, "Asset", context).complete,
              "Foreign stored fields are not silently discarded");
        p = f.package();
        p.exports[0].coverage = "opaque";
        rejects([&] { resolve_source_defaults(p, "Asset", context); }, "default-context");
        p = f.package();
        p.summary.ue5_version = 1013;
        rejects([&] { resolve_source_defaults(p, "Asset", context); }, "default-context");
        p = f.package();
        p.summary.custom_versions[std::string(32, 'b')] = 1;
        rejects([&] { resolve_source_defaults(p, "Asset", context); }, "default-context");
        p = f.package();
        p.exports[0].class_name = "/Script/Test.Other";
        rejects([&] { resolve_source_defaults(p, "Asset", context); }, "default-context");
        p = f.package();
        p.exports[0].outer_index = 1;
        rejects([&] { resolve_source_defaults(p, "Asset", context); }, "default-context");
        p = f.package();
        p.exports[0].flags |= 0x10;
        rejects([&] { resolve_source_defaults(p, "Asset", context); }, "default-context");
        p = f.package();
        EffectiveDefaultsOptions limits;
        limits.max_nodes = 1;
        rejects([&] { resolve_source_defaults(p, "Asset", context, limits); }, "default-limit");
        limits = {};
        limits.max_text_bytes = 0;
        rejects([&] { resolve_source_defaults(p, "Asset", context, limits); }, "default-limit");
        limits = {};
        limits.max_depth = 1;
        rejects([&] { resolve_source_defaults(p, "Asset", context, limits); }, "default-limit");
        limits = {};
        limits.control.cancelled = [] { return true; };
        rejects([&] { resolve_source_defaults(p, "Asset", context, limits); }, "cancelled");
        limits = {};
        unsigned work = 0;
        std::uint64_t text = 0;
        limits.charge = [&](auto n, auto b) {
            work += n;
            text += b;
        };
        result = resolve_source_defaults(p, "Asset", context, limits);
        check(result.provenance.at("workNodes") == work && result.provenance.at("textBytes") == text,
              "Host charges cover effective preparation and output work");
        DefaultContextReadOptions o;
        o.expected_sha256 = std::string(64, 'a');
        rejects([&] { f.read(o); }, "default-context");
        o = {};
        o.max_dependencies = 1;
        rejects([&] { f.read(o); }, "default-context");
        o = {};
        o.max_dependency_bytes = 1;
        rejects([&] { f.read(o); });
        o = {};
        o.document.max_bytes = 1;
        rejects([&] { f.read(o); }, "default-limit");
        o = {};
        o.snapshot.expected_ue5 = 1013;
        rejects([&] { f.read(o); }, "default-context");
        o = {};
        o.snapshot.expected_sha256 = std::string(64, 'a');
        rejects([&] { f.read(o); }, "default-context");
        o = {};
        o.document.control.cancelled = [] { return true; };
        rejects([&] { f.read(o); }, "cancelled");
        write(dir / "input.dat", "Changed recorded input!");
        rejects([&] { f.read(); }, "default-context");
        write(dir / "input.dat", "Original recorded input");
        fs::create_directories(dir / "absent");
        write(dir / "absent" / "User.ini", "New configuration");
        rejects([&] { f.read(); }, "default-context");
        fs::remove(dir / "absent" / "User.ini");
        context = f.read();
        p = f.package();
        const auto baseline = resolve_source_defaults(p, "Asset", context);
        auto peer = baseline;
        check(compare_effective_defaults(baseline, peer).status() == "identical",
              "Equal effective states have no semantic changes");
        peer.fields[field]["origin"] = "stored-override";
        peer.provenance["contextSha256"] = std::string(64, 'f');
        check(compare_effective_defaults(baseline, peer).status() == "identical",
              "Origins and independent context fingerprints stay provenance");
        peer = baseline;
        peer.fields[field]["value"] = 99;
        auto comparison = compare_effective_defaults(baseline, peer);
        check(comparison.status() == "changed" && comparison.changes.size() == 1 &&
                  comparison.changes[0]["before"] == 41 && comparison.changes[0]["after"] == 99,
              "Effective scalar diff reports actual resolved values");
        p.exports[0].properties["Selected"] = tag("NameProperty", "Item_1");
        p.value_identities = {
            {"names", {{"/objects/Asset/properties/Selected/value", {{"base", "Item"}, {"number", 2}}}}}};
        peer = resolve_source_defaults(p, "Asset", context);
        comparison = compare_effective_defaults(baseline, peer);
        check(comparison.changes.size() == 1 && comparison.changes[0]["after"]["number"] == 2,
              "Effective comparison detects distinct equal-display FNames");
        auto left = baseline;
        peer = baseline;
        left.fields[std::string(root) + ".Selected"]["value"] = {{"base", "Base"}, {"number", 0}};
        peer.fields[std::string(root) + ".Selected"]["value"] = {{"base", "base"}, {"number", 0}};
        check(compare_effective_defaults(left, peer).status() == "identical",
              "Selected ASCII FName equality is case-insensitive");
        left.fields[std::string(root) + ".Selected"]["value"]["base"] = "É";
        peer.fields[std::string(root) + ".Selected"]["value"]["base"] = "é";
        comparison = compare_effective_defaults(left, peer);
        check(comparison.status() == "unverified" && comparison.changes.empty(),
              "Unproved Unicode case equality never invents a name change");
        peer.fields[std::string(root) + ".Selected"]["value"]["number"] = 1;
        check(compare_effective_defaults(left, peer).status() == "changed",
              "Different internal numbers prove distinct names independently of Unicode case");
        {
            // "X-y" sorts between "X" and "X/..."; it must not hide the unknown descendant "X/z".
            EffectiveDefaults a, b;
            a.complete = b.complete = true;
            const Json type = {{"name", "OpaqueProperty"}};
            a.fields["F"] = {{"available", true}, {"type", type}, {"value", {{"X", 1}}}};
            b.fields["F"] = {{"available", true}, {"type", type}, {"value", Json::object()}};
            a.unavailable = Json::array({{{"path", "/fields/F/value/X-y"}}, {{"path", "/fields/F/value/X/z"}}});
            const auto sibling = compare_effective_defaults(a, b);
            check(sibling.changes.empty() &&
                      std::any_of(sibling.unavailable.begin(), sibling.unavailable.end(),
                                  [](const Json &row) { return row.at("path") == "/fields/F/value/X"; }),
                  "Unknown descendant scopes keep added/removed values uncertain despite sorted siblings");
        }
        p = f.package();
        p.exports[0].properties["Count"] = {{"type", {{"name", "IntProperty"}}},
                                            {"opaque", {{"reason", "unread"}}}};
        peer = resolve_source_defaults(p, "Asset", context);
        comparison = compare_effective_defaults(baseline, peer);
        check(comparison.status() == "unverified" && comparison.changes.empty() &&
                  comparison.unavailable.size() == 1,
              "Unavailable resolved values never appear as removals");
        peer = baseline;
        peer.fields[field]["type"]["name"] = "FloatProperty";
        comparison = compare_effective_defaults(baseline, peer);
        check(comparison.changes.empty() && comparison.unavailable.size() == 1,
              "Incompatible effective field declarations stay scoped and unavailable");
        peer = baseline;
        peer.fields["/Script/Test.Child.Added"] = peer.fields[field];
        comparison = compare_effective_defaults(baseline, peer);
        check(comparison.changes.empty() && !comparison.unavailable.empty(),
              "A changed context field inventory is not fabricated asset membership");
        left = baseline;
        peer = baseline;
        Json map_type = {{"name", "MapProperty"},
                         {"arguments", Json::array({{{"name", "NameProperty"}}, {{"name", "IntProperty"}}})}};
        left.fields["/Script/Test.Child.Lookup"] = {
            {"type", map_type},
            {"available", true},
            {"value",
             {{"kind", "effective-map"},
              {"entries", {{"[\"base\",0]", {{"key", {{"base", "Base"}, {"number", 0}}}, {"value", 11}}}}}}}};
        peer.fields["/Script/Test.Child.Lookup"] = left.fields["/Script/Test.Child.Lookup"];
        peer.fields["/Script/Test.Child.Lookup"]["value"]["entries"]["[\"base\",0]"]["key"]["base"] = "base";
        peer.fields["/Script/Test.Child.Lookup"]["value"]["entries"]["[\"base\",0]"]["value"] = 12;
        comparison = compare_effective_defaults(left, peer);
        check(comparison.changes.size() == 1 && comparison.changes[0]["before"] == 11 &&
                  comparison.changes[0]["after"] == 12,
              "Matched runtime map keys retain only their actual value diff");
        peer.provenance["keyEquality"] = "unknown/2";
        comparison = compare_effective_defaults(left, peer);
        check(comparison.changes.empty() && !comparison.unavailable.empty(),
              "Changed key equality contracts cannot invent map entry changes");
        left = baseline;
        peer = baseline;
        Json element_type = {{"name", "StructProperty"},
                             {"schema", record},
                             {"structGuid", std::string(32, '0')},
                             {"nativeSerialization", false}};
        Json array_type = {{"name", "ArrayProperty"}, {"arguments", Json::array({element_type})}};
        left.fields["/Script/Test.Child.Items"] = {
            {"type", array_type}, {"available", true}, {"value", Json::array()}};
        peer.fields["/Script/Test.Child.Items"] = left.fields["/Script/Test.Child.Items"];
        peer.fields["/Script/Test.Child.Items"]["value"].push_back(
            peer.fields[std::string(root) + ".Record"]["value"]);
        peer.fields["/Script/Test.Child.Items"]["value"][0][std::string(record) + ".Count"]["available"] =
            false;
        peer.complete = false;
        peer.unavailable.push_back(
            {{"path", "/fields/~1Script~1Test.Child.Items/value/0/~1Script~1Test.Record.Count"},
             {"reason", "unread"}});
        comparison = compare_effective_defaults(left, peer);
        check(comparison.changes.empty() && !comparison.unavailable.empty(),
              "A partly unread added array element stays unavailable instead of a fabricated complete "
              "addition");
        peer = baseline;
        peer.complete = false;
        peer.unavailable = Json::array();
        peer.fields[field]["value"] = 99;
        comparison = compare_effective_defaults(baseline, peer);
        check(comparison.changes.empty() && comparison.status() == "unverified",
              "An incomplete model without local coverage cannot certify value changes");
        EffectiveDefaultsOptions diff_limits;
        diff_limits.max_nodes = 1;
        rejects([&] { compare_effective_defaults(baseline, baseline, diff_limits); }, "default-limit");
        diff_limits = {};
        diff_limits.max_text_bytes = 0;
        rejects([&] { compare_effective_defaults(baseline, baseline, diff_limits); }, "default-limit");
        diff_limits = {};
        diff_limits.control.cancelled = [] { return true; };
        rejects([&] { compare_effective_defaults(baseline, baseline, diff_limits); }, "cancelled");
        auto local_document = f.context;
        const auto pinned_file = dir / "pinned.dat";
        write(pinned_file, "Original recorded input");
        f.context["inputMode"] = "pinned-inputs";
        for (auto &dependency : f.context["dependencies"])
            if (!dependency.value("absent", false))
                dependency["file"] = pinned_file.u8string();
        write(dir / "input.dat", "A different current module");
        auto pinned_context = f.read();
        check(resolve_source_defaults(f.package(), "Asset", pinned_context).complete &&
                  pinned_context.provenance().at("binding") == "verified-pinned-inputs",
              "Pinned captures replay verified original input copies after the current module changes");
        write(pinned_file, "Corrupted pinned input");
        rejects([&] { f.read(); }, "default-context");
        write(dir / "input.dat", "Original recorded input");
        f.context = local_document;
        auto retained_capture = f.capture;
        auto map_property = fixture::property(
            "Values", 4,
            {{"name", "MapProperty"},
             {"arguments", Json::array({{{"name", "NameProperty"}}, {{"name", "IntProperty"}}})}});
        f.capture["declarations"][root]["properties"].push_back(map_property);
        f.capture["declarations"][root]["slots"] = 5;
        f.capture["fields"][std::string(root) + ".Values"] = fixture::node(root, map_property, "");
        f.capture["fields"][std::string(root) + ".Values"]["wire"] = {{"available", false},
                                                                      {"reason", "simulated-capture-limit"}};
        context = f.read();
        p = f.package();
        p.exports[0].properties["Values"] = {
            {"type",
             {{"name", "MapProperty"},
              {"parameters", Json::array({{{"name", "NameProperty"}}, {{"name", "IntProperty"}}})}}},
            {"value",
             {{"kind", "stored-map-delta"}, {"removed", Json::object()}, {"entries", Json::object()}}}};
        auto missing_map = resolve_source_defaults(p, "Asset", context);
        check(!missing_map.fields.at(std::string(root) + ".Values").at("available").get<bool>() &&
                  missing_map.fields.at(field).at("value") == 41,
              "A delta with an unavailable base map cannot be fabricated as an empty container");
        f.capture = retained_capture;
        auto string_map = fixture::property(
            "Lookup", 4,
            {{"name", "MapProperty"},
             {"arguments", Json::array({{{"name", "StrProperty"}}, {{"name", "IntProperty"}}})}});
        f.capture["declarations"][root]["properties"].push_back(string_map);
        f.capture["declarations"][root]["slots"] = 5;
        f.capture["fields"][std::string(root) + ".Lookup"] =
            fixture::node(root, string_map, "0000000001000000fbffffff420061007300650000000b000000");
        context = f.read();
        p = f.package();
        p.exports[0].properties["Lookup"] = {
            {"type",
             {{"name", "MapProperty"},
              {"parameters", Json::array({{{"name", "StrProperty"}}, {{"name", "IntProperty"}}})}}},
            {"value",
             {{"kind", "stored-map-delta"},
              {"removed", Json::object()},
              {"entries", {{"\"base\"", {{"key", "base"}, {"value", 12}}}}}}}};
        auto string_result = resolve_source_defaults(p, "Asset", context);
        check(string_result.complete &&
                  string_result.fields.at(std::string(root) + ".Lookup").at("value").at("entries").size() ==
                      1 &&
                  string_result.fields.at(std::string(root) + ".Lookup")
                          .at("value")
                          .at("entries")
                          .at("\"base\"")
                          .at("value") == 12,
              "FString map keys use the selected native ASCII case-insensitive equality");
        f.capture = retained_capture;
        auto reference_property = fixture::property("Reference", 4, {{"name", "ObjectProperty"}});
        f.capture["declarations"][root]["properties"].push_back(reference_property);
        f.capture["declarations"][root]["slots"] = 5;
        f.capture["fields"][std::string(root) + ".Reference"] =
            fixture::node(root, reference_property, "00000000");
        context = f.read();
        p = f.package();
        p.exports[0].properties["Reference"] = tag("ObjectProperty", "None");
        check(resolve_source_defaults(p, "Asset", context)
                  .fields.at(std::string(root) + ".Reference")
                  .at("value")
                  .is_null(),
              "Stored null package references resolve to null rather than a literal None path");
        f.capture = retained_capture;
        auto saved = f.capture;
        f.capture["blueprintClass"] = true;
        rejects([&] { f.read(); }, "default-context");
        f.capture = saved;
        f.capture["structures"][record]["postSerialize"] = true;
        context = f.read();
        p = f.package();
        p.exports[0].properties["Record"] = {{"type",
                                              {{"name", "StructProperty"},
                                               {"parameters", Json::array({{{"name", "Record"}}})},
                                               {"structGuid", std::string(32, '0')}}},
                                             {"value", {{"Count", tag("IntProperty", 29)}}}};
        check(!resolve_source_defaults(p, "Asset", context).complete,
              "Native post-serialization cannot be replaced by a generic field merge");
        blueprint_checks(dir / "blueprint");
        std::cout << checks << " effective default checks passed\n";
        fs::remove_all(dir);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "After " << checks << ": " << e.what() << "\n";
        fs::remove_all(dir);
        return 1;
    }
}
