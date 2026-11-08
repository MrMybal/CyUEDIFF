#include <cyuediff/cooked_physics.hpp>
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
namespace cyuediff {
namespace {
void need(bool ok, const char *s) {
    if (!ok)
        throw Error("cooked-physics-review", s);
}
struct Review {
    const CookedPhysicsOptions &o;
    std::size_t max_edges, edges = 0, shapes = 0;
    std::uint64_t output = 0, vertices = 0, topology = 0;
    void reserve(std::uint64_t n) {
        if (output > o.max_output_bytes || n > o.max_output_bytes - output)
            throw Error("cooked-physics-limit", "Native physics review exceeds output budget");
        output += n;
    }
    void add(std::uint64_t &used, std::uint64_t n, std::uint64_t cap) {
        if (used > cap || n > cap - used)
            throw Error("cooked-physics-limit", "Native physics review exceeds element budget");
        used += n;
    }
    Json copy(const Json &j, unsigned depth = 0) {
        if (depth > 32)
            throw Error("cooked-physics-limit", "Native physics review depth exceeds limit");
        o.control.check();
        reserve(64);
        if (j.is_object()) {
            Json v = Json::object();
            for (auto i = j.begin(); i != j.end(); ++i) {
                reserve(i.key().size());
                v[i.key()] = copy(i.value(), depth + 1);
            }
            return v;
        }
        if (j.is_array()) {
            Json v = Json::array();
            for (const auto &x : j)
                v.push_back(copy(x, depth + 1));
            return v;
        }
        if (j.is_string())
            reserve(j.get_ref<const std::string &>().size());
        need(!j.is_number_float() || std::isfinite(j.get<double>()), "Non-finite native review value");
        return j;
    }
    std::size_t index(const Json &j, std::size_t n) {
        need(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
             "Invalid native physics review index");
        const auto i = j.get<std::uint64_t>();
        need(i < n, "Native physics review index exceeds table");
        return std::size_t(i);
    }
    CollisionPoint point(const Json &j) {
        need(j.is_array() && j.size() == 3, "Invalid native physics point");
        CollisionPoint p{};
        for (unsigned i = 0; i < 3; ++i) {
            need(j[i].is_number(), "Invalid native physics coordinate");
            p[i] = j[i].get<double>();
            need(std::isfinite(p[i]), "Non-finite native physics coordinate");
        }
        return p;
    }
    static void include(CollisionBounds &b, const CollisionPoint &p) {
        if (!b.valid) {
            b.low = b.high = p;
            b.valid = true;
        } else
            for (unsigned i = 0; i < 3; ++i) {
                b.low[i] = std::min(b.low[i], p[i]);
                b.high[i] = std::max(b.high[i], p[i]);
            }
    }
    void geometry(CollisionWireShape &shape, const Json &object, CollisionBounds &bounds) {
        const auto &points = object.at("vertices");
        need(points.is_array(), "Invalid native physics vertices");
        add(vertices, points.size(), o.max_vertices);
        std::set<std::pair<std::size_t, std::size_t>> unique;
        auto line = [&](std::size_t a, std::size_t b) {
            o.control.check();
            if (a == b || !unique.emplace(std::min(a, b), std::max(a, b)).second)
                return;
            if (edges >= max_edges)
                throw Error("cooked-physics-limit", "Native physics review exceeds edge budget");
            ++edges;
            reserve(sizeof(CollisionLine) + 96);
            CollisionLine e{point(points[a]), point(points[b])};
            include(shape.bounds, e.a);
            include(shape.bounds, e.b);
            include(bounds, e.a);
            include(bounds, e.b);
            shape.lines.push_back(e);
        };
        const auto kind = object.at("kind").get<std::string>();
        if (kind == "triangleMesh") {
            const auto &triangles = object.at("triangles");
            need(triangles.is_array(), "Invalid native physics triangles");
            add(topology, triangles.size(), o.max_topology);
            for (const auto &t : triangles) {
                need(t.is_array() && t.size() == 3, "Invalid native physics triangle");
                std::array<std::size_t, 3> v{};
                for (unsigned k = 0; k < 3; ++k)
                    v[k] = index(t[k], points.size());
                for (unsigned k = 0; k < 3; ++k)
                    line(v[k], v[(k + 1) % 3]);
            }
        } else if (kind == "convex") {
            if (!object.contains("faces") || !object.contains("halfEdges")) {
                shape.diagnostics.push_back("Native convex face topology is not stored");
                return;
            }
            const auto &faces = object.at("faces"), &half = object.at("halfEdges");
            need(faces.is_array() && half.is_array(), "Invalid native convex topology");
            add(topology, faces.size(), o.max_topology);
            add(topology, half.size(), o.max_topology);
            for (const auto &face : faces) {
                o.control.check();
                need(face.is_array() && face.size() == 2, "Invalid native convex face");
                auto first = index(face[0], half.size());
                need(face[1].is_number_integer() && face[1].get<std::int64_t>() >= 3,
                     "Invalid native convex edge count");
                auto count = face[1].get<std::size_t>();
                need(count <= half.size() - first, "Native convex face exceeds edge table");
                for (std::size_t j = 0; j < count; ++j) {
                    const auto &a = half[first + j], &b = half[first + (j + 1) % count];
                    need(a.is_array() && a.size() == 3 && b.is_array() && b.size() == 3,
                         "Invalid native convex edge");
                    line(index(a[1], points.size()), index(b[1], points.size()));
                }
            }
        } else
            need(false, "Unsupported native physics review geometry type");
    }
    CookedPhysicsReview build(const Json &snapshot) {
        o.control.checkpoint("Preparing native physics geometry");
        need(snapshot.is_object() && snapshot.value("schema", std::string{}) == "cyuediff.cooked-physics/1",
             "Invalid cooked physics snapshot");
        need(snapshot.at("bodies").is_array(), "Invalid cooked physics body table");
        if (snapshot["bodies"].size() > o.max_bodies)
            throw Error("cooked-physics-limit", "Native physics review body budget exceeded");
        CookedPhysicsReview out;
        out.model = {{"schema", "cyuediff.collision/1"},
                     {"space", "cooker"},
                     {"scope", "decoded native Chaos cooker geometry; no simulation or owner transforms"},
                     {"complete", snapshot.at("complete")},
                     {"bodyTableKnown", snapshot.at("bodyTableKnown")},
                     {"bodies", Json::object()},
                     {"owners", Json::object()},
                     {"provenance", copy(snapshot.at("provenance"))}};
        out.model["provenance"]["nativePhysicsDecoded"] = true;
        out.wire.source_complete = snapshot.at("complete");
        out.wire.space = "cooker";
        std::uint64_t formats = 0, objects = 0;
        for (const auto &body : snapshot["bodies"]) {
            o.control.check();
            auto path = body.at("object").get<std::string>();
            reserve(8192 + path.size());
            need(!out.model["bodies"].contains(path), "Duplicate native physics body path");
            Json b = {{"class", copy(body.at("class"))},
                      {"complete", body.at("complete")},
                      {"nativeFormatTableKnown", true},
                      {"collections", Json::object()},
                      {"unavailableFormats", Json::array()},
                      {"shapes", Json::object()},
                      {"diagnostics", Json::array()},
                      {"properties",
                       {{"hasCookedCollisionData", body.at("hasCookedCollisionData")},
                        {"formats", Json::object()}}}};
            need(body.at("formats").is_array(), "Invalid native physics formats");
            add(formats, body["formats"].size(), o.max_formats);
            for (const auto &format : body["formats"]) {
                auto name = format.at("name").get<std::string>() + "#" +
                            std::to_string(format.at("number").get<std::uint32_t>());
                reserve(4096 + name.size());
                need(!b["properties"]["formats"].contains(name), "Duplicate native physics format");
                auto &details = b["properties"]["formats"][name];
                details = {{"status", copy(format.at("status"))}};
                if (format.at("status") != "decoded") {
                    b["unavailableFormats"].push_back("chaos/" + name + "/");
                    b["diagnostics"].push_back("Native format unavailable: " + name);
                    continue;
                }
                const auto &physics = format.at("physics");
                need(physics.at("objects").is_array(), "Invalid native physics object table");
                add(objects, physics["objects"].size(), o.max_objects);
                std::map<std::uint32_t, const Json *> tags;
                for (const auto &object : physics["objects"]) {
                    auto tag = object.at("tag").get<std::uint32_t>();
                    need(tags.emplace(tag, &object).second, "Duplicate native physics graph tag");
                }
                for (const auto *key : {"uvIndices", "uvPositions", "uvChannels", "faceRemap"})
                    details[key] = copy(physics.at(key));
                for (const auto *refs : {"convexRefs", "triangleMeshRefs"}) {
                    const std::string kind = std::string(refs) == "convexRefs" ? "convex" : "triangleMesh";
                    const auto collection = "chaos/" + name + "/" + kind;
                    need(physics.at(refs).is_array(), "Invalid native physics references");
                    const auto &references = physics.at(refs);
                    b["collections"][collection] = references.size();
                    for (std::size_t i = 0; i < references.size(); ++i) {
                        o.control.check();
                        if (shapes >= o.max_references)
                            throw Error("cooked-physics-limit",
                                        "Native physics review shape budget exceeded");
                        ++shapes;
                        reserve(4096 + collection.size());
                        auto identity = collection + "/index:" + std::to_string(i);
                        Json shape = {{"kind", kind},
                                      {"collection", collection},
                                      {"complete", true},
                                      {"diagnostics", Json::array()},
                                      {"data", nullptr}};
                        CollisionWireShape wire;
                        wire.body = path;
                        wire.identity = identity;
                        wire.kind = kind;
                        wire.collection = collection;
                        if (!references[i].is_null()) {
                            auto it = tags.find(references[i].get<std::uint32_t>());
                            need(it != tags.end(), "Unresolved native physics graph reference");
                            const auto &object = *it->second;
                            need(object.at("kind") == kind, "Incompatible native physics graph reference");
                            shape["data"] = copy(object);
                            for (const auto *key :
                                 {"tag", "offset", "bytes", "indexBits", "structureIndexType"})
                                shape["data"].erase(key);
                            geometry(wire, object, out.wire.bounds);
                        } else
                            wire.diagnostics.push_back("Stored native physics reference is null");
                        shape["diagnostics"] = wire.diagnostics;
                        b["shapes"][identity] = std::move(shape);
                        out.wire.shapes.push_back(std::move(wire));
                    }
                }
            }
            out.model["bodies"][path] = std::move(b);
        }
        o.control.check();
        return out;
    }
};
} // namespace
CookedPhysicsReview cooked_physics_review(const Json &snapshot, const CookedPhysicsOptions &options,
                                          std::size_t max_edges) {
    if (options.profile != CookedPhysicsProfile::unreal_5_3_windows)
        throw Error("cooked-physics-profile",
                    "Native physics review requires the explicit UE 5.3 Windows profile");
    try {
        return Review{options, max_edges}.build(snapshot);
    } catch (const Json::exception &e) {
        throw Error("cooked-physics-review",
                    std::string("Invalid native physics review snapshot: ") + e.what());
    }
}
} // namespace cyuediff
