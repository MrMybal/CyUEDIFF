#include <cyuediff/cooked_mesh.hpp>
#include "archive.hpp"
#include "zen_bulk.hpp"
#include "cooked_nanite_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
namespace cyuediff {
namespace {
void need(bool b, const char *s, std::uint64_t p = 0) {
    if (!b)
        throw Error("cooked-mesh-layout", s, p);
}
void limit(bool b, const char *s) {
    if (!b)
        throw Error("cooked-mesh-limit", s);
}
struct Cursor {
    const std::vector<std::uint8_t> &bytes;
    std::uint64_t pos, end;
    void range(std::uint64_t n) const {
        need(pos <= end && n <= end - pos, "Cooked mesh field exceeds its native window", pos);
    }
    std::uint64_t number(unsigned n) {
        range(n);
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= std::uint64_t(bytes[std::size_t(pos++)]) << (8 * i);
        return v;
    }
    std::uint32_t u32() {
        return static_cast<std::uint32_t>(number(4));
    }
    std::int32_t i32() {
        auto v = u32();
        std::int32_t s;
        std::memcpy(&s, &v, 4);
        return s;
    }
    bool boolean() {
        auto v = u32();
        need(v <= 1, "Invalid cooked mesh bool32", pos - 4);
        return v != 0;
    }
    float f32() {
        auto v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        need(std::isfinite(f), "Non-finite cooked mesh float", pos - 4);
        return f;
    }
    float half() {
        const auto bits = number(2);
        const auto sign = bits & 0x8000 ? -1.f : 1.f;
        const auto exp = (bits >> 10) & 31, mant = bits & 1023;
        need(exp != 31, "Non-finite cooked mesh half", pos - 2);
        return sign * std::ldexp(static_cast<float>(exp ? mant + 1024 : mant),
                                 exp ? static_cast<int>(exp) - 25 : -24);
    }
    std::uint32_t count(std::uint32_t max) {
        auto n = i32();
        need(n >= 0, "Negative cooked mesh array count", pos - 4);
        limit(static_cast<std::uint32_t>(n) <= max, "Cooked mesh array exceeds element budget");
        return n;
    }
    void strip(unsigned classes = 0) {
        auto global = number(1), c = number(1);
        need(global == 1 && c == classes, "Unsupported cooked mesh buffer strip flags", pos - 2);
    }
    std::uint32_t array(unsigned stride, std::uint32_t max) {
        need(u32() == stride, "Cooked mesh bulk-array stride disagrees with profile", pos - 4);
        auto n = count(max);
        range(std::uint64_t(n) * stride);
        return n;
    }
};
struct Budget {
    const CookedMeshOptions &o;
    std::uint64_t vertices = 0, indices = 0, triangles = 0, output = 0, sections = 0;
    void add(std::uint64_t &counter, std::uint64_t n, std::uint64_t max, const char *s) {
        limit(counter <= max && n <= max - counter, s);
        counter += n;
    }
    void reserve(std::uint64_t n) {
        add(output, n, o.max_output_bytes, "Cooked mesh decoded output exceeds byte budget");
    }
};
struct Sizes {
    std::uint64_t all = 0, depth = 0, reversed = 0;
};
struct VertexMeta {
    std::uint32_t uvs = 0, vertices = 0, stride = 0, positions = 0, color_stride = 0, colors = 0;
    bool full_uv = false, high_tangent = false;
    Json json() const {
        return {{"uvChannels", uvs},           {"vertices", vertices},
                {"positionStride", stride},    {"positionVertices", positions},
                {"colorStride", color_stride}, {"colorVertices", colors},
                {"fullPrecisionUV", full_uv},  {"highPrecisionTangents", high_tangent}};
    }
};
struct IndexMeta {
    std::int32_t count = 0;
    bool wide = false;
    Json json() const {
        return {{"count", count}, {"bits", wide ? 32 : 16}};
    }
};
struct Availability {
    std::uint32_t depth_triangles = 0, flags = 0;
    VertexMeta v;
    std::array<IndexMeta, 5> indices{};
    Json json() const {
        Json a = Json::array();
        for (const auto &i : indices)
            a.push_back(i.json());
        return {
            {"depthTriangles", depth_triangles}, {"flags", flags}, {"vertices", v.json()}, {"indices", a}};
    }
};
Availability availability(Cursor &r, const CookedMeshOptions &o) {
    Availability a;
    a.depth_triangles = r.u32();
    a.flags = r.u32();
    need(!(a.flags & ~127u), "Unknown cooked mesh availability flags");
    a.v.uvs = r.u32();
    a.v.vertices = r.u32();
    a.v.full_uv = r.boolean();
    a.v.high_tangent = r.boolean();
    a.v.stride = r.u32();
    a.v.positions = r.u32();
    a.v.color_stride = r.u32();
    a.v.colors = r.u32();
    limit(a.v.uvs <= o.max_uv_channels && a.v.vertices <= o.max_vertices && a.v.positions <= o.max_vertices &&
              a.v.colors <= o.max_vertices,
          "Cooked mesh availability exceeds vertex budget");
    need(a.v.stride == 12 && a.v.vertices == a.v.positions &&
             (!a.v.colors || (a.v.color_stride == 4 && a.v.colors == a.v.vertices)),
         "Invalid cooked mesh availability dimensions");
    for (auto &i : a.indices) {
        i.count = r.i32();
        i.wide = r.boolean();
        need(i.count >= -1, "Invalid cached cooked mesh index count");
        limit(i.count < 0 || static_cast<std::uint32_t>(i.count) <= o.max_indices,
              "Cooked mesh availability exceeds index budget");
    }
    limit(a.depth_triangles <= o.max_triangles, "Cooked mesh availability exceeds triangle budget");
    return a;
}
Sizes stored_sizes(Cursor &r) {
    return {r.u32(), r.u32(), r.u32()};
}
void match_sizes(const Sizes &actual, const Sizes &stored) {
    need(actual.all == stored.all && actual.depth == stored.depth && actual.reversed == stored.reversed,
         "Cooked mesh serialized buffer sizes disagree with payload");
}
std::vector<std::uint32_t> index_buffer(Cursor &r, std::uint32_t vertices, Budget &b, IndexMeta &meta,
                                        std::uint64_t &size) {
    meta.wide = r.boolean();
    auto bytes = r.array(1, std::numeric_limits<std::int32_t>::max());
    const unsigned stride = meta.wide ? 4 : 2;
    need(bytes % stride == 0, "Cooked mesh index byte count is not aligned");
    const auto n = bytes / stride;
    b.add(b.indices, n, b.o.max_indices, "Cooked mesh index buffers exceed aggregate index budget");
    b.reserve(std::uint64_t(n) * 4);
    meta.count = static_cast<std::int32_t>(n);
    std::vector<std::uint32_t> out;
    out.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 4095) == 0)
            b.o.control.check();
        auto v = static_cast<std::uint32_t>(r.number(stride));
        need(v < vertices, "Cooked mesh index references an absent vertex", r.pos - stride);
        out.push_back(v);
    }
    need(!r.boolean(), "Windows cooked mesh index-expansion flag requires another profile");
    size = bytes;
    return out;
}
float packed(Cursor &r, bool high) {
    const auto bits = static_cast<std::uint32_t>(r.number(high ? 2 : 1));
    const auto max = high ? 65536 : 256;
    const auto signed_value = bits & (max / 2) ? static_cast<int>(bits) - max : static_cast<int>(bits);
    return static_cast<float>(signed_value) / (high ? 32767.f : 127.f);
}
Sizes buffers(Cursor &r, CookedMeshLod &lod, Budget &b, const Availability *expected, const Json &materials) {
    const auto begin = r.pos;
    r.strip(lod.strip_flags);
    VertexMeta meta;
    meta.stride = r.u32();
    meta.positions = r.u32();
    need(meta.stride == 12, "Cooked mesh position stride is not float3");
    b.add(b.vertices, meta.positions, b.o.max_vertices, "Cooked mesh LODs exceed aggregate vertex budget");
    auto n = r.array(12, b.o.max_vertices);
    need(n == meta.positions, "Cooked mesh position counts disagree");
    Sizes sizes;
    sizes.all = std::uint64_t(n) * 12;
    std::shared_ptr<MeshGeometry> geometry;
    if (b.o.geometry) {
        b.reserve(std::uint64_t(n) * (sizeof(MeshVertex) + sizeof(MeshInstance)));
        geometry = std::make_shared<MeshGeometry>();
        geometry->vertices.resize(n);
        geometry->instances.resize(n);
    }
    std::array<float, 3> low{}, high{};
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 4095) == 0)
            b.o.control.checkpoint("cooked-mesh-vertices", i, n);
        std::array<float, 3> point{r.f32(), r.f32(), r.f32()};
        if (!i)
            low = high = point;
        for (unsigned axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], point[axis]);
            high[axis] = std::max(high[axis], point[axis]);
        }
        if (geometry) {
            geometry->vertices[i] = {i, point};
            geometry->instances[i].id = i;
            geometry->instances[i].vertex = i;
        }
    }
    r.strip();
    meta.uvs = r.u32();
    meta.vertices = r.u32();
    meta.full_uv = r.boolean();
    meta.high_tangent = r.boolean();
    limit(meta.uvs <= b.o.max_uv_channels, "Too many cooked mesh UV channels");
    need(meta.vertices == n, "Cooked mesh tangent vertices disagree with positions");
    const unsigned tangent_stride = meta.high_tangent ? 16 : 8;
    need(r.array(tangent_stride, b.o.max_vertices) == n, "Cooked mesh tangent count disagrees with vertices");
    sizes.all += std::uint64_t(n) * tangent_stride;
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 4095) == 0)
            b.o.control.check();
        std::array<float, 4> x, z;
        for (auto &v : x)
            v = packed(r, meta.high_tangent);
        for (auto &v : z)
            v = packed(r, meta.high_tangent);
        if (geometry) {
            geometry->instances[i].tangent = {x[0], x[1], x[2]};
            geometry->instances[i].normal = {z[0], z[1], z[2]};
            geometry->instances[i].binormal_sign = z[3];
        }
    }
    const unsigned uv_stride = meta.full_uv ? 8 : 4;
    const auto uv_count = std::uint64_t(n) * meta.uvs;
    limit(uv_count <= std::numeric_limits<std::int32_t>::max(), "Too many cooked mesh UV values");
    need(r.array(uv_stride, static_cast<std::uint32_t>(uv_count)) == uv_count,
         "Cooked mesh UV count disagrees with dimensions");
    sizes.all += uv_count * uv_stride;
    if (geometry)
        b.reserve(uv_count * sizeof(std::array<float, 2>));
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 4095) == 0)
            b.o.control.check();
        if (geometry)
            geometry->instances[i].uvs.reserve(meta.uvs);
        for (unsigned j = 0; j < meta.uvs; ++j) {
            std::array<float, 2> uv{meta.full_uv ? r.f32() : r.half(), meta.full_uv ? r.f32() : r.half()};
            if (geometry)
                geometry->instances[i].uvs.push_back(uv);
        }
    }
    r.strip();
    meta.color_stride = r.u32();
    meta.colors = r.u32();
    need(meta.colors == 0 || (meta.color_stride == 4 && meta.colors == n),
         "Invalid cooked mesh color dimensions");
    if (meta.colors) {
        need(r.array(4, b.o.max_vertices) == n, "Cooked mesh color count disagrees with vertices");
        sizes.all += std::uint64_t(n) * 4;
        for (unsigned i = 0; i < n; ++i) {
            if ((i & 4095) == 0)
                b.o.control.check();
            std::array<float, 4> bgra;
            for (auto &v : bgra)
                v = static_cast<float>(r.number(1)) / 255.f;
            if (geometry)
                geometry->instances[i].color = {bgra[2], bgra[1], bgra[0], bgra[3]};
        }
    }
    std::array<IndexMeta, 5> indices{};
    std::uint64_t index_bytes = 0;
    auto primary = index_buffer(r, n, b, indices[0], index_bytes);
    sizes.all += index_bytes;
    if (!(lod.strip_flags & 4)) {
        index_buffer(r, n, b, indices[1], index_bytes);
        sizes.reversed += index_bytes;
        sizes.all += index_bytes;
    }
    index_buffer(r, n, b, indices[2], index_bytes);
    sizes.depth = index_bytes;
    sizes.all += index_bytes;
    if (!(lod.strip_flags & 4)) {
        index_buffer(r, n, b, indices[3], index_bytes);
        sizes.reversed += index_bytes;
        sizes.all += index_bytes;
    }
    Json raytrace = nullptr;
    if (!(lod.strip_flags & 8)) {
        auto raw = r.array(1, std::numeric_limits<std::int32_t>::max());
        sizes.all += raw;
        raytrace = {{"bytes", raw},
                    {"sha256", detail::hash_range(r.bytes, r.pos, raw, b.o.control)},
                    {"decoded", false}};
        r.pos += raw;
    }
    Json samplers = Json::array();
    for (std::size_t s = 0; s <= lod.sections.size(); ++s) {
        b.o.control.check();
        auto probabilities = r.count(b.o.max_triangles);
        r.range(std::uint64_t(probabilities) * 4);
        for (unsigned j = 0; j < probabilities; ++j) {
            if ((j & 4095) == 0)
                b.o.control.check();
            auto v = r.f32();
            need(v >= 0 && v <= 1, "Cooked mesh sampler probability outside unit interval");
        }
        auto aliases = r.count(b.o.max_triangles);
        need(aliases == probabilities, "Cooked mesh sampler arrays disagree");
        r.range(std::uint64_t(aliases) * 4);
        for (unsigned j = 0; j < aliases; ++j) {
            if ((j & 4095) == 0)
                b.o.control.check();
            auto v = r.i32();
            need(v >= -1 && (v < 0 || static_cast<unsigned>(v) < aliases),
                 "Cooked mesh sampler alias exceeds table");
        }
        auto total = r.f32();
        need(total >= 0, "Negative cooked mesh sampler total");
        samplers.push_back({{"entries", aliases}, {"weight", total}});
    }
    if (expected) {
        need(meta.json() == expected->v.json(),
             "Cooked mesh availability disagrees with loaded vertex buffers");
        // Producer support CVars can clear availability metadata even though the
        // serialized buffer remains present. Only advertised channels constrain it.
        const unsigned active[] = {1u, expected->flags & 4u, expected->flags & 2u, expected->flags & 8u,
                                   expected->flags & 32u};
        for (unsigned i = 0; i < 5; ++i)
            if (active[i] && expected->indices[i].count >= 0)
                need(indices[i].json() == expected->indices[i].json(),
                     "Cooked mesh availability disagrees with loaded index buffers");
        if (expected->flags & 2u)
            need(indices[2].count % 3 == 0 &&
                     static_cast<std::uint32_t>(indices[2].count / 3) == expected->depth_triangles,
                 "Cooked mesh depth-only triangle count disagrees with availability");
    }
    for (const auto &section : lod.sections) {
        const auto count = std::uint64_t(section.triangles) * 3;
        need(section.first_index <= primary.size() && count <= primary.size() - section.first_index,
             "Cooked mesh section exceeds primary index buffer");
        need(!count || (section.min_vertex <= section.max_vertex && section.max_vertex < n),
             "Cooked mesh section vertex range is invalid");
        b.add(b.triangles, section.triangles, b.o.max_triangles,
              "Cooked mesh sections exceed aggregate triangle budget");
        if (geometry)
            b.reserve(std::uint64_t(section.triangles) * sizeof(MeshTriangle));
        for (std::uint64_t j = 0; j < count; j += 3) {
            if ((j & 4095) == 0)
                b.o.control.check();
            std::array<std::uint32_t, 3> triangle;
            for (unsigned axis = 0; axis < 3; ++axis) {
                auto v = primary[section.first_index + j + axis];
                need(v >= section.min_vertex && v <= section.max_vertex,
                     "Cooked mesh section index outside its declared vertex range");
                triangle[axis] = v;
            }
            if (geometry) {
                const auto id = static_cast<std::uint32_t>(geometry->triangles.size());
                geometry->triangles.push_back({id, id, section.material, triangle});
            }
        }
        if (geometry) {
            std::string name = "$material-index:" + std::to_string(section.material);
            if (section.material < materials.size()) {
                const auto &m = materials[section.material];
                const auto imported = m.find("/Script/Engine.StaticMaterial.ImportedMaterialSlotName");
                const auto runtime = m.find("/Script/Engine.StaticMaterial.MaterialSlotName");
                auto it = imported != m.end() ? imported : runtime;
                if (it != m.end() && it->contains("value") && (*it)["value"].is_string())
                    name = (*it)["value"].get<std::string>();
            }
            geometry->material_slots[section.material] = name;
        }
    }
    if (geometry) {
        if (n)
            geometry->bounds = {{"min", low}, {"max", high}};
        geometry->attributes = {{"source", "cooked-render-vertices"},
                                {"uvChannels", meta.uvs},
                                {"colorsStored", meta.colors != 0},
                                {"colorFallback", meta.colors ? Json(nullptr) : Json("opaque-white")},
                                {"normals", "stored-signed-normalized-without-renormalization"}};
        lod.geometry = std::move(geometry);
    }
    Json index_list = Json::array();
    for (const auto &v : indices)
        index_list.push_back(v.json());
    lod.buffers = {{"vertices", meta.json()},
                   {"indices", index_list},
                   {"rayTracing", raytrace},
                   {"samplers", samplers},
                   {"payloadOffset", begin},
                   {"payloadBytes", r.pos - begin},
                   {"sha256", detail::hash_range(r.bytes, begin, r.pos - begin, b.o.control)}};
    return sizes;
}
} // namespace

