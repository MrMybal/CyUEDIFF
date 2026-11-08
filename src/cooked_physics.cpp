#include <cyuediff/cooked_physics.hpp>
#include "archive.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
namespace cyuediff {
namespace {
void need(bool ok, const char *message, std::uint64_t offset = 0) {
    if (!ok)
        throw Error("cooked-physics-layout", message, offset);
}
void limit(bool ok, const char *message) {
    if (!ok)
        throw Error("cooked-physics-limit", message);
}
struct Budget {
    const CookedPhysicsOptions &o;
    std::uint64_t output = 0, vertices = 0, triangles = 0, topology = 0, nodes = 0;
    std::uint64_t objects = 0, refs = 0, formats = 0;
    void add(std::uint64_t &used, std::uint64_t n, std::uint64_t cap) {
        limit(used <= cap && n <= cap - used, "Cooked physics aggregate budget exceeded");
        used += n;
    }
    void reserve(std::uint64_t bytes) {
        add(output, bytes, o.max_output_bytes);
    }
};
struct Cursor {
    const std::vector<std::uint8_t> &data;
    std::uint64_t pos, end;
    Budget &budget;
    void range(std::uint64_t n) const {
        need(pos <= end && end <= data.size() && n <= end - pos, "Cooked physics field exceeds native window",
             pos);
    }
    std::uint64_t integer(unsigned n) {
        range(n);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < n; ++i)
            value |= std::uint64_t(data[std::size_t(pos++)]) << (i * 8);
        return value;
    }
    std::int32_t i32() {
        auto u = static_cast<std::uint32_t>(integer(4));
        std::int32_t value;
        std::memcpy(&value, &u, 4);
        return value;
    }
    bool boolean() {
        const auto value = integer(4);
        need(value <= 1, "Invalid cooked physics bool32", pos - 4);
        return value != 0;
    }
    double real(unsigned bytes = 4) {
        double value;
        auto bits = integer(bytes);
        if (bytes == 4) {
            auto u = static_cast<std::uint32_t>(bits);
            float f;
            std::memcpy(&f, &u, 4);
            value = f;
        } else
            std::memcpy(&value, &bits, 8);
        need(std::isfinite(value), "Non-finite cooked physics number", pos - bytes);
        return value;
    }
    std::uint32_t count(std::uint32_t cap, unsigned stride) {
        const auto value = i32();
        need(value >= 0, "Negative cooked physics array count", pos - 4);
        limit(static_cast<std::uint32_t>(value) <= cap, "Cooked physics array exceeds element budget");
        range(std::uint64_t(value) * stride);
        return static_cast<std::uint32_t>(value);
    }
    Json vector(unsigned n, unsigned bytes = 4) {
        Json out = Json::array();
        for (unsigned i = 0; i < n; ++i)
            out.push_back(real(bytes));
        return out;
    }
    Json reals(unsigned components, std::uint64_t &used, std::uint32_t cap, unsigned bytes = 4) {
        const auto n = count(cap, components * bytes);
        budget.add(used, n, cap);
        budget.reserve(std::uint64_t(n) * (64 + components * 48));
        Json out = Json::array();
        for (unsigned i = 0; i < n; ++i) {
            if ((i & 1023) == 0)
                budget.o.control.check();
            out.push_back(vector(components, bytes));
        }
        return out;
    }
    Json indices(unsigned components, unsigned bytes, std::uint64_t &used, std::uint32_t cap,
                 bool signed_values = false) {
        const auto n = count(cap, components * bytes);
        budget.add(used, n, cap);
        budget.reserve(std::uint64_t(n) * (64 + components * 48));
        Json out = Json::array();
        for (unsigned i = 0; i < n; ++i) {
            if ((i & 1023) == 0)
                budget.o.control.check();
            Json row = Json::array();
            for (unsigned j = 0; j < components; ++j) {
                auto v = integer(bytes);
                if (signed_values && (v & (1ull << (bytes * 8 - 1))))
                    row.push_back(-static_cast<std::int64_t>((1ull << (bytes * 8)) - v));
                else
                    row.push_back(v);
            }
            out.push_back(components == 1 ? std::move(row[0]) : std::move(row));
        }
        return out;
    }
    std::string hex(unsigned n) {
        range(n);
        budget.reserve(std::uint64_t(n) * 2);
        std::string out;
        constexpr char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < n; ++i) {
            auto b = integer(1);
            out.push_back(digits[b >> 4]);
            out.push_back(digits[b & 15]);
        }
        return out;
    }
};
void valid_bounds(const Json &b) {
    for (unsigned i = 0; i < 3; ++i)
        need(b[i].get<double>() <= b[i + 3].get<double>(), "Inverted cooked physics bounds");
}
void contained(const Json &vertices, const Json &bounds, const OperationControl &control) {
    if (vertices.empty())
        return;
    valid_bounds(bounds);
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        if ((i & 1023) == 0)
            control.check();
        for (unsigned k = 0; k < 3; ++k) {
            double lo = bounds[k], hi = bounds[k + 3], v = vertices[i][k];
            auto eps = std::max({1., std::abs(lo), std::abs(hi)}) * 0.00005;
            need(v >= lo - eps && v <= hi + eps, "Cooked physics vertex lies outside stored bounds");
        }
    }
}
std::size_t index(const Json &v, std::size_t size, const char *message) {
    need(v.is_number_integer() && (!v.is_number_unsigned() ? v.get<std::int64_t>() >= 0 : true), message);
    auto n = v.get<std::uint64_t>();
    need(n < size, message);
    return static_cast<std::size_t>(n);
}
void convex_topology(const Json &v, const OperationControl &control) {
    const auto &faces = v.at("faces"), &half = v.at("halfEdges"), &vertices = v.at("vertices");
    const auto &ve = v.at("vertexEdges"), &edges = v.at("uniqueEdges");
    need(faces.size() == v.at("planes").size() && ve.size() == vertices.size(),
         "Convex topology counts disagree");
    std::vector<std::size_t> next(half.size(), SIZE_MAX);
    for (std::size_t f = 0; f < faces.size(); ++f) {
        control.check();
        auto first = faces[f][0].get<std::int64_t>(), count = faces[f][1].get<std::int64_t>();
        need(first >= 0 && count >= 3 && std::uint64_t(first) <= half.size() &&
                 std::uint64_t(count) <= half.size() - std::size_t(first),
             "Invalid convex face edge range");
        for (std::size_t j = 0; j < std::size_t(count); ++j) {
            auto h = std::size_t(first) + j;
            need(next[h] == SIZE_MAX && half[h][0] == f, "Overlapping convex face or wrong plane index");
            next[h] = std::size_t(first) + (j + 1) % std::size_t(count);
        }
    }
    for (std::size_t h = 0; h < half.size(); ++h) {
        if ((h & 1023) == 0)
            control.check();
        need(next[h] != SIZE_MAX, "Unowned convex half edge");
        index(half[h][1], vertices.size(), "Convex half-edge vertex exceeds table");
        auto twin = index(half[h][2], half.size(), "Convex twin edge exceeds table");
        need(twin != h && half[twin][2] == h && next[twin] != SIZE_MAX, "Non-reciprocal convex twin edge");
        need(half[h][1] == half[next[twin]][1] && half[next[h]][1] == half[twin][1],
             "Convex twin endpoints disagree");
    }
    for (std::size_t i = 0; i < ve.size(); ++i) {
        if ((i & 1023) == 0)
            control.check();
        auto h = index(ve[i], half.size(), "Convex vertex edge exceeds table");
        need(half[h][1] == i, "Convex vertex edge has wrong origin");
    }
    std::set<std::size_t> unique;
    for (const auto &e : edges) {
        control.check();
        auto h = index(e, half.size(), "Convex unique edge exceeds table");
        auto t = half[h][2].get<std::size_t>();
        need(unique.insert(std::min(h, t)).second, "Duplicate convex unique edge");
    }
    // The engine rebuilds an omitted edge list; retain that fact instead of inventing stored indices.
    need(edges.empty() || edges.size() * 2 == half.size(), "Incomplete convex unique edge list");
}
Json base(Cursor &r, unsigned expected) {
    const auto type = r.integer(1);
    need(type == expected, "Unsupported Chaos factory object type", r.pos - 1);
    auto convex = r.boolean(), collide = r.boolean();
    auto collision = r.integer(1);
    need(collision <= 17, "Unsupported Chaos collision type", r.pos - 1);
    return {{"type", type},
            {"kind", expected == 8 ? "convex" : "triangleMesh"},
            {"isConvex", convex},
            {"collide", collide},
            {"collisionType", collision}};
}
Json convex(Cursor &r) {
    auto out = base(r, 8);
    out["planes"] = r.reals(6, r.budget.topology, r.budget.o.max_topology);
    out["vertices"] = r.reals(3, r.budget.vertices, r.budget.o.max_vertices);
    out["bounds"] = r.vector(6);
    out["volume"] = r.real();
    out["centerOfMass"] = r.vector(3);
    out["margin"] = r.real();
    need(out["volume"].get<double>() >= 0 && out["margin"].get<double>() >= 0,
         "Negative convex volume/margin");
    const auto type = r.integer(1);
    need(type <= 3, "Unknown convex topology index type", r.pos - 1);
    out["structureIndexType"] = type;
    if (type) {
        const unsigned bytes = type == 1 ? 1 : type == 2 ? 2 : 4;
        const auto cap = r.budget.o.max_topology;
        out["faces"] = r.indices(2, bytes, r.budget.topology, cap, type != 1);
        out["halfEdges"] = r.indices(3, bytes, r.budget.topology, cap, type != 1);
        out["vertexEdges"] = r.indices(1, bytes, r.budget.topology, cap, type != 1);
        out["uniqueEdges"] = r.indices(1, bytes, r.budget.topology, cap, type != 1);
        convex_topology(out, r.budget.o.control);
    }
    // Chaos TVector<double>'s FArchive overload stores f32; FRotation3 stores f64.
    out["unitMassInertiaTensor"] = r.vector(3);
    out["rotationOfMass"] = r.vector(4, 8);
    for (const auto &v : out["unitMassInertiaTensor"])
        need(v.get<double>() >= 0, "Negative convex inertia");
    contained(out["vertices"], out["bounds"], r.budget.o.control);
    return out;
}
void bvh_check(const Json &nodes, std::size_t faces, const OperationControl &control) {
    if (nodes.empty())
        return;
    std::vector<unsigned char> state(nodes.size());
    std::vector<std::pair<std::size_t, bool>> stack{{0, false}};
    while (!stack.empty()) {
        control.check();
        auto [i, done] = stack.back();
        stack.pop_back();
        if (done) {
            state[i] = 2;
            continue;
        }
        need(state[i] != 1, "Cyclic cooked physics BVH");
        if (state[i] == 2)
            continue;
        state[i] = 1;
        stack.emplace_back(i, true);
        for (const auto &child : nodes[i]) {
            auto at = child["index"].get<std::int64_t>(), count = child["faces"].get<std::int64_t>();
            need(count >= 0 && at >= -1, "Invalid cooked physics BVH child");
            if (count)
                need(at >= 0 && std::uint64_t(at) <= faces && std::uint64_t(count) <= faces - std::size_t(at),
                     "BVH face range exceeds table");
            else if (at >= 0) {
                need(std::uint64_t(at) < nodes.size(), "BVH child node exceeds table");
                stack.emplace_back(std::size_t(at), false);
            }
            if (count || at >= 0)
                valid_bounds(child["bounds"]);
        }
    }
    need(std::all_of(state.begin(), state.end(), [](auto n) { return n == 2; }),
         "Unreachable cooked physics BVH node");
}
Json trimesh(Cursor &r) {
    auto out = base(r, 11);
    const auto owns_particles = r.boolean();
    need(owns_particles, "Shared Chaos particle views are unsupported", r.pos - 4);
    out["vertices"] = r.reals(3, r.budget.vertices, r.budget.o.max_vertices);
    const auto wide = r.boolean();
    out["indexBits"] = wide ? 32 : 16;
    out["triangles"] = r.indices(3, wide ? 4 : 2, r.budget.triangles, r.budget.o.max_triangles, wide);
    for (const auto &t : out["triangles"]) {
        r.budget.o.control.check();
        for (const auto &i : t)
            index(i, out["vertices"].size(), "Physics triangle vertex exceeds table");
    }
    out["bounds"] = r.vector(6);
    const auto nodes = r.count(r.budget.o.max_bvh_nodes, 64);
    r.budget.add(r.budget.nodes, nodes, r.budget.o.max_bvh_nodes);
    r.budget.reserve(std::uint64_t(nodes) * 2048);
    out["bvhNodes"] = Json::array();
    for (unsigned i = 0; i < nodes; ++i) {
        if ((i & 1023) == 0)
            r.budget.o.control.check();
        Json children = Json::array();
        for (unsigned j = 0; j < 2; ++j) {
            auto b = r.vector(6);
            auto at = r.i32(), count = r.i32();
            children.push_back({{"bounds", std::move(b)}, {"index", at}, {"faces", count}});
        }
        out["bvhNodes"].push_back(std::move(children));
    }
    out["faceBounds"] = r.reals(6, r.budget.topology, r.budget.o.max_topology);
    out["materials"] = r.indices(1, 2, r.budget.topology, r.budget.o.max_topology);
    out["externalFaces"] = r.indices(1, 4, r.budget.topology, r.budget.o.max_topology, true);
    out["externalVertices"] = r.indices(1, 4, r.budget.topology, r.budget.o.max_topology, true);
    need(out["faceBounds"].size() == out["triangles"].size(),
         "Physics BVH face count disagrees with triangles");
    need(out["materials"].empty() || out["materials"].size() == out["triangles"].size(),
         "Physics material count disagrees with triangles");
    for (const auto &b : out["faceBounds"]) {
        r.budget.o.control.check();
        valid_bounds(b);
    }
    bvh_check(out["bvhNodes"], out["faceBounds"].size(), r.budget.o.control);
    contained(out["vertices"], out["bounds"], r.budget.o.control);
    return out;
}
Json decode(Cursor &r) {
    const auto begin = r.pos;
    const auto precision = r.integer(4);
    need(precision == 4, "Chaos cooker precision must be float32", r.pos - 4);
    Json objects = Json::array();
    std::map<std::uint32_t, std::size_t> tags;
    auto refs = [&](unsigned type) {
        const auto n = r.count(r.budget.o.max_references, 4);
        r.budget.add(r.budget.refs, n, r.budget.o.max_references);
        r.budget.reserve(std::uint64_t(n) * 64);
        Json result = Json::array();
        for (unsigned i = 0; i < n; ++i) {
            r.budget.o.control.check();
            if (!r.boolean()) {
                result.push_back(nullptr);
                continue;
            }
            const auto tag = r.i32();
            need(tag >= 0, "Negative Chaos graph tag", r.pos - 4);
            auto found = tags.find(static_cast<std::uint32_t>(tag));
            if (found == tags.end()) {
                r.budget.add(r.budget.objects, 1, r.budget.o.max_objects);
                r.budget.reserve(8192);
                const auto start = r.pos;
                auto object = type == 8 ? convex(r) : trimesh(r);
                object["tag"] = tag;
                object["offset"] = start - begin;
                object["bytes"] = r.pos - start;
                found = tags.emplace(static_cast<std::uint32_t>(tag), objects.size()).first;
                objects.push_back(std::move(object));
            }
            need(objects[found->second]["type"] == type, "Chaos graph tag aliases incompatible object types");
            result.push_back(tag);
        }
        return result;
    };
    auto convex_refs = refs(8), tri_refs = refs(11);
    auto uv_indices = r.indices(1, 4, r.budget.topology, r.budget.o.max_topology, true);
    auto uv_positions = r.reals(3, r.budget.vertices, r.budget.o.max_vertices, 8);
    const auto channels = r.count(r.budget.o.max_uv_channels, 4);
    r.budget.reserve(std::uint64_t(channels) * 64);
    Json uv = Json::array();
    for (unsigned i = 0; i < channels; ++i) {
        auto values = r.reals(2, r.budget.topology, r.budget.o.max_topology, 8);
        need(values.size() == uv_positions.size(), "Physics UV channel vertex count disagrees");
        uv.push_back(std::move(values));
    }
    need(uv_indices.size() % 3 == 0, "Physics UV index count is not triangular");
    for (const auto &i : uv_indices) {
        r.budget.o.control.check();
        index(i, uv_positions.size(), "Physics UV index exceeds vertices");
    }
    auto remap = r.indices(1, 4, r.budget.topology, r.budget.o.max_topology, true);
    need(r.pos == r.end, "Trailing bytes in selected Chaos cooker stream", r.pos);
    return {{"schema", "cyuediff.chaos-physics/1"},
            {"profile", "ue5.3-windows"},
            {"precisionBytes", 4},
            {"bytes", r.pos - begin},
            {"sha256", detail::hash_range(r.data, begin, r.pos - begin, r.budget.o.control)},
            {"objects", std::move(objects)},
            {"convexRefs", std::move(convex_refs)},
            {"triangleMeshRefs", std::move(tri_refs)},
            {"uvIndices", std::move(uv_indices)},
            {"uvPositions", std::move(uv_positions)},
            {"uvChannels", std::move(uv)},
            {"faceRemap", std::move(remap)}};
}
void profile(const CookedPhysicsOptions &options) {
    options.control.check();
    if (options.profile != CookedPhysicsProfile::unreal_5_3_windows)
        throw Error("cooked-physics-profile",
                    "Cooked physics requires the explicit UE 5.3 Windows Chaos profile");
}
} // namespace
Json read_chaos_physics(const std::vector<std::uint8_t> &bytes, const CookedPhysicsOptions &options) {
    profile(options);
    limit(bytes.size() <= options.max_read_bytes, "Chaos stream exceeds read budget");
    Budget budget{options};
    budget.reserve(4096);
    Cursor r{bytes, 0, bytes.size(), budget};
    auto out = decode(r);
    options.control.check();
    return out;
}
Json read_zen_cooked_physics(const Container &container, std::size_t entry, const ZenReadOptions &zen,
                             const ContainerReadOptions &read, const CookedPhysicsOptions &supplied) {
    auto options = supplied;
    options.control.cancelled = [a = supplied.control.cancelled, b = zen.control.cancelled,
                                 c = read.control.cancelled] {
        return (a && a()) || (b && b()) || (c && c());
    };
    if (!options.control.progress)
        options.control.progress = zen.control.progress ? zen.control.progress : read.control.progress;
    profile(options);
    if (zen.profile != ZenProfile::unreal_5_3)
        throw Error("cooked-physics-profile",
                    "Cooked physics requires the matching explicit Zen UE 5.3 profile");
    need(entry < container.entries().size(), "Invalid cooked physics entry");
    const auto &member = container.entries()[entry];
    need(member.chunk_type == 1 && member.id.size() == 24,
         "Cooked physics requires a Zen export-bundle chunk");
    limit(member.size <= options.max_read_bytes, "Cooked physics package exceeds read budget");
    auto reads = read;
    reads.control = options.control;
    const auto bytes = container.read(entry, reads);
    auto zopts = zen;
    zopts.control = options.control;
    const auto package = ZenPackage::parse(bytes, zopts);
    if ((package.flags & 0x80000200u) != 0x80000200u)
        throw Error("cooked-physics-profile", "Cooked physics requires an editor-stripped cooked package");
    Budget budget{options};
    budget.reserve(8192);
    for (const auto *text :
         {&package.name, &package.sha256, &package.schema_context_sha256, &package.script_objects_sha256})
        budget.reserve(text->size());
    std::uint64_t read_bytes = bytes.size();
    std::map<std::string, std::vector<std::uint8_t>> chunks;
    auto find_chunk = [&](const std::string &id) {
        std::size_t found = SIZE_MAX;
        for (std::size_t i = 0; i < container.entries().size(); ++i) {
            if ((i & 4095) == 0)
                options.control.check();
            const auto &e = container.entries()[i];
            if (e.id == id && !e.deleted) {
                need(found == SIZE_MAX, "Ambiguous cooked physics bulk chunk");
                found = i;
            }
        }
        return found;
    };
    Json bodies = Json::array();
    bool known = std::all_of(package.exports.begin(), package.exports.end(), [](const auto &e) {
        return !e.class_name.empty() && e.class_name.front() != '$';
    });
    bool complete = known;
    for (std::size_t i = 0; i < package.exports.size(); ++i) {
        options.control.checkpoint("cooked-physics-body", i, package.exports.size());
        const auto &object = package.exports[i];
        if ((object.flags & 0x30) || (object.class_name != "/Script/Engine.BodySetup" &&
                                      object.class_name != "/Script/Engine.SkeletalBodySetup"))
            continue;
        limit(bodies.size() < options.max_bodies, "Cooked physics body count exceeds budget");
        budget.reserve(8192 + object.path.size() + object.class_name.size());
        if (!object.property_read.is_object() ||
            object.property_read.value("status", std::string{}) != "decoded")
            throw Error("cooked-physics-context",
                        "Cooked BodySetup requires a successfully decoded explicit property boundary");
        const auto offset = object.property_read.at("offset").get<std::uint64_t>();
        const auto consumed = object.property_read.at("consumedBytes").get<std::uint64_t>();
        need(offset <= object.size && consumed <= object.size - offset,
             "Invalid cooked physics property boundary");
        const auto native = object.offset + offset + consumed;
        Cursor r{bytes, native, object.offset + object.size, budget};
        auto guid = r.boolean() ? Json(r.hex(16)) : Json(nullptr);
        auto body_guid = r.hex(16);
        const auto cooked = r.boolean();
        need(cooked, "BodySetup native physics is not cooked", r.pos - 4);
        const auto has = r.boolean();
        const auto formats = r.count(options.max_formats, 12);
        budget.add(budget.formats, formats, options.max_formats);
        Json body = {{"object", object.path},      {"class", object.class_name},    {"exportIndex", i},
                     {"nativeOffset", native},     {"nativeBytes", r.end - native}, {"objectGuidBytes", guid},
                     {"bodyGuidBytes", body_guid}, {"hasCookedCollisionData", has}, {"complete", true},
                     {"formats", Json::array()}};
        std::set<std::pair<std::uint32_t, std::uint32_t>> format_names;
        for (unsigned f = 0; f < formats; ++f) {
            options.control.check();
            budget.reserve(4096);
            const auto name = static_cast<std::uint32_t>(r.integer(4)),
                       number = static_cast<std::uint32_t>(r.integer(4));
            need(name < package.names.size(), "Cooked physics format name exceeds package table", r.pos - 8);
            need(format_names.emplace(name, number).second, "Duplicate cooked physics format name",
                 r.pos - 8);
            const auto bulk_index = r.integer(4);
            need(bulk_index < package.bulk_data.size(), "Cooked physics bulk-map index exceeds table",
                 r.pos - 4);
            const auto &bulk = package.bulk_data[std::size_t(bulk_index)];
            const auto flags = bulk.at("flags").get<std::uint32_t>();
            const auto size = bulk.at("size").get<std::uint64_t>();
            if (flags & ~0x1ffffu || (flags & ((1u << 1) | (1u << 9) | (1u << 15))))
                throw Error("cooked-physics-profile",
                            "Unsupported cooked physics bulk flags or legacy compression");
            const bool external = flags & 1, unused = flags & (1u << 5);
            std::uint64_t at = 0;
            auto chunk_id = member.id;
            std::string unavailable;
            if (unused)
                unavailable = "unused";
            else if (!external) {
                need(!(flags & ((1u << 8) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 14))),
                     "Conflicting inline cooked physics bulk flags");
                at = r.pos;
                r.range(size);
                r.pos += size;
            } else {
                auto off = bulk.at("offset").get<std::int64_t>();
                need(off >= 0, "Negative cooked physics external bulk offset");
                at = std::uint64_t(off);
                unsigned type = flags & (1u << 11) ? 3 : 2;
                if (flags & (1u << 14)) {
                    auto optional = member.id;
                    optional.replace(22, 2, "03");
                    if (find_chunk(optional) != SIZE_MAX) {
                        type = 3;
                        off = bulk.at("duplicateOffset").get<std::int64_t>();
                        need(off >= 0, "Negative cooked physics duplicate offset");
                        at = std::uint64_t(off);
                    }
                } else if (flags & (1u << 12))
                    type = 4;
                chunk_id.replace(22, 2, type == 2 ? "02" : type == 3 ? "03" : "04");
            }
            budget.reserve(package.names[name].size());
            Json format = {{"name", package.names[name]},
                           {"number", number},
                           {"bulkIndex", bulk_index},
                           {"flags", flags},
                           {"bytes", size},
                           {"offset", unused ? Json(nullptr) : Json(at)},
                           {"declaredOffset", bulk.at("offset")},
                           {"chunkId", chunk_id},
                           {"storage", unused     ? "unused"
                                       : external ? "external"
                                                  : "inline"}};
            const auto *data = &bytes;
            auto source_hash = member.source_hash;
            if (!unused && !size)
                unavailable = "empty-payload";
            if (external && unavailable.empty()) {
                const auto selected = find_chunk(chunk_id);
                if (selected == SIZE_MAX)
                    unavailable = "missing-chunk";
                else {
                    const auto &chunk = container.entries()[selected];
                    need(at <= chunk.size && size <= chunk.size - at,
                         "Cooked physics range exceeds external chunk");
                    source_hash = chunk.source_hash;
                    auto found = chunks.find(chunk_id);
                    if (found == chunks.end()) {
                        limit(chunk.size <= options.max_read_bytes - read_bytes,
                              "Verified cooked physics chunks exceed aggregate read budget");
                        read_bytes += chunk.size;
                        found = chunks.emplace(chunk_id, container.read(selected, reads)).first;
                    }
                    data = &found->second;
                }
            }
            if (unavailable.empty()) {
                Cursor payload{*data, at, at + size, budget};
                payload.range(size);
                format["physics"] = decode(payload);
                format["chunkSourceHash"] = source_hash;
                format["status"] = "decoded";
            } else {
                format["status"] = unavailable;
                body["complete"] = false;
                complete = false;
            }
            body["formats"].push_back(std::move(format));
        }
        need(r.pos == r.end, "Trailing BodySetup native bytes for selected profile", r.pos);
        if (has && !formats) {
            body["complete"] = false;
            complete = false;
        }
        bodies.push_back(std::move(body));
    }
    options.control.check();
    return {{"schema", "cyuediff.cooked-physics/1"},
            {"profile", "ue5.3-windows"},
            {"scope",
             "stored native Chaos cooker geometry in cooker coordinates; no defaults or owner transforms"},
            {"complete", complete},
            {"bodyTableKnown", known},
            {"bodies", std::move(bodies)},
            {"provenance",
             {{"package", package.name},
              {"packageSha256", package.sha256},
              {"schemaContextSha256", package.schema_context_sha256},
              {"scriptObjectsSha256", package.script_objects_sha256},
              {"mappingVerified", false}}}};
}
namespace {
struct PhysicsComparison {
    const CookedPhysicsOptions &options;
    Budget budget{options};
    Json changes = Json::array(), unresolved = Json::array();
    Json copy(const Json &v, unsigned depth = 0) {
        limit(depth <= 32, "Physics comparison depth exceeds limit");
        options.control.check();
        budget.reserve(64);
        if (v.is_array()) {
            Json a = Json::array();
            for (const auto &x : v)
                a.push_back(copy(x, depth + 1));
            return a;
        }
        if (v.is_object()) {
            Json a = Json::object();
            for (auto i = v.begin(); i != v.end(); ++i) {
                budget.reserve(i.key().size());
                a[i.key()] = copy(i.value(), depth + 1);
            }
            return a;
        }
        if (v.is_string())
            budget.reserve(v.get_ref<const std::string &>().size());
        need(!v.is_number_float() || std::isfinite(v.get<double>()), "Non-finite physics comparison input");
        return v;
    }
    void change(const std::string &path, const Json *a, const Json *b) {
        limit(changes.size() < options.max_changes, "Physics comparison change count exceeds budget");
        budget.reserve(512 + path.size());
        Json c = {{"path", path}, {"kind", a ? (b ? "modified" : "removed") : "added"}};
        if (a)
            c["before"] = copy(*a);
        if (b)
            c["after"] = copy(*b);
        changes.push_back(std::move(c));
    }
    static std::string token(const std::string &key) {
        std::string out;
        for (char c : key) {
            if (c == '~')
                out += "~0";
            else if (c == '/')
                out += "~1";
            else
                out += c;
        }
        return out;
    }
    void diff(const Json *a, const Json *b, const std::string &path, unsigned depth = 0) {
        limit(depth <= 32, "Physics comparison depth exceeds limit");
        options.control.check();
        budget.reserve(32);
        if (!a || !b) {
            change(path, a, b);
            return;
        }
        if (a->is_object() && b->is_object()) {
            for (auto i = a->begin(); i != a->end(); ++i) {
                auto j = b->find(i.key());
                diff(&i.value(), j == b->end() ? nullptr : &j.value(), path + "/" + token(i.key()),
                     depth + 1);
            }
            for (auto i = b->begin(); i != b->end(); ++i)
                if (!a->contains(i.key()))
                    diff(nullptr, &i.value(), path + "/" + token(i.key()), depth + 1);
        } else if (a->is_array() && b->is_array()) {
            auto n = std::max(a->size(), b->size());
            for (std::size_t i = 0; i < n; ++i)
                diff(i < a->size() ? &(*a)[i] : nullptr, i < b->size() ? &(*b)[i] : nullptr,
                     path + "/" + std::to_string(i), depth + 1);
        } else if (*a != *b)
            change(path, a, b);
    }
    Json semantic(const Json &physics) {
        need(physics.is_object() && physics.value("schema", std::string{}) == "cyuediff.chaos-physics/1",
             "Invalid Chaos physics comparison schema");
        auto out = copy(physics);
        out.erase("sha256");
        out.erase("bytes");
        need(out.at("objects").is_array(), "Invalid Chaos physics object table");
        std::map<std::uint32_t, std::size_t> tags;
        for (std::size_t i = 0; i < out["objects"].size(); ++i) {
            auto &o = out["objects"][i];
            need(o.is_object(), "Invalid Chaos physics object");
            auto tag = o.at("tag").get<std::uint32_t>();
            need(tags.emplace(tag, i).second, "Duplicate Chaos physics comparison tag");
            for (auto k : {"tag", "offset", "bytes", "indexBits", "structureIndexType"})
                o.erase(k);
        }
        for (auto key : {"convexRefs", "triangleMeshRefs"}) {
            need(out.at(key).is_array(), "Invalid Chaos physics references");
            for (auto &ref : out[key])
                if (!ref.is_null()) {
                    auto it = tags.find(ref.get<std::uint32_t>());
                    need(it != tags.end(), "Unresolved Chaos physics comparison tag");
                    ref = it->second;
                }
        }
        return out;
    }
    Json bodies(const Json &snapshot) {
        need(snapshot.is_object() && snapshot.value("schema", std::string{}) == "cyuediff.cooked-physics/1",
             "Invalid cooked physics comparison snapshot");
        need(snapshot.at("bodies").is_array(), "Invalid physics body table");
        limit(snapshot["bodies"].size() <= options.max_bodies,
              "Physics comparison body count exceeds budget");
        Json out = Json::object();
        for (const auto &body : snapshot["bodies"]) {
            options.control.check();
            const auto name = body.at("object").get<std::string>();
            budget.reserve(4096 + name.size());
            need(!out.contains(name), "Duplicate physics comparison body identity");
            Json b = {{"class", copy(body.at("class"))},
                      {"hasCookedCollisionData", copy(body.at("hasCookedCollisionData"))},
                      {"formats", Json::object()}};
            need(body.at("formats").is_array(), "Invalid physics formats");
            limit(body["formats"].size() <= options.max_formats,
                  "Physics comparison format count exceeds budget");
            for (const auto &f : body["formats"]) {
                auto id = f.at("name").get<std::string>() + "#" +
                          std::to_string(f.at("number").get<std::uint32_t>());
                budget.reserve(512 + id.size());
                need(!b["formats"].contains(id), "Duplicate physics comparison format identity");
                if (f.at("status") == "decoded")
                    b["formats"][id] = semantic(f.at("physics"));
                else
                    b["formats"][id] = {{"unavailable", copy(f.at("status"))}};
            }
            out[name] = std::move(b);
        }
        return out;
    }
};
} // namespace
Json compare_cooked_physics(const Json &before, const Json &after, const CookedPhysicsOptions &options) {
    profile(options);
    PhysicsComparison c{options};
    auto a = c.bodies(before), b = c.bodies(after);
    bool complete = before.at("complete").get<bool>() && after.at("complete").get<bool>();
    for (auto i = a.begin(); i != a.end(); ++i) {
        auto j = b.find(i.key());
        auto path = "/bodies/" + PhysicsComparison::token(i.key());
        if (j == b.end()) {
            if (after.at("bodyTableKnown").get<bool>())
                c.change(path, &i.value(), nullptr);
            else {
                c.budget.reserve(path.size() + 128);
                c.unresolved.push_back(path);
            }
            continue;
        }
        for (const auto &key : {"class", "hasCookedCollisionData"})
            c.diff(&i->at(key), &j->at(key), path + "/" + key);
        auto &af = i->at("formats"), &bf = j->at("formats");
        for (auto f = af.begin(); f != af.end(); ++f) {
            auto g = bf.find(f.key());
            auto fp = path + "/formats/" + PhysicsComparison::token(f.key());
            if (f->contains("unavailable") || (g != bf.end() && g->contains("unavailable"))) {
                c.budget.reserve(fp.size() + 128);
                c.unresolved.push_back(fp);
                complete = false;
            } else
                c.diff(&f.value(), g == bf.end() ? nullptr : &g.value(), fp);
        }
        for (auto f = bf.begin(); f != bf.end(); ++f)
            if (!af.contains(f.key())) {
                auto fp = path + "/formats/" + PhysicsComparison::token(f.key());
                if (f->contains("unavailable")) {
                    c.budget.reserve(fp.size() + 128);
                    c.unresolved.push_back(fp);
                    complete = false;
                } else
                    c.change(fp, nullptr, &f.value());
            }
    }
    for (auto i = b.begin(); i != b.end(); ++i)
        if (!a.contains(i.key())) {
            auto path = "/bodies/" + PhysicsComparison::token(i.key());
            if (before.at("bodyTableKnown").get<bool>())
                c.change(path, nullptr, &i.value());
            else {
                c.budget.reserve(path.size() + 128);
                c.unresolved.push_back(path);
            }
        }
    options.control.check();
    return {{"schema", "cyuediff.cooked-physics-diff/1"},
            {"complete", complete},
            {"status", complete ? (c.changes.empty() ? "unchanged" : "changed") : "incomplete"},
            {"changes", std::move(c.changes)},
            {"unresolved", std::move(c.unresolved)}};
}
} // namespace cyuediff
