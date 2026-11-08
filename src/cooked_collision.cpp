#include <cyuediff/collision.hpp>
#include <cyuediff/zen.hpp>
#include "collision_internal.hpp"
#include <cmath>
#include <algorithm>
namespace cyuediff {
namespace {
struct Normalizer {
    const CollisionOptions &options;
    std::size_t nodes = 0, text_bytes = 0;
    void text(std::size_t n) {
        if (n > options.max_text_bytes - text_bytes)
            throw Error("limit", "Cooked collision text budget exceeded");
        text_bytes += n;
    }
    static bool property(const Json &v) {
        return v.is_object() && v.contains("type") && (v.contains("value") || v.contains("opaque"));
    }
    Json normalize(const Json &value, unsigned depth = 0) {
        if (depth > options.max_depth || nodes++ >= options.max_nodes)
            throw Error("limit", "Cooked collision node/depth budget exceeded");
        if ((nodes & 127) == 0)
            options.control.check();
        if (value.is_object()) {
            const bool fields = !value.empty() && std::all_of(value.begin(), value.end(),
                                                              [](const auto &v) { return property(v); });
            Json result = Json::object();
            for (auto it = value.begin(); it != value.end(); ++it) {
                text(it.key().size());
                auto key = it.key();
                if (fields) {
                    const auto dot = key.rfind('.');
                    if (dot != std::string::npos)
                        key = key.substr(dot + 1);
                    if (result.contains(key))
                        throw Error("collision-identity",
                                    "Ambiguous qualified cooked collision field: " + key);
                }
                result[key] = normalize(it.value(), depth + 1);
            }
            if (property(value) && value.contains("value") && value["type"].is_string()) {
                const auto kind = value["type"].get<std::string>();
                if (kind == "ObjectProperty" || kind == "ClassProperty") {
                    auto &reference = result["value"];
                    if (reference.is_object() && reference.value("kind", std::string{}) == "export" &&
                        reference.contains("path") && reference["path"].is_string())
                        reference = reference["path"];
                }
            }
            return result;
        }
        if (value.is_array()) {
            Json result = Json::array();
            for (const auto &v : value)
                result.push_back(normalize(v, depth + 1));
            return result;
        }
        if (value.is_string())
            text(value.get_ref<const std::string &>().size());
        if (value.is_number_float() && !std::isfinite(value.get<double>()))
            throw Error("collision-value", "Non-finite cooked collision value");
        return value;
    }
};
bool relevant(const ZenExport &e) {
    return !(e.flags & 0x30) &&
           (e.class_name == "/Script/Engine.BodySetup" ||
            e.class_name == "/Script/Engine.SkeletalBodySetup" ||
            e.class_name == "/Script/Engine.StaticMesh" || e.class_name == "/Script/Engine.PhysicsAsset");
}
} // namespace
Json collision_snapshot(const ZenPackage &package, const CollisionOptions &options) {
    options.control.checkpoint("Projecting cooked collision shapes");
    Normalizer normalizer{options};
    std::vector<Json> properties;
    std::vector<detail::CollisionInput> inputs;
    const auto count = std::count_if(package.exports.begin(), package.exports.end(), relevant);
    if (static_cast<std::size_t>(count) > options.max_bodies &&
        static_cast<std::size_t>(count) - options.max_bodies > options.max_bodies)
        throw Error("limit", "Cooked collision input exceeds body/owner budget");
    properties.reserve(count);
    inputs.reserve(count);
    for (const auto &e : package.exports) {
        options.control.check();
        if (!relevant(e))
            continue;
        const bool decoded =
            e.property_read.is_object() && e.property_read.value("status", std::string{}) == "decoded";
        std::uint64_t consumed = 0;
        if (decoded) {
            const auto offset = e.property_read.at("offset").get<std::uint64_t>();
            const auto bytes = e.property_read.at("consumedBytes").get<std::uint64_t>();
            if (offset > e.size || bytes > e.size - offset)
                throw Error("collision-value", "Cooked collision property boundary exceeds export");
            consumed = offset + bytes;
            properties.push_back(normalizer.normalize(e.property_read.at("properties")));
        } else
            properties.push_back(Json::object());
        detail::CollisionInput input;
        input.path = e.path;
        input.class_name = e.class_name;
        input.properties = &properties.back();
        input.properties_complete = decoded;
        input.native = {{"coverage", decoded ? "opaque-cooked-native" : "opaque-cooked-body"},
                        {"bytes", e.size - consumed},
                        {"opaque", {{"exportBodySha256", e.sha256}}}};
        if (!decoded)
            input.native["opaque"]["propertyRead"] = e.property_read;
        const auto outer = e.references.find("outer");
        if (outer != e.references.end() && !outer->is_null()) {
            if (outer->is_object() && outer->value("kind", std::string{}) == "export" &&
                outer->contains("path") && (*outer)["path"].is_string())
                input.outer = (*outer)["path"];
            else {
                input.outer_resolved = false;
                input.outer_index = e.outer_index;
            }
        }
        inputs.push_back(std::move(input));
    }
    for (const auto *s : {&package.profile, &package.name, &package.sha256, &package.schema_context_sha256,
                          &package.script_objects_sha256})
        normalizer.text(s->size());
    auto limits = options;
    limits.max_nodes -= normalizer.nodes;
    limits.max_text_bytes -= normalizer.text_bytes;
    const bool body_table_known =
        std::all_of(package.exports.begin(), package.exports.end(),
                    [](const auto &e) { return !e.class_name.empty() && e.class_name.front() != '$'; });
    auto result = detail::project_collision_inputs(inputs, body_table_known, limits);
    result["scope"] =
        "stored mapped cooked collision properties; native physics and archetype defaults unresolved";
    result["provenance"] = {{"profile", package.profile},
                            {"package", package.name},
                            {"packageSha256", package.sha256},
                            {"schemaContextSha256", package.schema_context_sha256},
                            {"scriptObjectsSha256", package.script_objects_sha256},
                            {"mappingVerified", false},
                            {"nativePhysicsDecoded", false}};
    options.control.check();
    return result;
}
} // namespace cyuediff
