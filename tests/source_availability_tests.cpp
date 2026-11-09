#include "../src/source_availability.hpp"
#include <iostream>
#include <cyuediff/skeleton.hpp>
#include <cyuediff/collision.hpp>
#include "skeleton_model.hpp"
#include "collision_fixture.hpp"
using namespace cyuediff;
using namespace cyuediff::detail;
namespace {
unsigned checks = 0;
void check(bool v, const char *why) {
    ++checks;
    if (!v)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Expected guarded source coverage failure");
}
Json fp(char c = 'a') { return {{"bytes", 4}, {"sha256", std::string(64, c)}}; }
Json value(Json v) { return {{"type", {{"name", "IntProperty"}}}, {"value", v}}; }
Json unknown(char c = 'a') {
    return {{"type", {{"name", "IntProperty"}}},
            {"opaque", {{"bytes", 4}, {"sha256", std::string(64, c)}, {"reason", "opaque"}}}};
}
Package fixture() {
    Package p;
    ObjectExport e;
    e.path = "Asset/~A";
    e.coverage = "properties";
    e.properties = {{"Known", value(1)}, {"Opaque", value(2)}};
    p.exports.push_back(e);
    return p;
}
} // namespace
int main() {
    try {
        auto a = fixture(), b = a;
        b.exports[0].properties["Opaque"] = unknown();
        b.exports[0].properties["Known"]["value"] = 3;
        auto c = source_coverage(a, b);
        const std::string base = "/objects/Asset~1~0A";
        check(c.unavailable.size() == 1 && c.unavailable[0]["path"] == base + "/properties/Opaque/value" &&
                  c.unavailable[0]["afterUnavailable"] == true,
              "Opaque value is scoped using actual escaped object/property identity");
        check(c.excludes(base + "/properties/Opaque/value") &&
                  c.excludes(base + "/properties/Opaque/opaque") &&
                  !c.excludes(base + "/properties/Known/value"),
              "Unavailable values never mask a separate known changed field");
        b.exports[0].coverage = "partial-properties";
        check(source_coverage(a, b).unavailable == c.unavailable,
              "Completed tag inventory remains known when one value is unread");
        a.exports[0].properties["Opaque"] = unknown();
        b = a;
        b.exports[0].properties["Opaque"]["opaque"]["reason"] = "unsupported-new-context";
        c = source_coverage(a, b);
        check(c.unavailable.empty() && c.excludes(base + "/properties/Opaque/opaque/reason"),
              "Identical opaque byte hashes do not invent diagnostic-only asset changes");
        b.exports[0].properties["Opaque"]["opaque"]["sha256"] = std::string(64, 'b');
        c = source_coverage(a, b);
        check(c.unavailable.size() == 1 && !c.excludes(base + "/properties/Opaque/opaque/sha256"),
              "Changed unread bytes remain evidence with unresolved semantic value");
        b.exports[0].properties["Opaque"]["opaque"].erase("sha256");
        check(source_coverage(a, b).unavailable.size() == 1, "Missing raw hashes never prove equality");
        a = fixture();
        b = a;
        b.exports[0].coverage = "opaque";
        b.exports[0].properties = Json::object();
        b.exports[0].opaque["export"] = fp();
        c = source_coverage(a, b);
        check(c.excludes(base + "/properties/Known/value") && c.excludes(base + "/opaque") &&
                  c.unavailable.size() == 1,
              "Unread whole export is never an empty property map");
        a = b;
        check(source_coverage(a, b).unavailable.empty(),
              "Equal complete opaque export bytes remain unchanged");
        a = fixture();
        b = a;
        a.exports[0].native_coverage = "extension";
        a.exports[0].native_data = {{"decoder", "test/1"}, {"data", {{"number", 42}}}};
        b.exports[0].native_coverage = "opaque";
        b.exports[0].opaque["suffix"] = fp();
        c = source_coverage(a, b);
        check(c.excludes(base + "/native/data/number") && c.unavailable.size() == 1 &&
                  c.unavailable[0]["afterUnavailable"] == true &&
                  !c.unavailable[0]["beforeUnavailable"].get<bool>(),
              "Unavailable native peer does not remove a decoded native field");
        b = a;
        b.exports[0].native_data["data"]["number"] = 43;
        c = source_coverage(a, b);
        check(c.unavailable.empty() && !c.excludes(base + "/native/data/number") &&
                  !c.excludes(base + "/native/decoder"),
              "Known native fields compare independently of decoder label provenance");
        b.exports[0].native_coverage = "reference-skeleton";
        c = source_coverage(a, b);
        check(c.excludes(base + "/native") && c.unavailable[0]["beforeUnavailable"] == false &&
                  c.unavailable[0]["afterUnavailable"] == false,
              "Different native projection formats do not imply shared field inventories");
        a = fixture();
        b = a;
        a.exports[0].opaque["suffix"] = fp();
        b.exports[0].opaque["suffix"] = fp();
        check(source_coverage(a, b).unavailable.empty(),
              "Shared unchanged unknown suffix does not hide independent property changes");
        b.exports[0].opaque["suffix"] = fp('b');
        check(source_coverage(a, b).unavailable.size() == 1,
              "Changed opaque suffix preserves unknown semantic coverage");
        a = fixture();
        b = a;
        a.exports[0].properties["Known"]["value"] = nullptr;
        b = a;
        check(source_coverage(a, b).unavailable.empty(), "Stored null remains a known value");
        b.exports[0].properties["Known"]["resolvedType"] = {{"name", "IntProperty"}};
        c = source_coverage(a, b);
        check(c.unavailable.empty() && c.excludes(base + "/properties/Known/resolvedType"),
              "Resolved metadata is context rather than stored tag changes");
        SourceCoverage manual;
        manual.suppressed = {"/a", "/a/b"};
        check(manual.excludes("/a/c") && !manual.excludes("/ab"),
              "Ancestor masks keep JSON pointer boundaries despite intervening sorted paths");
        a = fixture();
        b = a;
        b.exports[0].properties["Known"]["value"] = 3;
        b.exports[0].properties["Opaque"] = unknown();
        auto diff = compare(a, b);
        check(diff.status() == "unverified" && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == base + "/properties/Known/value" &&
                  !diff.unavailable_scopes.empty(),
              "Package comparison retains a known changed field without inventing unread value removal");
        check(!diff.to_json().at("projectionComparisonComplete").get<bool>() &&
                  diff.to_json().at("unavailableScopes").size() == 1,
              "Public partial report separates intact input from unresolved semantic comparison");
        b = a;
        b.exports[0].coverage = "opaque";
        b.exports[0].properties = Json::object();
        b.exports[0].opaque["export"] = fp();
        diff = compare(a, b);
        check(diff.changes.empty() && diff.status() == "unverified",
              "A wholly unread peer does not remove all properties or add opaque partition metadata");
        a = fixture();
        b = a;
        a.exports[0].native_coverage = "extension";
        a.exports[0].native_data = {{"decoder", "test/1"}, {"data", {{"number", 42}}}};
        b.exports[0].native_coverage = "opaque";
        b.exports[0].opaque["suffix"] = fp();
        diff = compare(a, b);
        check(diff.changes.empty() && !diff.unavailable_scopes.empty(),
              "Native decoder failure does not remove fields through generic package comparison");
        a = fixture();
        b = a;
        a.exports[0].properties["Opaque"] = unknown();
        b.exports[0].properties["Opaque"] = unknown('b');
        diff = compare(a, b);
        check(diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == base + "/properties/Opaque/opaque/sha256" &&
                  diff.status() == "unverified",
              "Raw opaque changes remain explicitly unverified evidence");
        a = fixture();
        b = a;
        b.exports[0].properties["Known"]["resolvedType"] = {{"name", "IntProperty"}};
        check(compare(a, b).changes.empty(),
              "Context-completed type metadata does not invent a stored property change");
        a = fixture();
        a.exports[0].class_name = "/Script/Engine.StaticMesh";
        b = a;
        b.exports[0].coverage = "opaque";
        b.exports[0].properties = Json::object();
        b.exports[0].opaque["export"] = fp();
        diff = compare(a, b);
        check(diff.mesh_changes.empty() && diff.changes.empty() && diff.unavailable_scopes.size() >= 2,
              "A dependent mesh projection does not invent defaults/removals from an unread source export");
        a = fixture();
        a.exports[0].native_coverage = "extension";
        a.exports[0].native_data = {
            {"decoder", "explicit/1"},
            {"data", {{"type", {{"name", "IntProperty"}}}, {"opaque", fp()}, {"resolvedType", 1}}}};
        b = a;
        b.exports[0].native_data["data"]["resolvedType"] = 2;
        diff = compare(a, b);
        check(diff.unavailable_scopes.empty() && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == base + "/native/data/resolvedType",
              "Arbitrary native callback data never becomes opaque or context metadata by field-name "
              "heuristics");
        a = fixture();
        a.exports[0].properties["Opaque"] = {{"type", {{"name", "StructProperty"}}},
                                             {"value", {{"decoder", "custom/1"}, {"data", {{"known", 1}}}}}};
        b = a;
        b.exports[0].properties["Opaque"]["value"]["decoder"] = "custom/2";
        b.exports[0].properties["Opaque"]["value"]["data"] = {{"replacement", 2}};
        diff = compare(a, b);
        check(diff.changes.empty() && diff.unavailable_scopes.size() == 1,
              "A changed struct serializer cannot invent native member removals or additions");
        a = fixture();
        a.exports[0].properties["Opaque"] = unknown();
        b = a;
        b.exports[0].properties["Opaque"]["type"]["name"] = "FloatProperty";
        c = source_coverage(a, b);
        check(c.unavailable.size() == 1,
              "Equal opaque bytes under different stored types do not prove equal semantic values");
        a = fixture();
        a.exports[0].properties["Opaque"] = {{"type", {{"name", "StructProperty"}}},
                                             {"value", {{"decoder", "custom/1"}, {"data", {{"x", 1}}}}}};
        b = a;
        b.exports[0].properties["Opaque"]["value"] = {{"X", value(1)}};
        diff = compare(a, b);
        check(diff.changes.empty() && diff.unavailable_scopes.size() == 1,
              "Native callback and reflected struct representations need an explicit shared format");
        a = fixture();
        b = a;
        b.exports[0].properties["Known"]["serializationFlags"] = 32;
        b.exports[0].properties["Known"]["value"] = nullptr;
        diff = compare(a, b);
        check(diff.unavailable_scopes.size() == 1 &&
                  diff.unavailable_scopes[0]["reason"] == "skipped-property-value" &&
                  diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == base + "/properties/Known/serializationFlags",
              "Skipped serialization has no property value; it never changes a known value into null");
        a = b;
        check(source_coverage(a, b).unavailable.empty(),
              "Equal skipped tags prove unchanged stored markers without resolving a constructor default");
        a = fixture();
        const auto map = [](Json key, const std::string &id) {
            return Json{{"type", {{"name", "MapProperty"}}},
                        {"value",
                         {{"kind", "stored-map-delta"},
                          {"removed", Json::object()},
                          {"entries", {{id, {{"key", key}, {"value", 99}}}}}}}};
        };
        a.exports[0].properties["Opaque"] = map(Json{{"Part", value(1)}}, "known-key");
        b = a;
        b.exports[0].properties["Opaque"] = map(Json{{"Part", unknown()}}, "unread-key");
        diff = compare(a, b);
        check(diff.changes.empty() && diff.unavailable_scopes.size() == 1 &&
                  diff.unavailable_scopes[0]["reason"] == "map-key-identity-unavailable",
              "An unread struct key cannot invent removed and added logical map entries");

        a = fixture();
        a.exports[0].properties["Opaque"] = map(1, "one");
        a.exports[0].properties["Opaque"]["value"]["removed"] = {{"known", Json{{"Part", value(1)}}}};
        b = a;
        b.exports[0].properties["Opaque"]["value"]["removed"] = {{"unread", Json{{"Part", unknown()}}}};
        diff = compare(a, b);
        check(diff.changes.empty() && diff.unavailable_scopes.size() == 1,
              "Unread removed map keys cannot imply a changed logical deletion set");
        a = fixture();
        a.exports[0].properties["Opaque"] = {{"type", {{"name", "ArrayProperty"}}},
                                             {"value", Json::array({value(1), value(2)})}};
        b = a;
        b.exports[0].properties["Opaque"]["value"][0] = unknown();
        b.exports[0].properties["Opaque"]["value"][1]["value"] = 3;
        b.exports[0].properties["Opaque"]["value"].push_back(value(4));
        diff = compare(a, b);
        check(diff.status() == "unverified" && diff.changes.size() == 2 &&
                  diff.changes[0]["path"] == base + "/properties/Opaque/value/1/value" &&
                  diff.changes[1]["op"] == "add",
              "Unequal arrays retain known index changes without replacing an unread element value");
        a = fixture();
        b = a;
        b.summary.ue5_version = 1009;
        b.decoder_context_sha256 = std::string(64, 'a');
        diff = compare(a, b);
        check(diff.changes.empty() && diff.source_contexts.at("changed") == true,
              "Archive and catalogue provenance stays separate from stored asset changes");
        SourceCoverage projected_raw;
        projected_raw.unavailable.push_back(
            {{"object", "Unread"}, {"reason", "export-properties-unavailable"}});
        Json projected = {{"meshes",
                           {{"Known", {{"settings", {{"path", "Unread"}, {"value", 1}}}}},
                            {"Unread", {{"settings", Json::object()}}}}}};
        auto guarded = projection_coverage(projected_raw, a, b, projected, projected, "meshes");
        check(!guarded.excludes("/meshes/Known/settings/path") && guarded.excludes("/meshes/Unread") &&
                  guarded.excludes("/complete"),
              "Only projector-owned identities determine dependency scopes; user fields named path remain "
              "stored data");
        auto rig = test::skeleton_model(), unread_rig = rig;
        unread_rig.exports[0].native_data = Json::object();
        unread_rig.exports[0].native_coverage = "opaque";
        auto skeleton_diff = compare_skeletons(rig, unread_rig);
        check(skeleton_diff.status() == "incomplete" && skeleton_diff.changes.empty() &&
                  !skeleton_diff.to_json().at("unavailableScopes").empty(),
              "The dedicated source skeleton API cannot remove every bone after a decoder failure");
        auto body = test::collision_fixture(), unread_body = body;
        unread_body.exports[0].coverage = "opaque";
        unread_body.exports[0].properties = Json::object();
        auto collision_diff = compare_collisions(body, unread_body);
        check(!collision_diff.complete && collision_diff.changes.empty() &&
                  !collision_diff.unavailable_scopes.empty(),
              "The dedicated collision API preserves unknown source geometry");
        a = fixture();
        a.exports[0].class_name = "/Script/Engine.Material";
        a.exports[0].properties["Expressions"] = value(Json::array());
        b = a;
        b.exports[0].coverage = "opaque";
        b.exports[0].properties = Json::object();
        auto material_diff = compare_materials(a, b);
        check(!material_diff.complete && material_diff.changes.empty() &&
                  !material_diff.unavailable_scopes.empty(),
              "The dedicated material API never replaces a readable graph with an unread empty peer");
        a = fixture();
        b = a;
        b.exports[0].properties["Known"]["resolvedType"] = {{"name", "FloatProperty"}};
        diff = compare(a, b);
        check(diff.changes.empty() && diff.status() == "unverified" &&
                  diff.unavailable_scopes[0]["reason"] == "source-type-format-incompatible" &&
                  !diff.unavailable_scopes[0]["beforeUnavailable"].get<bool>(),
              "Equal readable JSON under incompatible effective source types is not verified semantic "
              "equality");
        a = fixture();
        b = a;
        b.exports[0].properties["Known"]["type"] = "IntProperty";
        diff = compare(a, b);
        check(diff.unavailable_scopes.empty() && diff.changes.empty(),
              "Equivalent public descriptor string/object forms do not make known values unavailable");
        a = fixture();
        b = a;
        b.exports[0].coverage = "metadata";
        b.exports[0].properties["Known"]["value"] = 99;
        diff = compare(a, b);
        check(diff.changes.empty() && diff.status() == "unverified",
              "Metadata-only coverage stays authoritative even if a host leaves stale property values in its "
              "mutable package");
        MaterialComparison aggregate_material{Json::array(), true};
        CollisionComparison aggregate_collision{Json::array(), true};
        SkeletonComparison aggregate_skeleton{Json::array(), true};
        check(aggregate_material.complete && aggregate_collision.complete && aggregate_skeleton.complete &&
                  aggregate_material.unavailable_scopes.empty(),
              "Availability members append after the existing aggregate initializer prefix");
        SourceCoverageOptions limits;
        limits.max_nodes = 1;
        rejects([&] { source_coverage(a, b, limits); });
        limits = {};
        limits.max_path_bytes = 1;
        rejects([&] { source_coverage(a, b, limits); });
        limits = {};
        limits.control.cancelled = [] { return true; };
        rejects([&] { source_coverage(a, b, limits); });
        b.exports.push_back(b.exports[0]);
        rejects([&] { source_coverage(a, b); });
        std::cout << checks << " source coverage checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
