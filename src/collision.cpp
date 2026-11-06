#include <cyuediff/collision.hpp>
#include <algorithm>
#include <cmath>
#include <set>
namespace cyuediff {
namespace {
const Json *value(const Json &properties, const std::string &name) {
    if (!properties.is_object())
        return nullptr;
    const auto it = properties.find(name);
    return it != properties.end() && it->is_object() && it->contains("value") ? &it->at("value") : nullptr;
}
struct Projection {
    const CollisionOptions &options;
    std::size_t nodes = 0, text_bytes = 0, shapes = 0, vertices = 0, indices = 0;
    void text(std::size_t count) {
        if (count > options.max_text_bytes - std::min(text_bytes, options.max_text_bytes))
            throw Error("limit", "Collision projection exceeds text budget");
        text_bytes += count;
    }
    Json copy(const Json &source, unsigned depth = 0) {
        if (depth > options.max_depth || nodes++ >= options.max_nodes)
            throw Error("limit", "Collision projection exceeds node/depth budget");
        if (nodes % 256 == 0)
            options.control.check();
        if (source.is_object()) {
            Json out = Json::object();
            for (auto it = source.begin(); it != source.end(); ++it) {
                text(it.key().size());
                out[it.key()] = copy(*it, depth + 1);
            }
            return out;
        }
        if (source.is_array()) {
            Json out = Json::array();
            for (const auto &item : source)
                out.push_back(copy(item, depth + 1));
            return out;
        }
        if (source.is_string())
            text(source.get_ref<const std::string &>().size());
        if (source.is_number_float() && !std::isfinite(source.get<double>()))
            throw Error("collision-value", "Non-finite collision metadata");
        return source;
    }
    static bool number(const Json &v, bool nonnegative = false) {
        return v.is_number() && std::isfinite(v.get<double>()) && (!nonnegative || v.get<double>() >= 0);
    }
    static Json vector(const Json &v, bool rotation) {
        if (v.is_array() && v.size() == 3 &&
            std::all_of(v.begin(), v.end(), [](const auto &x) { return number(x); }))
            return v;
        if (v.is_object()) {
            Json out = Json::array();
            const std::array<const char *, 3> keys = rotation ? std::array<const char *, 3>{"Pitch", "Yaw", "Roll"}
                                                               : std::array<const char *, 3>{"X", "Y", "Z"};
            for (const auto *key : keys) {
                const auto *component = value(v, key);
                if (!component || !number(*component))
                    return nullptr;
                out.push_back(*component);
            }
            return out;
        }
        return nullptr;
    }
    Json shape(const Json &properties, const std::string &kind, std::size_t source_index, bool named) {
        if (shapes++ >= options.max_shapes)
            throw Error("limit", "Collision projection exceeds shape budget");
        options.control.check();
        Json out = {{"kind", kind},
                    {"identity", named ? "name" : "index"},
                    {"sourceIndex", source_index},
                    {"name", nullptr},
                    {"geometry", Json::object()},
                    {"settings", Json::object()},
                    {"extraProperties", Json::object()},
                    {"unresolvedProperties", Json::object()},
                    {"complete", true},
                    {"diagnostics", Json::array()}};
        if (named && !options.source_indices)
            out.erase("sourceIndex");
        if (!properties.is_object()) {
            out["unresolvedProperties"]["element"] = copy(properties);
            out["complete"] = false;
            out["diagnostics"].push_back("Collision element is not a decoded reflected struct");
            return out;
        }
        if (const auto *name = value(properties, "Name"); name && name->is_string())
            out["name"] = copy(*name);
        if (properties.contains("Name") &&
            (!value(properties, "Name") || !value(properties, "Name")->is_string())) {
            out["unresolvedProperties"]["Name"] = copy(properties.at("Name"));
            out["complete"] = false;
        }
        std::set<std::string> handled = {"Name"};
        auto geometry = [&](const char *field, const char *target, bool vector_field,
                            bool nonnegative = false) {
            handled.insert(field);
            const auto *v = value(properties, field);
            Json decoded = v ? (vector_field ? vector(*v, std::string(field) == "Rotation")
                                             : (number(*v, nonnegative) ? *v : Json(nullptr)))
                             : Json(nullptr);
            if (!decoded.is_null())
                out["geometry"][target] = copy(decoded);
            else {
                out["complete"] = false;
                out["diagnostics"].push_back(std::string("Missing or unresolved stored geometry field: ") +
                                             field);
                if (properties.contains(field))
                    out["unresolvedProperties"][field] = copy(properties.at(field));
            }
        };
        if (kind == "convex") {
            handled.insert("VertexData");
            handled.insert("IndexData");
            handled.insert("Transform");
            const auto *points = value(properties, "VertexData"), *faces = value(properties, "IndexData"),
                       *transform = value(properties, "Transform");
            auto unresolved = [&](const char *field) {
                out["complete"] = false;
                out["diagnostics"].push_back(std::string("Missing or unresolved convex field: ") + field);
                if (properties.contains(field))
                    out["unresolvedProperties"][field] = copy(properties.at(field));
            };
            bool valid_points = points && points->is_array();
            Json decoded = Json::array();
            if (valid_points) {
                if (points->size() > options.max_vertices - std::min(vertices, options.max_vertices))
                    throw Error("limit", "Collision projection exceeds convex vertex budget");
                vertices += points->size();
                for (const auto &point : *points) {
                    options.control.check();
                    auto v = vector(point, false);
                    if (v.is_null()) {
                        valid_points = false;
                        break;
                    }
                    decoded.push_back(copy(v));
                }
            }
            if (valid_points)
                out["geometry"]["vertices"] = std::move(decoded);
            else
                unresolved("VertexData");
            bool valid_indices = valid_points && faces && faces->is_array() && faces->size() % 3 == 0;
            if (faces && faces->is_array()) {
                if (faces->size() > options.max_indices - std::min(indices, options.max_indices))
                    throw Error("limit", "Collision projection exceeds convex index budget");
                indices += faces->size();
            }
            if (valid_indices)
                for (const auto &index : *faces) {
                    options.control.check();
                    bool valid = index.is_number_unsigned()
                                     ? index.get<std::uint64_t>() < points->size()
                                     : index.is_number_integer() && index.get<std::int64_t>() >= 0 &&
                                           std::uint64_t(index.get<std::int64_t>()) < points->size();
                    if (!valid) {
                        valid_indices = false;
                        break;
                    }
                }
            if (valid_indices)
                out["geometry"]["indices"] = copy(*faces);
            else
                unresolved("IndexData");
            bool valid_transform = transform && transform->is_object();
            if (valid_transform)
                for (const auto &component :
                     {std::make_pair("rotation", 4u), std::make_pair("translation", 3u),
                      std::make_pair("scale", 3u)}) {
                    auto v = transform->find(component.first);
                    valid_transform =
                        valid_transform && v != transform->end() && v->is_array() &&
                        v->size() == component.second &&
                        std::all_of(v->begin(), v->end(), [](const auto &x) { return number(x); });
                }
            if (valid_transform)
                out["geometry"]["transform"] = copy(*transform);
            else
                unresolved("Transform");
        } else {
            geometry("Center", "center", true);
            if (kind != "sphere")
                geometry("Rotation", "rotation", true);
            if (kind == "box") {
                geometry("X", "x", false, true);
                geometry("Y", "y", false, true);
                geometry("Z", "z", false, true);
            } else if (kind == "tapered-capsule") {
                geometry("Radius0", "radius0", false, true);
                geometry("Radius1", "radius1", false, true);
                geometry("Length", "length", false, true);
            } else {
                geometry("Radius", "radius", false, true);
                if (kind == "capsule")
                    geometry("Length", "length", false, true);
            }
        }
        for (const char *field : {"RestOffset", "bContributeToMass", "CollisionEnabled", "bIsGenerated"}) {
            handled.insert(field);
            if (properties.contains(field))
                out["settings"][field] = copy(properties.at(field));
        }
        for (auto it = properties.begin(); it != properties.end(); ++it)
            if (!handled.count(it.key()))
                out["extraProperties"][it.key()] = copy(*it);
        return out;
    }
};
bool body_type(const ObjectExport &entry) {
    return entry.class_name == "/Script/Engine.BodySetup" ||
           entry.class_name == "/Script/Engine.SkeletalBodySetup";
}
} // namespace
Json collision_snapshot(const Package &package, const CollisionOptions &options) {
    options.control.checkpoint("Projecting stored collision shapes");
    Projection projection{options};
    Json bodies = Json::object();
    bool complete = package.complete_input;
    for (const auto &entry : package.exports) {
        options.control.check();
        if (!body_type(entry))
            continue;
        if (bodies.size() >= options.max_bodies)
            throw Error("limit", "Collision projection exceeds body budget");
        if (bodies.contains(entry.path))
            throw Error("collision-identity", "Duplicate collision body path");
        projection.text(entry.path.size() + entry.class_name.size());
        Json body = {
            {"class", entry.class_name},
            {"properties", Json::object()},
            {"shapes", Json::object()},
            {"collections", Json::object()},
            {"unresolvedCollections", Json::object()},
            {"native",
             {{"coverage", entry.native_coverage},
              {"bytes", entry.native_end >= entry.native_start ? entry.native_end - entry.native_start : 0},
              {"opaque", projection.copy(entry.opaque)}}},
            {"complete", entry.coverage == "properties"},
            {"diagnostics", Json::array()}};
        if (entry.properties.is_object())
            for (auto it = entry.properties.begin(); it != entry.properties.end(); ++it)
                if (it.key() != "AggGeom")
                    body["properties"][it.key()] = projection.copy(*it);
        if (entry.outer_index) {
            try {
                body["outer"] = projection.copy(package.reference(entry.outer_index));
            } catch (const Error &) {
                body["outerIndex"] = entry.outer_index;
                body["complete"] = false;
                body["diagnostics"].push_back("Collision body has an unresolved outer reference");
            }
        } else
            body["outer"] = nullptr;
        const auto *aggregate = value(entry.properties, "AggGeom");
        if (!aggregate || !aggregate->is_object()) {
            body["complete"] = false;
            body["diagnostics"].push_back(
                "Stored aggregate geometry is unavailable; omitted fields are not defaulted");
            if (entry.properties.contains("AggGeom"))
                body["unresolvedCollections"]["AggGeom"] = projection.copy(entry.properties.at("AggGeom"));
        } else {
            const std::map<std::string, std::string> supported = {{"SphereElems", "sphere"},
                                                                  {"BoxElems", "box"},
                                                                  {"SphylElems", "capsule"},
                                                                  {"TaperedCapsuleElems", "tapered-capsule"},
                                                                  {"ConvexElems", "convex"}};
            for (auto collection = aggregate->begin(); collection != aggregate->end(); ++collection) {
                const auto kind = supported.find(collection.key());
                const auto *array = value(*aggregate, collection.key());
                if ((collection.key() == "LevelSetElems" || collection.key() == "SkinnedLevelSetElems") &&
                    array && array->is_array() && array->empty()) {
                    body["collections"][collection.key()] = 0;
                    continue;
                }
                if (kind == supported.end() || !array || !array->is_array()) {
                    body["unresolvedCollections"][collection.key()] = projection.copy(*collection);
                    body["complete"] = false;
                    body["diagnostics"].push_back("Unsupported or unresolved collision collection: " +
                                                  collection.key());
                    continue;
                }
                if (array->size() > options.max_shapes - std::min(projection.shapes, options.max_shapes))
                    throw Error("limit", "Collision projection exceeds shape budget");
                body["collections"][collection.key()] = array->size();
                std::map<std::string, std::size_t> names;
                for (const auto &item : *array) {
                    options.control.check();
                    const auto *name = value(item, "Name");
                    if (name && name->is_string() && *name != "" && *name != "None")
                        ++names[name->get<std::string>()];
                }
                for (std::size_t i = 0; i < array->size(); ++i) {
                    const auto &properties = array->at(i);
                    const auto *name = value(properties, "Name");
                    const auto label = name && name->is_string() ? name->get<std::string>() : std::string{};
                    const bool named = !label.empty() && label != "None" && names.at(label) == 1;
                    const auto key =
                        kind->second + ":" + (named ? "name:" + label : "index:" + std::to_string(i));
                    projection.text(key.size());
                    auto shape = projection.shape(properties, kind->second, i, named);
                    if (!named) {
                        shape["diagnostics"].push_back(
                            "No unique stored shape name; identity uses collection index");
                        if (!label.empty() && label != "None")
                            body["diagnostics"].push_back("Duplicate collision shape name: " + label);
                    }
                    if (!shape["complete"].get<bool>())
                        body["complete"] = false;
                    body["shapes"][key] = std::move(shape);
                }
            }
        }
        if (!body["complete"].get<bool>())
            complete = false;
        bodies[entry.path] = std::move(body);
    }
    Json owners = Json::object();
    for (const auto &entry : package.exports) {
        options.control.check();
        const char *field = entry.class_name == "/Script/Engine.StaticMesh"     ? "BodySetup"
                            : entry.class_name == "/Script/Engine.PhysicsAsset" ? "SkeletalBodySetups"
                                                                                : nullptr;
        if (!field)
            continue;
        if (owners.size() >= options.max_bodies)
            throw Error("limit", "Collision projection exceeds owner budget");
        if (owners.contains(entry.path))
            throw Error("collision-identity", "Duplicate collision owner path");
        projection.text(entry.path.size() + entry.class_name.size());
        const auto *references = value(entry.properties, field);
        if (references && references->is_array() && references->size() > options.max_bodies)
            throw Error("limit", "Collision owner exceeds reference budget");
        Json owner = {{"class", entry.class_name},
                      {"bodyReferences", Json::array()},
                      {"complete", true},
                      {"diagnostics", Json::array()}};
        owner["containedBodies"] = Json::array();
        for (auto it = bodies.begin(); it != bodies.end(); ++it)
            if (it->contains("outer") && it->at("outer") == entry.path)
                owner["containedBodies"].push_back(projection.copy(it.key()));
        if (entry.properties.contains(field))
            owner["property"] = projection.copy(entry.properties.at(field));
        auto reference = [&](const Json &ref) {
            options.control.check();
            const bool resolved = ref.is_string() && bodies.contains(ref.get<std::string>());
            owner["bodyReferences"].push_back({{"path", projection.copy(ref)}, {"resolved", resolved}});
            if (!resolved && !ref.is_null() && ref != "None") {
                owner["complete"] = false;
                owner["diagnostics"].push_back("Collision body reference is external, missing or unresolved");
            }
        };
        if (!references || (std::string(field) == "SkeletalBodySetups" && !references->is_array())) {
            owner["complete"] = false;
            owner["diagnostics"].push_back("Stored collision body references are unavailable");
        } else if (references->is_array()) {
            if (references->size() > options.max_bodies)
                throw Error("limit", "Collision owner exceeds reference budget");
            for (const auto &ref : *references)
                reference(ref);
        } else
            reference(*references);
        if (!owner["complete"].get<bool>())
            complete = false;
        owners[entry.path] = std::move(owner);
    }
    if (bodies.empty())
        complete = false;
    return {{"schema", "cyuediff.collision/1"},
            {"scope", "stored reflected collision properties; no physics cooking or default reconstruction"},
            {"complete", complete},
            {"bodies", std::move(bodies)},
            {"owners", std::move(owners)}};
}
Json Package::collision_snapshot() const {
    return cyuediff::collision_snapshot(*this);
}
} // namespace cyuediff