Json CookedMeshSection::to_json() const {
    return {{"material", material},      {"firstIndex", first_index},
            {"triangles", triangles},    {"minVertex", min_vertex},
            {"maxVertex", max_vertex},   {"collision", collision},
            {"shadow", shadow},          {"forceOpaque", opaque},
            {"rayTracing", ray_tracing}, {"distanceField", distance_field}};
}
Json CookedMeshLod::to_json(bool include) const {
    Json s = Json::array();
    for (const auto &v : sections)
        s.push_back(v.to_json());
    Json j = {{"cookedOut", cooked_out},
              {"inlined", inlined},
              {"stripFlags", strip_flags},
              {"maxDeviation", max_deviation},
              {"sections", s},
              {"buffers", buffers},
              {"bulk", bulk},
              {"unavailable", unavailable},
              {"geometryAvailable", geometry != nullptr}};
    if (include && geometry)
        j["geometry"] = geometry->to_json();
    return j;
}
Json CookedStaticMesh::to_json(bool include) const {
    Json l = Json::array();
    for (const auto &v : lods)
        l.push_back(v.to_json(include));
    return {{"schema", "cyuediff.cooked-static-mesh/1"},
            {"profile", "ue5.3-windows"},
            {"object", object_path},
            {"packageSha256", package_sha256},
            {"schemaContextSha256", schema_context_sha256},
            {"scriptObjectsSha256", script_objects_sha256},
            {"exportIndex", export_index},
            {"mappingVerified", false},
            {"nativeOffset", native_offset},
            {"parsedNativeBytes", parsed_native_bytes},
            {"nativePrefix", native_prefix},
            {"lods", l},
            {"materials", materials},
            {"nanite", nanite ? nanite->catalog() : Json(nullptr)},
            {"tail",
             {{"offset", tail_offset},
              {"bytes", tail_bytes},
              {"sha256", tail_sha256},
              {"decoded", false},
              {"includes", nanite ? "distance-field, render-data and later object fields"
                                  : "Nanite, distance-field, render-data and later object fields"}}}};
}
CookedStaticMesh read_zen_static_mesh(const Container &container, std::size_t entry,
                                      const ZenReadOptions &zen, const ContainerReadOptions &read,
                                      const CookedMeshOptions &supplied, std::size_t selected) {
    auto options = supplied;
    OperationControl control;
    control.cancelled = [a = options.control.cancelled, b = zen.control.cancelled, c = read.control.cancelled,
                         d = options.nanite.control.cancelled] {
        return (a && a()) || (b && b()) || (c && c()) || (d && d());
    };
    control.progress =
        options.control.progress
            ? options.control.progress
            : (zen.control.progress
                   ? zen.control.progress
                   : (read.control.progress ? read.control.progress : options.nanite.control.progress));
    options.control = control;
    if (options.nanite.profile != NaniteProfile::unspecified &&
        options.nanite.profile != NaniteProfile::unreal_5_3_windows)
        throw Error("cooked-nanite-profile", "Unsupported Nanite resource profile");
    control.check();
    if (options.profile != CookedMeshProfile::unreal_5_3_windows || zen.profile != ZenProfile::unreal_5_3 ||
        !options.keep_mobile_min_lod_setting.has_value())
        throw Error("cooked-mesh-profile", "Cooked static meshes require UE 5.3 Windows and the explicit "
                                           "producer KeepMobileMinLODSettingOnDesktop setting");
    need(entry < container.entries().size(), "Invalid cooked mesh package entry");
    const auto &member = container.entries()[entry];
    need(member.chunk_type == 1 && member.id.size() == 24, "Cooked mesh requires a Zen export-bundle chunk");
    limit(member.size <= options.max_read_bytes, "Cooked mesh package exceeds read budget");
    auto reads = read;
    reads.control = control;
    const auto bytes = container.read(entry, reads);
    auto zopts = zen;
    zopts.control = control;
    auto package = ZenPackage::parse(bytes, zopts);
    if ((package.flags & 0x80000200u) != 0x80000200u)
        throw Error("cooked-mesh-profile", "Cooked static mesh requires an editor-stripped cooked package");
    if (selected == SIZE_MAX)
        for (std::size_t i = 0; i < package.exports.size(); ++i)
            if (package.exports[i].class_name == "/Script/Engine.StaticMesh" &&
                !(package.exports[i].flags & 0x30)) {
                need(selected == SIZE_MAX, "Select one of several cooked StaticMesh exports");
                selected = i;
            }
    need(selected < package.exports.size(), "No selected cooked StaticMesh export");
    const auto &object = package.exports[selected];
    need(object.class_name == "/Script/Engine.StaticMesh" && !(object.flags & 0x30),
         "Selected export is not a non-template StaticMesh");
    if (object.property_read.is_null() || object.property_read.value("status", std::string{}) != "decoded")
        throw Error("cooked-mesh-context",
                    "Cooked StaticMesh requires a successfully decoded explicit property binding");
    const auto offset = object.property_read.at("offset").get<std::uint64_t>(),
               consumed = object.property_read.at("consumedBytes").get<std::uint64_t>();
    need(offset <= object.size && consumed <= object.size - offset, "Invalid cooked mesh property boundary");
    CookedStaticMesh out;
    out.object_path = object.path;
    out.package_sha256 = package.sha256;
    out.schema_context_sha256 = package.schema_context_sha256;
    out.script_objects_sha256 = package.script_objects_sha256;
    out.export_index = selected;
    out.native_offset = object.offset + offset + consumed;
    const auto &properties = object.property_read.at("properties");
    const auto m = properties.find("/Script/Engine.StaticMesh.StaticMaterials");
    if (m != properties.end() && m->contains("value") && (*m)["value"].is_array())
        out.materials = (*m)["value"];
    Cursor r{bytes, out.native_offset, object.offset + object.size};
    const auto guid = r.boolean();
    if (guid) {
        r.range(16);
        r.pos += 16;
    }
    r.strip();
    need(r.boolean(), "StaticMesh native data is not cooked");
    auto body = r.i32(), nav = r.i32();
    r.range(16);
    std::string lighting;
    const char *hex = "0123456789abcdef";
    for (unsigned i = 0; i < 16; ++i) {
        auto v = r.number(1);
        lighting.push_back(hex[v >> 4]);
        lighting.push_back(hex[v & 15]);
    }
    auto sockets = r.count(zen.max_entries);
    r.range(std::uint64_t(sockets) * 4);
    Json socket_refs = Json::array();
    Budget budget{options};
    budget.reserve(std::uint64_t(sockets) * 32 + out.materials.dump().size());
    for (unsigned i = 0; i < sockets; ++i) {
        if ((i & 4095) == 0)
            control.check();
        socket_refs.push_back(r.i32());
    }
    Json mobile = nullptr;
    if (*options.keep_mobile_min_lod_setting)
        mobile = r.i32();
    out.native_prefix = {{"objectGuidStored", guid},
                         {"bodySetupIndex", body},
                         {"navCollisionIndex", nav},
                         {"lightingGuidBytes", lighting},
                         {"socketIndices", socket_refs},
                         {"keepMobileMinLODSettingOnDesktop", *options.keep_mobile_min_lod_setting},
                         {"mobileMinLOD", mobile}};
    const auto lods = r.count(options.max_lods);
    budget.reserve(std::uint64_t(lods) * 4096);
    detail::ZenBulkReader bulk_reader(container, package, member, bytes, reads, options.max_read_bytes,
                                      "cooked-mesh");
    for (unsigned l = 0; l < lods; ++l) {
        control.checkpoint("cooked-mesh-lod", l, lods);
        CookedMeshLod lod;
        auto global = r.number(1);
        lod.strip_flags = static_cast<std::uint8_t>(r.number(1));
        need(global == 1 && !(lod.strip_flags & ~15u), "Unsupported cooked mesh LOD strip flags");
        auto count = r.count(options.max_sections);
        budget.add(budget.sections, count, options.max_sections,
                   "Cooked mesh LODs exceed aggregate section budget");
        budget.reserve(std::uint64_t(count) * 512);
        r.range(std::uint64_t(count) * 40);
        lod.sections.reserve(count);
        for (unsigned s = 0; s < count; ++s) {
            control.check();
            CookedMeshSection section;
            section.material = r.u32();
            section.first_index = r.u32();
            section.triangles = r.u32();
            section.min_vertex = r.u32();
            section.max_vertex = r.u32();
            section.collision = r.boolean();
            section.shadow = r.boolean();
            section.opaque = r.boolean();
            section.ray_tracing = r.boolean();
            section.distance_field = r.boolean();
            limit(section.triangles <= options.max_triangles, "Cooked mesh section exceeds triangle budget");
            lod.sections.push_back(section);
        }
        lod.max_deviation = r.f32();
        need(lod.max_deviation >= 0, "Negative cooked mesh deviation");
        lod.cooked_out = r.boolean();
        lod.inlined = r.boolean();
        if (lod.cooked_out) {
            need(lod.inlined, "Cooked-out mesh LOD must be inlined");
            lod.unavailable = "cooked-out";
        } else if (lod.inlined) {
            auto sizes = buffers(r, lod, budget, nullptr, out.materials);
            match_sizes(sizes, stored_sizes(r));
        } else {
            auto bulk_index = r.u32();
            auto window = bulk_reader.describe(bulk_index, r.pos, r.end);
            const auto available = availability(r, options);
            const auto sizes = stored_sizes(r);
            bulk_reader.load(window);
            lod.bulk = window.metadata;
            lod.bulk["availability"] = available.json();
            lod.unavailable = window.unavailable;
            if (lod.unavailable.empty()) {
                Cursor payload{*window.data, window.offset, window.offset + window.bytes};
                payload.range(window.bytes);
                auto actual = buffers(payload, lod, budget, &available, out.materials);
                match_sizes(actual, sizes);
                need(payload.pos == payload.end, "Trailing bytes in cooked mesh LOD bulk payload");
            }
        }
        out.lods.push_back(std::move(lod));
    }
    const auto inlined = r.number(1);
    need(inlined <= lods, "Cooked mesh inline LOD count exceeds LOD array");
    out.native_prefix["numInlinedLODs"] = inlined;
    if (options.nanite.profile != NaniteProfile::unspecified) {
        auto nanite_options = options.nanite;
        nanite_options.control = control;
        nanite_options.max_output_bytes =
            std::min(nanite_options.max_output_bytes, options.max_output_bytes - budget.output);
        auto resource = detail::read_nanite_resource(bytes, r.pos, r.end, bulk_reader, nanite_options,
                                                     {{"object", out.object_path},
                                                      {"packageSha256", out.package_sha256},
                                                      {"schemaContextSha256", out.schema_context_sha256},
                                                      {"scriptObjectsSha256", out.script_objects_sha256},
                                                      {"exportIndex", out.export_index}});
        budget.reserve(resource.accounted_output_bytes());
        out.nanite = std::make_shared<CookedNaniteResource>(std::move(resource));
    }

    out.tail_offset = r.pos;
    out.tail_bytes = r.end - r.pos;
    out.tail_sha256 = detail::hash_range(bytes, r.pos, out.tail_bytes, control);
    out.parsed_native_bytes = r.pos - out.native_offset;
    control.check();
    return out;
}
} // namespace cyuediff
