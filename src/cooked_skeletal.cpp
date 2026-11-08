#include <cyuediff/cooked_skeletal.hpp>
#include "archive.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
namespace cyuediff {
namespace {
void need(bool b, const char *s, std::uint64_t pos = 0) {
    if (!b)
        throw Error("cooked-skeletal-layout", s, pos);
}
void limit(bool b, const char *s) {
    if (!b)
        throw Error("cooked-skeletal-limit", s);
}
void supported(bool b, const char *s) {
    if (!b)
        throw Error("cooked-skeletal-feature", s);
}
struct Cursor {
    const std::vector<std::uint8_t> &bytes;
    std::uint64_t pos, end;
    void range(std::uint64_t n) const {
        need(pos <= end && n <= end - pos, "Skeletal field exceeds native window", pos);
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
        const auto v = u32();
        need(v <= 1, "Invalid skeletal bool32", pos - 4);
        return v != 0;
    }
    float f32() {
        auto v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        need(std::isfinite(f), "Non-finite skeletal float", pos - 4);
        return f;
    }
    double f64() {
        auto v = number(8);
        double f;
        std::memcpy(&f, &v, 8);
        need(std::isfinite(f), "Non-finite skeletal double", pos - 8);
        return f;
    }
    float half() {
        auto v = number(2);
        const auto exp = (v >> 10) & 31, mant = v & 1023;
        need(exp != 31, "Non-finite skeletal half", pos - 2);
        return (v & 0x8000 ? -1.f : 1.f) *
               std::ldexp(static_cast<float>(exp ? mant + 1024 : mant), exp ? int(exp) - 25 : -24);
    }
    std::uint32_t count(std::uint32_t max) {
        auto n = i32();
        need(n >= 0, "Negative skeletal array count", pos - 4);
        limit(static_cast<std::uint32_t>(n) <= max, "Skeletal array exceeds element budget");
        return n;
    }
    std::uint32_t array(unsigned stride, std::uint32_t max) {
        need(u32() == stride, "Skeletal bulk-array stride disagrees with profile", pos - 4);
        auto n = count(max);
        range(std::uint64_t(n) * stride);
        return n;
    }
    std::uint8_t strip() {
        auto g = number(1), c = number(1);
        supported(g == 1, "Skeletal native decoder requires editor-stripped non-server data");
        return static_cast<std::uint8_t>(c);
    }
    std::string name(const std::vector<std::string> &names) {
        const auto index = u32(), suffix = u32();
        need(index < names.size(), "Skeletal name index exceeds name map", pos - 8);
        auto result = names[index];
        if (suffix)
            result += "_" + std::to_string(suffix - 1);
        return result;
    }
    std::string guid() {
        std::string out;
        const char *hex = "0123456789abcdef";
        for (unsigned i = 0; i < 16; ++i) {
            auto v = number(1);
            out.push_back(hex[v >> 4]);
            out.push_back(hex[v & 15]);
        }
        return out;
    }
};
struct Budget {
    const CookedSkeletalOptions &o;
    std::uint64_t vertices = 0, indices = 0, triangles = 0, sections = 0, influences = 0, output = 0;
    void add(std::uint64_t &counter, std::uint64_t n, std::uint64_t max, const char *s) {
        limit(counter <= max && n <= max - counter, s);
        counter += n;
    }
    void reserve(std::uint64_t n) {
        add(output, n, o.max_output_bytes, "Skeletal output exceeds byte budget");
    }
};
std::vector<std::uint16_t> bone_array(Cursor &r, Budget &b, std::size_t bones) {
    auto n = r.count(b.o.max_bones);
    r.range(std::uint64_t(n) * 2);
    b.reserve(std::uint64_t(n) * 2);
    std::vector<std::uint16_t> out;
    out.reserve(n);
    std::set<std::uint16_t> seen;
    for (unsigned i = 0; i < n; ++i) {
        b.o.control.check();
        auto v = static_cast<std::uint16_t>(r.number(2));
        need(v < bones && seen.insert(v).second, "Skeletal bone array has an absent or duplicate bone",
             r.pos - 2);
        out.push_back(v);
    }
    return out;
}
Json bone_json(const MeshBone &v) {
    return {{"id", v.id},
            {"name", v.name},
            {"parent", v.parent},
            {"rotation", v.rotation},
            {"translation", v.translation},
            {"scale", v.scale}};
}
float packed(Cursor &r, bool high) {
    auto v = static_cast<unsigned>(r.number(high ? 2 : 1));
    auto max = high ? 65536 : 256;
    return static_cast<float>(v & (max / 2) ? int(v) - max : int(v)) / (high ? 32767.f : 127.f);
}
void section(Cursor &r, CookedSkeletalLod &lod, const std::vector<std::string> &, Budget &b,
             std::size_t bones) {
    CookedSkeletalSection s;
    const auto strips = r.strip();
    supported((strips & ~1u) == 0, "Unknown skeletal section strip flags");
    s.material = static_cast<std::uint32_t>(r.number(2));
    s.first_index = r.u32();
    s.triangles = r.u32();
    auto tangent = r.boolean();
    auto mask = r.number(1);
    need(mask <= 3, "Invalid skeletal tangent mask channel");
    auto shadow = r.boolean(), ray = r.boolean();
    s.first_vertex = r.u32();
    const auto cloth_lods = r.count(b.o.max_lods);
    for (unsigned i = 0; i < cloth_lods; ++i) {
        b.o.control.check();
        supported(r.count(b.o.max_vertices) == 0, "Skeletal cloth mappings require a cloth decoder");
    }
    s.bone_map = bone_array(r, b, bones);
    s.vertices = r.u32();
    auto maxinf = r.i32();
    need(maxinf >= 0, "Negative skeletal section influence count");
    s.max_influences = static_cast<std::uint32_t>(maxinf);
    limit(s.vertices <= b.o.max_vertices && s.max_influences <= b.o.max_influences,
          "Skeletal section exceeds vertex or influence budget");
    auto cloth_asset = static_cast<std::uint16_t>(r.number(2));
    auto cloth_guid = r.guid();
    auto cloth_lod = r.i32();
    supported(cloth_asset == 65535 && cloth_lod == -1 && cloth_guid == std::string(32, '0'),
              "Skeletal clothing binding requires a cloth decoder");
    supported((strips & 1) != 0, "Skeletal duplicate-vertex table requires another decoder");
    const auto disabled = r.boolean();
    s.fields = {{"stripFlags", strips},
                {"recomputeTangents", tangent},
                {"tangentMaskChannel", mask},
                {"shadow", shadow},
                {"rayTracing", ray},
                {"disabled", disabled},
                {"clothMappingLODs", cloth_lods}};
    b.add(b.triangles, s.triangles, b.o.max_triangles, "Skeletal LODs exceed triangle budget");
    lod.sections.push_back(std::move(s));
}
void buffers(Cursor &r, CookedSkeletalLod &lod, Budget &b, const CookedSkeletalMesh &mesh, bool colors,
             std::uint32_t stored_size) {
    const auto begin = r.pos;
    need(r.strip() == lod.strip_flags, "Skeletal streamed strip flags disagree with LOD");
    auto index_bytes = static_cast<unsigned>(r.number(1));
    need(index_bytes == 2 || index_bytes == 4, "Invalid skeletal index width");
    auto ni = r.array(index_bytes, b.o.max_indices);
    b.add(b.indices, ni, b.o.max_indices, "Skeletal LODs exceed index budget");
    b.reserve(std::uint64_t(ni) * 4);
    std::vector<std::uint32_t> indices;
    indices.reserve(ni);
    for (unsigned i = 0; i < ni; ++i) {
        if ((i & 4095) == 0)
            b.o.control.check();
        indices.push_back(static_cast<std::uint32_t>(r.number(index_bytes)));
    }
    need(r.u32() == 12, "Skeletal position stride is not float3");
    auto n = r.u32();
    b.add(b.vertices, n, b.o.max_vertices, "Skeletal LODs exceed vertex budget");
    need(r.array(12, b.o.max_vertices) == n, "Skeletal position counts disagree");
    b.reserve(std::uint64_t(n) * 4);
    std::vector<std::int32_t> owners(n, -1);
    for (std::size_t si = 0; si < lod.sections.size(); ++si) {
        auto &s = lod.sections[si];
        need(s.material < mesh.materials.size(), "Skeletal section material is absent");
        need(s.first_vertex <= n && s.vertices <= n - s.first_vertex,
             "Skeletal section exceeds vertex buffer");
        for (std::uint64_t j = s.first_vertex; j < std::uint64_t(s.first_vertex) + s.vertices; ++j) {
            if ((j & 4095) == 0)
                b.o.control.check();
            need(owners[j] == -1, "Overlapping skeletal section vertex ranges");
            owners[j] = static_cast<std::int32_t>(si);
        }
    }
    for (auto i : indices)
        need(i < n, "Skeletal index references an absent vertex");
    std::shared_ptr<MeshGeometry> g;
    std::shared_ptr<MeshSkin> skin;
    if (b.o.geometry) {
        b.reserve(std::uint64_t(n) * (sizeof(MeshVertex) + sizeof(MeshInstance) + 64));
        g = std::make_shared<MeshGeometry>();
        g->vertices.resize(n);
        g->instances.resize(n);
        skin = std::make_shared<MeshSkin>();
        skin->bones = mesh.bones;
        b.reserve(std::uint64_t(mesh.bones.size()) * 256);
    }
    std::array<float, 3> low{}, high{};
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 4095) == 0)
            b.o.control.checkpoint("cooked-skeletal-vertices", i, n);
        std::array<float, 3> p{r.f32(), r.f32(), r.f32()};
        if (!i)
            low = high = p;
        for (unsigned k = 0; k < 3; ++k) {
            low[k] = std::min(low[k], p[k]);
            high[k] = std::max(high[k], p[k]);
        }
        if (g) {
            g->vertices[i] = {i, p};
            g->instances[i].id = i;
            g->instances[i].vertex = i;
        }
    }
    need(r.strip() == 0, "Unknown skeletal tangent buffer strip flags");
    auto uvs = r.u32();
    need(r.u32() == n, "Skeletal tangent vertex count disagrees");
    auto full = r.boolean(), hi = r.boolean();
    limit(uvs <= b.o.max_uv_channels, "Skeletal UV channels exceed budget");
    need(r.array(hi ? 16 : 8, b.o.max_vertices) == n, "Skeletal tangent array count disagrees");
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 4095) == 0)
            b.o.control.check();
        std::array<float, 4> x, z;
        for (auto &v : x)
            v = packed(r, hi);
        for (auto &v : z)
            v = packed(r, hi);
        if (g) {
            g->instances[i].tangent = {x[0], x[1], x[2]};
            g->instances[i].normal = {z[0], z[1], z[2]};
            g->instances[i].binormal_sign = z[3];
        }
    }
    auto nuv = std::uint64_t(n) * uvs;
    limit(nuv <= std::numeric_limits<std::int32_t>::max(), "Skeletal UV count exceeds range");
    need(r.array(full ? 8 : 4, static_cast<std::uint32_t>(nuv)) == nuv, "Skeletal UV array count disagrees");
    if (g)
        b.reserve(nuv * sizeof(std::array<float, 2>));
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 4095) == 0)
            b.o.control.check();
        if (g)
            g->instances[i].uvs.reserve(uvs);
        for (unsigned j = 0; j < uvs; ++j) {
            std::array<float, 2> v{full ? r.f32() : r.half(), full ? r.f32() : r.half()};
            if (g)
                g->instances[i].uvs.push_back(v);
        }
    }
    need(r.strip() == 0, "Unknown skeletal skin buffer strip flags");
    const auto variable = r.boolean();
    const auto maxinf = r.u32(), weight_count = r.u32(), weight_vertices = r.u32();
    const auto wide_bone = r.boolean(), wide_weight = r.boolean();
    need(weight_vertices == n, "Skeletal skin vertex count disagrees");
    limit(maxinf <= b.o.max_influences, "Skeletal influence count exceeds budget");
    const unsigned bone_bytes = wide_bone ? 2 : 1, weight_bytes = wide_weight ? 2 : 1;
    b.add(b.influences, weight_count, std::uint64_t(b.o.max_vertices) * b.o.max_influences,
          "Skeletal LODs exceed influence budget");
    const auto raw_bytes = r.array(1, std::numeric_limits<std::int32_t>::max());
    need(std::uint64_t(weight_count) * (bone_bytes + weight_bytes) == raw_bytes,
         "Skeletal skin byte count disagrees with weights");
    const auto raw = r.pos;
    r.pos += raw_bytes;
    need(r.strip() == 0, "Unknown skeletal lookup buffer strip flags");
    const auto look_vertices = r.u32();
    need(look_vertices == (variable ? n : 0), "Skeletal lookup vertex count disagrees with skin mode");
    const auto look_count = r.array(4, b.o.max_vertices);
    need(look_count == look_vertices, "Skeletal lookup array count disagrees");
    const auto lookup = r.pos;
    r.pos += std::uint64_t(look_count) * 4;
    if (!variable)
        need(std::uint64_t(n) * maxinf == weight_count, "Skeletal fixed influence count disagrees");
    std::uint64_t used = 0;
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 4095) == 0)
            b.o.control.checkpoint("cooked-skeletal-weights", i, n);
        auto c = maxinf;
        std::uint64_t off = std::uint64_t(i) * maxinf * (bone_bytes + weight_bytes);
        if (variable) {
            Cursor v{r.bytes, lookup + std::uint64_t(i) * 4, lookup + std::uint64_t(i + 1) * 4};
            auto packed = v.u32();
            off = packed >> 8;
            c = packed & 255;
            need(c <= maxinf, "Skeletal lookup influence count exceeds maximum");
        }
        need(off <= raw_bytes && std::uint64_t(c) * (bone_bytes + weight_bytes) <= raw_bytes - off,
             "Skeletal lookup exceeds skin buffer");
        used += c;
        need(owners[i] >= 0, "Skeletal skin vertex has no section bone map");
        const auto &s = lod.sections[owners[i]];
        Cursor bi{r.bytes, raw + off, raw + raw_bytes},
            wi{r.bytes, raw + off + std::uint64_t(c) * bone_bytes, raw + raw_bytes};
        std::set<std::uint32_t> positive;
        if (skin)
            b.reserve(std::uint64_t(c) * 64 + 128);
        std::vector<MeshInfluence> influences;
        for (unsigned j = 0; j < c; ++j) {
            const auto local = static_cast<std::uint32_t>(bi.number(bone_bytes));
            auto w = static_cast<std::uint16_t>(wi.number(weight_bytes));
            if (!wide_weight)
                w = static_cast<std::uint16_t>(w * 257u);
            need(local < s.bone_map.size(), "Skeletal influence references an absent section bone");
            // Zero padding is retained in the raw buffer fingerprint. Semantic skin
            // maps include nonzero weights only; fixed streams can repeat padding IDs.
            if (w) {
                need(positive.insert(s.bone_map[local]).second, "Duplicate nonzero skeletal influence bone");
                if (skin)
                    influences.push_back({s.bone_map[local], w});
            }
        }
        need(positive.size() <= s.max_influences, "Nonzero skeletal influences exceed section maximum");
        if (skin)
            skin->profiles["default"][i] = std::move(influences);
    }
    need(used == weight_count, "Skeletal lookup total disagrees with stored influence count");
    if (colors) {
        need(r.strip() == 0, "Unknown skeletal color buffer strip flags");
        need(r.u32() == 4 && r.u32() == n, "Skeletal color dimensions disagree");
        need(r.array(4, b.o.max_vertices) == n, "Skeletal color array count disagrees");
        for (unsigned i = 0; i < n; ++i) {
            if ((i & 4095) == 0)
                b.o.control.check();
            std::array<float, 4> v;
            for (auto &c : v)
                c = static_cast<float>(r.number(1)) / 255.f;
            if (g)
                g->instances[i].color = {v[2], v[1], v[0], v[3]};
        }
    }
    supported(r.count(b.o.max_sections) == 0, "Alternate cooked skin weights require another decoder");
    auto ray_bytes = r.count(std::numeric_limits<std::int32_t>::max());
    r.range(ray_bytes);
    Json ray = {{"bytes", ray_bytes},
                {"sha256", detail::hash_range(r.bytes, r.pos, ray_bytes, b.o.control)},
                {"decoded", false}};
    r.pos += ray_bytes;
    supported(!r.boolean(), "Compressed cooked morph buffers require another decoder");
    supported(r.count(b.o.max_sections) == 0, "Cooked skeletal vertex attributes require another decoder");
    need(r.pos - begin == stored_size, "Skeletal serialized buffer size disagrees with payload");
    std::uint64_t covered_indices = 0;
    for (const auto &s : lod.sections) {
        const auto count = std::uint64_t(s.triangles) * 3;
        need(s.first_index == covered_indices && count <= ni - covered_indices,
             "Skeletal sections do not partition index buffer");
        covered_indices += count;
        if (g)
            b.reserve(std::uint64_t(s.triangles) * sizeof(MeshTriangle));
        for (std::uint64_t j = s.first_index; j < std::uint64_t(s.first_index) + count; j += 3) {
            if ((j & 4095) == 0)
                b.o.control.check();
            std::array<std::uint32_t, 3> t{indices[j], indices[j + 1], indices[j + 2]};
            for (auto v : t)
                need(v >= s.first_vertex && v - s.first_vertex < s.vertices,
                     "Skeletal triangle exceeds section vertex range");
            if (g) {
                auto id = static_cast<std::uint32_t>(g->triangles.size());
                g->triangles.push_back({id, id, s.material, t});
            }
        }
        if (g)
            g->material_slots[s.material] = mesh.materials[s.material].at("slot").get<std::string>();
    }
    need(covered_indices == ni, "Skeletal indices have no owning section");
    if (g) {
        skin->validate(b.o.control);
        g->skin = skin;
        g->bounds = {{"min", low}, {"max", high}};
        g->attributes = {{"source", "cooked-skeletal-render-lod"},
                         {"colorsStored", colors},
                         {"weightEncoding", wide_weight ? "uint16" : "uint8-expanded-to-uint16"},
                         {"weightNormalizationApplied", false},
                         {"zeroWeightPaddingInSemanticSkin", false}};
        lod.geometry = g;
    }
    lod.buffers = {{"vertices", n},
                   {"indices", ni},
                   {"indexBytes", index_bytes},
                   {"uvChannels", uvs},
                   {"fullPrecisionUV", full},
                   {"highPrecisionTangents", hi},
                   {"colorsStored", colors},
                   {"variableInfluences", variable},
                   {"maxInfluences", maxinf},
                   {"influences", weight_count},
                   {"boneIndexBytes", bone_bytes},
                   {"boneWeightBytes", weight_bytes},
                   {"lookupVertices", look_vertices},
                   {"skinBufferSha256", detail::hash_range(r.bytes, raw, raw_bytes, b.o.control)},
                   {"rayTracing", ray},
                   {"serializedBytes", stored_size}};
}
} // namespace
Json CookedSkeletalSection::to_json() const {
    return {{"material", material},        {"firstIndex", first_index}, {"triangles", triangles},
            {"firstVertex", first_vertex}, {"vertices", vertices},      {"maxInfluences", max_influences},
            {"boneMap", bone_map},         {"fields", fields}};
}
Json CookedSkeletalLod::to_json(bool include) const {
    Json s = Json::array();
    for (const auto &v : sections)
        s.push_back(v.to_json());
    Json j = {{"cookedOut", cooked_out},     {"inlined", inlined},
              {"stripFlags", strip_flags},   {"requiredBones", required_bones},
              {"activeBones", active_bones}, {"sections", s},
              {"buffers", buffers},          {"unavailable", unavailable}};
    if (include && geometry)
        j["geometry"] = geometry->to_json();
    return j;
}
Json CookedSkeletalMesh::to_json(bool include) const {
    Json l = Json::array(), b = Json::array();
    for (const auto &v : lods)
        l.push_back(v.to_json(include));
    for (const auto &v : bones)
        b.push_back(bone_json(v));
    return {{"schema", "cyuediff.cooked-skeletal-mesh/1"},
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
            {"materials", materials},
            {"bones", b},
            {"lods", l},
            {"tail",
             {{"offset", tail_offset}, {"bytes", tail_bytes}, {"sha256", tail_sha256}, {"decoded", false}}}};
}
CookedSkeletalMesh read_zen_skeletal_mesh(const Container &container, std::size_t entry,
                                          const ZenReadOptions &zen, const ContainerReadOptions &read,
                                          const CookedSkeletalOptions &input, std::size_t selected) {
    auto o = input;
    OperationControl control;
    control.cancelled = [a = input.control, b = zen.control, c = read.control] {
        a.check();
        b.check();
        c.check();
        return false;
    };
    control.progress = input.control.progress
                           ? input.control.progress
                           : (zen.control.progress ? zen.control.progress : read.control.progress);
    o.control = control;
    control.check();
    if (o.profile != CookedSkeletalProfile::unreal_5_3_windows || zen.profile != ZenProfile::unreal_5_3 ||
        !o.keep_mobile_min_lod_setting.has_value())
        throw Error("cooked-skeletal-profile", "Cooked skeletal meshes require UE 5.3 Windows and explicit "
                                               "producer KeepMobileMinLODSettingOnDesktop presence");
    need(entry < container.entries().size(), "Invalid skeletal package entry");
    const auto &member = container.entries()[entry];
    need(member.chunk_type == 1 && member.id.size() == 24,
         "Skeletal mesh requires a Zen export-bundle chunk");
    limit(member.size <= o.max_read_bytes, "Skeletal package exceeds read budget");
    auto reads = read;
    reads.control = control;
    auto bytes = container.read(entry, reads);
    auto zopts = zen;
    zopts.control = control;
    auto package = ZenPackage::parse(bytes, zopts);
    if ((package.flags & 0x80000200u) != 0x80000200u)
        throw Error("cooked-skeletal-profile", "Skeletal decoder requires an editor-stripped cooked package");
    if (selected == SIZE_MAX)
        for (std::size_t i = 0; i < package.exports.size(); ++i)
            if (package.exports[i].class_name == "/Script/Engine.SkeletalMesh" &&
                !(package.exports[i].flags & 0x30)) {
                need(selected == SIZE_MAX, "Select one of several SkeletalMesh exports");
                selected = i;
            }
    need(selected < package.exports.size(), "No selected cooked SkeletalMesh export");
    const auto &object = package.exports[selected];
    need(object.class_name == "/Script/Engine.SkeletalMesh" && !(object.flags & 0x30),
         "Selected export is not a non-template SkeletalMesh");
    if (object.property_read.is_null() || object.property_read.value("status", std::string{}) != "decoded")
        throw Error("cooked-skeletal-context", "SkeletalMesh requires a decoded explicit property binding");
    const auto offset = object.property_read.at("offset").get<std::uint64_t>(),
               consumed = object.property_read.at("consumedBytes").get<std::uint64_t>();
    need(offset <= object.size && consumed <= object.size - offset, "Invalid skeletal property boundary");
    bool colors = false;
    const auto &properties = object.property_read.at("properties");
    const auto prop = properties.find("/Script/Engine.SkeletalMesh.bHasVertexColors");
    if (prop != properties.end()) {
        need(prop->value("type", std::string{}) == "BoolProperty" && prop->contains("value") &&
                 prop->at("value").is_boolean(),
             "Invalid skeletal bHasVertexColors property");
        colors = prop->at("value").get<bool>();
        if (o.has_vertex_colors.has_value())
            need(*o.has_vertex_colors == colors, "Producer color setting disagrees with stored property");
    } else {
        if (!o.has_vertex_colors.has_value())
            throw Error("cooked-skeletal-context",
                        "Omitted bHasVertexColors requires an explicit producer color setting");
        colors = *o.has_vertex_colors;
    }
    CookedSkeletalMesh out;
    out.object_path = object.path;
    out.package_sha256 = package.sha256;
    out.schema_context_sha256 = package.schema_context_sha256;
    out.script_objects_sha256 = package.script_objects_sha256;
    out.export_index = selected;
    out.native_offset = object.offset + offset + consumed;
    Cursor r{bytes, out.native_offset, object.offset + object.size};
    Budget budget{o};
    const auto guid = r.boolean();
    Json object_guid = nullptr;
    if (guid)
        object_guid = r.guid();
    need(r.strip() == 0, "Unknown skeletal mesh native strip flags");
    Json bounds = Json::array();
    for (unsigned i = 0; i < 7; ++i) {
        auto v = r.f64();
        need(i < 3 || v >= 0, "Negative skeletal bounds extent or sphere radius");
        bounds.push_back(v);
    }
    const auto mats = r.count(o.max_sections);
    budget.reserve(std::uint64_t(mats) * 1024);
    for (unsigned i = 0; i < mats; ++i) {
        control.check();
        auto material = r.i32();
        auto slot = r.name(package.names);
        bool imported = r.boolean();
        Json imported_slot = imported ? Json(r.name(package.names)) : Json(nullptr);
        const auto initialized = r.boolean(), override_density = r.boolean();
        Json densities = Json::array();
        for (unsigned j = 0; j < 4; ++j)
            densities.push_back(r.f32());
        budget.reserve(slot.size() + imported_slot.dump().size());
        out.materials.push_back({{"objectIndex", material},
                                 {"slot", slot},
                                 {"importedSlot", imported_slot},
                                 {"uvInitialized", initialized},
                                 {"uvDensityOverride", override_density},
                                 {"uvDensities", densities}});
    }
    const auto nb = r.count(o.max_bones);
    budget.reserve(std::uint64_t(nb) * 512);
    out.bones.reserve(nb);
    for (unsigned i = 0; i < nb; ++i) {
        control.check();
        MeshBone b;
        b.id = i;
        b.name = r.name(package.names);
        budget.reserve(b.name.size());
        b.parent = r.i32();
        need(b.parent < static_cast<std::int64_t>(nb), "Skeletal parent bone index exceeds table");
        out.bones.push_back(std::move(b));
    }
    need(r.count(o.max_bones) == nb, "Skeletal pose count disagrees with bone count");
    for (auto &b : out.bones) {
        control.check();
        for (auto &v : b.rotation)
            v = r.f64();
        for (auto &v : b.translation)
            v = r.f64();
        for (auto &v : b.scale)
            v = r.f64();
    }
    need(r.count(o.max_bones) == nb, "Skeletal name-index map count disagrees");
    std::set<std::uint32_t> mapped;
    for (unsigned i = 0; i < nb; ++i) {
        control.check();
        auto name = r.name(package.names);
        auto index = r.i32();
        need(index >= 0 && static_cast<unsigned>(index) < nb && out.bones[index].name == name &&
                 mapped.insert(static_cast<unsigned>(index)).second,
             "Skeletal name-index map disagrees with bone table");
    }
    MeshSkin validation;
    validation.bones = out.bones;
    validation.validate(control);
    need(r.boolean(), "Skeletal mesh native data is not cooked");
    Json mobile = nullptr;
    if (*o.keep_mobile_min_lod_setting) {
        auto v = r.i32();
        need(v >= 0 && v <= 255, "Invalid skeletal mobile LOD bias");
        mobile = v;
    }
    out.native_prefix = {
        {"objectGuidStored", guid},
        {"objectGuid", object_guid},
        {"importedBounds", bounds},
        {"hasVertexColors", colors},
        {"vertexColorSource", prop != properties.end() ? "stored-property" : "explicit-producer-setting"},
        {"keepMobileMinLODSettingOnDesktop", *o.keep_mobile_min_lod_setting},
        {"mobileMinLOD", mobile}};
    const auto nl = r.count(o.max_lods);
    budget.reserve(std::uint64_t(nl) * 4096);
    out.lods.reserve(nl);
    for (unsigned l = 0; l < nl; ++l) {
        control.checkpoint("cooked-skeletal-lod", l, nl);
        CookedSkeletalLod lod;
        lod.strip_flags = r.strip();
        supported((lod.strip_flags & ~3u) == 0, "Unknown skeletal LOD strip flags");
        lod.cooked_out = r.boolean();
        lod.inlined = r.boolean();
        lod.required_bones = bone_array(r, budget, nb);
        if (lod.cooked_out) {
            need(lod.inlined, "Cooked-out skeletal LOD must be inlined");
            lod.unavailable = "cooked-out";
        } else {
            const auto ns = r.count(o.max_sections);
            budget.add(budget.sections, ns, o.max_sections, "Skeletal LODs exceed section budget");
            budget.reserve(std::uint64_t(ns) * 1024);
            lod.sections.reserve(ns);
            for (unsigned s = 0; s < ns; ++s) {
                control.check();
                section(r, lod, package.names, budget, nb);
            }
            lod.active_bones = bone_array(r, budget, nb);
            const auto size = r.u32();
            supported(lod.inlined, "Streaming skeletal LOD payloads require another decoder");
            buffers(r, lod, budget, out, colors, size);
        }
        out.lods.push_back(std::move(lod));
    }
    const auto inlined = r.number(1), nonoptional = r.number(1);
    need(inlined <= nl && nonoptional <= nl && inlined <= nonoptional,
         "Invalid skeletal inline/nonoptional LOD counts");
    out.native_prefix["numInlinedLODs"] = inlined;
    out.native_prefix["numNonOptionalLODs"] = nonoptional;
    out.tail_offset = r.pos;
    out.tail_bytes = r.end - r.pos;
    out.tail_sha256 = detail::hash_range(bytes, r.pos, out.tail_bytes, control);
    out.parsed_native_bytes = r.pos - out.native_offset;
    control.check();
    return out;
}
} // namespace cyuediff
