#include <cyuediff/cooked_skeletal.hpp>
#include <cyuediff/string_encoding.hpp>
#include "archive.hpp"
#include "zen_bulk.hpp"
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
    std::string string(const CookedSkeletalOptions &o) {
        const auto n = i32();
        if (!n)
            return {};
        const auto chars = n < 0 ? std::uint64_t(-std::int64_t(n)) : std::uint64_t(n);
        limit(chars * (n < 0 ? 2 : 1) <= o.max_profile_name_bytes, "Profile FString exceeds byte budget");
        range(chars * (n < 0 ? 2 : 1));
        std::string s;
        if (n > 0) {
            for (std::uint64_t i = 0; i < chars; ++i) {
                auto v = number(1);
                if (i + 1 == chars)
                    need(v == 0, "Unterminated profile FString", pos - 1);
                else {
                    need(v != 0, "Embedded null in profile FString", pos - 1);
                    s += char(v);
                }
            }
            auto encoding = o.narrow_string_encoding;
            if (encoding == NarrowStringEncoding::unspecified)
                encoding = NarrowStringEncoding::ascii;
            return decode_narrow_string(s, encoding, o.max_profile_name_bytes, o.control);
        }
        auto append = [&](std::uint32_t cp) {
            if (cp < 128)
                s += char(cp);
            else if (cp < 2048) {
                s += char(192 | (cp >> 6));
                s += char(128 | (cp & 63));
            } else if (cp < 65536) {
                s += char(224 | (cp >> 12));
                s += char(128 | ((cp >> 6) & 63));
                s += char(128 | (cp & 63));
            } else {
                s += char(240 | (cp >> 18));
                s += char(128 | ((cp >> 12) & 63));
                s += char(128 | ((cp >> 6) & 63));
                s += char(128 | (cp & 63));
            }
        };
        for (std::uint64_t i = 0; i < chars; ++i) {
            o.control.check();
            auto cp = std::uint32_t(number(2));
            if (i + 1 == chars) {
                need(cp == 0, "Unterminated profile UTF16 FString", pos - 2);
                break;
            }
            need(cp != 0, "Embedded null in profile UTF16 FString", pos - 2);
            if (cp >= 0xd800 && cp <= 0xdbff) {
                need(i + 2 < chars, "Truncated profile UTF16 surrogate", pos - 2);
                auto low = number(2);
                ++i;
                need(low >= 0xdc00 && low <= 0xdfff, "Invalid profile UTF16 surrogate", pos - 2);
                cp = 65536 + ((cp - 0xd800) << 10) + std::uint32_t(low - 0xdc00);
            } else
                need(cp < 0xdc00 || cp > 0xdfff, "Invalid profile UTF16 surrogate", pos - 2);
            append(cp);
            limit(s.size() <= o.max_profile_name_bytes, "Decoded profile FString exceeds string budget");
        }
        return s;
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
    std::uint64_t profiles = 0, overrides = 0, morphs = 0, morph_batches = 0, morph_deltas = 0,
                  cloth_mappings = 0, duplicate_indices = 0, duplicate_links = 0;
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
ClothMapping cloth_mapping(Cursor &r) {
    ClothMapping m;
    for (auto *v : {&m.position, &m.normal, &m.tangent})
        for (auto &f : *v) {
            const auto bits = r.u32();
            std::memcpy(&f, &bits, 4);
        }
    for (auto &v : m.source_indices)
        v = static_cast<std::uint16_t>(r.number(2));
    const auto bits = r.u32();
    std::memcpy(&m.weight, &bits, 4);
    m.padding = r.u32();
    m.validate();
    return m;
}
std::vector<ClothMapping> cloth_mappings(Cursor &r, Budget &b, bool bulk) {
    const auto n = bulk ? r.array(64, INT32_MAX) : r.count(INT32_MAX);
    r.range(std::uint64_t(n) * 64);
    b.add(b.cloth_mappings, n, b.o.max_cloth_mappings, "Cloth mappings exceed aggregate budget");
    b.reserve(std::uint64_t(n) * (sizeof(ClothMapping) + (b.o.geometry ? 256 : 0)));
    std::vector<ClothMapping> out;
    out.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        if (!(i & 1023))
            b.o.control.checkpoint("cooked-cloth-mappings", i, n);
        out.push_back(cloth_mapping(r));
    }
    return out;
}
std::vector<ClothBufferIndex> cloth_indices(Cursor &r, Budget &b) {
    const auto n = r.count(b.o.max_sections);
    r.range(std::uint64_t(n) * 12);
    b.reserve(std::uint64_t(n) * sizeof(ClothBufferIndex));
    std::vector<ClothBufferIndex> out;
    out.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        b.o.control.check();
        out.push_back({r.u32(), r.u32(), r.u32()});
    }
    return out;
}
bool has_cloth_buffer(const CookedSkeletalLod &lod) {
    for (const auto &s : lod.sections)
        if (s.cloth && s.cloth->has_render_mapping())
            return true;
    return false;
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
    auto cloth = std::make_shared<ClothSection>();
    cloth->section = static_cast<std::uint32_t>(lod.sections.size());
    cloth->first_vertex = s.first_vertex;
    b.reserve(sizeof(ClothSection) + std::uint64_t(cloth_lods) * sizeof(std::vector<ClothMapping>));
    for (unsigned i = 0; i < cloth_lods; ++i) {
        b.o.control.check();
        cloth->lod_biases.push_back(cloth_mappings(r, b, false));
    }
    s.bone_map = bone_array(r, b, bones);
    s.vertices = r.u32();
    auto maxinf = r.i32();
    need(maxinf >= 0, "Negative skeletal section influence count");
    s.max_influences = static_cast<std::uint32_t>(maxinf);
    limit(s.vertices <= b.o.max_vertices && s.max_influences <= b.o.max_influences,
          "Skeletal section exceeds vertex or influence budget");
    cloth->vertices = s.vertices;
    const auto asset_bits = r.number(2);
    cloth->asset_index = asset_bits & 0x8000 ? int(asset_bits) - 65536 : int(asset_bits);
    for (auto &v : cloth->asset_guid)
        v = r.u32();
    cloth->asset_lod = r.i32();
    cloth->validate(b.o.max_vertices, b.o.max_cloth_influences, b.o.control);
    s.cloth = std::move(cloth);
    if ((strips & 1) == 0) {
        auto overlap = std::make_shared<MeshSectionOverlaps>();
        overlap->section = static_cast<std::uint32_t>(lod.sections.size());
        overlap->first_vertex = s.first_vertex;
        overlap->vertices = s.vertices;
        const auto begin = r.pos;
        const auto count = r.count(b.o.max_duplicate_indices);
        need(count != 0, "Overlap array has no required native allocation dummy");
        r.range(std::uint64_t(count) * 4);
        b.add(b.duplicate_indices, count, b.o.max_duplicate_indices,
              "Skeletal LODs exceed duplicate index budget");
        b.reserve(sizeof(MeshSectionOverlaps) + std::uint64_t(count) * 48);
        overlap->indices.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            b.o.control.check();
            overlap->indices.push_back(r.u32());
        }
        const auto windows = r.count(std::max(1u, b.o.max_vertices));
        need(windows == std::max(1u, s.vertices), "Overlap windows disagree with section vertices");
        r.range(std::uint64_t(windows) * 8);
        b.reserve(std::uint64_t(windows) * 128);
        overlap->windows.reserve(windows);
        for (unsigned i = 0; i < windows; ++i) {
            b.o.control.check();
            const auto length = r.u32(), offset = r.u32();
            if (i < s.vertices) { // A zero-vertex section's allocation dummy is never indexed.
                b.add(b.duplicate_links, length, b.o.max_duplicate_indices,
                      "Skeletal LODs exceed resolved duplicate index budget");
                b.reserve(std::uint64_t(length) * 64);
            }
            overlap->windows.push_back({length, offset});
        }
        overlap->validate(b.o.max_vertices, b.o.control, b.o.max_duplicate_indices);
        overlap->serialized_sha256 = detail::hash_range(r.bytes, begin, r.pos - begin, b.o.control);
        s.overlaps = std::move(overlap);
    }
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
std::string folded(std::string name) {
    for (auto &c : name)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return name;
}
Json availability(Cursor &r, Budget &b, bool colors, const std::vector<std::string> &names,
                  const CookedSkeletalLod &lod) {
    const auto &o = b.o;
    const auto index_bytes = r.number(1);
    need(index_bytes == 2 || index_bytes == 4, "Invalid cached skeletal index width");
    const auto indices = r.i32();
    need(indices >= 0, "Negative cached skeletal index count");
    limit(static_cast<std::uint32_t>(indices) <= o.max_indices, "Cached skeletal index count exceeds budget");
    const auto uvs = r.u32(), vertices = r.u32();
    const auto full = r.boolean(), high = r.boolean();
    const auto position_stride = r.u32(), position_vertices = r.u32();
    const auto color_stride = r.u32(), color_vertices = r.u32();
    const auto variable = r.boolean();
    const auto maxinf = r.u32(), weights = r.u32(), weight_vertices = r.u32();
    const auto wide_bone = r.boolean(), wide_weight = r.boolean();
    const auto lookup_vertices = r.u32();
    limit(uvs <= o.max_uv_channels && vertices <= o.max_vertices && position_vertices <= o.max_vertices &&
              color_vertices <= o.max_vertices && weight_vertices <= o.max_vertices &&
              lookup_vertices <= o.max_vertices && maxinf <= o.max_influences &&
              weights <= std::uint64_t(o.max_vertices) * o.max_influences,
          "Cached skeletal vertex/skin dimensions exceed budget");
    need(position_stride == 12 && position_vertices == vertices && weight_vertices == vertices,
         "Cached skeletal vertex dimensions disagree");
    need((!color_vertices || (color_stride == 4 && color_vertices == vertices)) &&
             (!colors || color_vertices == vertices),
         "Cached skeletal color dimensions disagree");
    need(lookup_vertices == (variable ? vertices : 0) &&
             (variable || std::uint64_t(vertices) * maxinf == weights),
         "Cached skeletal skin dimensions disagree");
    Json cloth = nullptr;
    if (has_cloth_buffer(lod)) {
        const auto cloth_index_map = cloth_indices(r, b);
        const auto stride = r.u32(), count = r.u32();
        need(stride == 64, "Cached cloth stride disagrees with profile");
        limit(count <= o.max_cloth_mappings, "Cached cloth records exceed budget");
        Json rows = Json::array();
        for (const auto &i : cloth_index_map)
            rows.push_back(i.to_json());
        cloth = {{"stride", stride}, {"vertices", count}, {"indexMapping", rows}};
    }
    const auto count = r.count(o.max_weight_profiles);
    Json profiles = Json::array();
    std::set<std::string> keys;
    for (unsigned i = 0; i < count; ++i) {
        o.control.check();
        auto name = r.name(names);
        need(!name.empty() && keys.insert(folded(name)).second, "Empty or duplicate cached weight profile");
        b.reserve(128 + name.size());
        profiles.push_back(std::move(name));
    }
    return {{"cloth", cloth},
            {"weightProfiles", profiles},
            {"vertices", vertices},
            {"indices", indices},
            {"indexBytes", index_bytes},
            {"uvChannels", uvs},
            {"fullPrecisionUV", full},
            {"highPrecisionTangents", high},
            {"colorsStored", colors},
            {"variableInfluences", variable},
            {"maxInfluences", maxinf},
            {"influences", weights},
            {"boneIndexBytes", wide_bone ? 2 : 1},
            {"boneWeightBytes", wide_weight ? 2 : 1},
            {"lookupVertices", lookup_vertices},
            {"positionStride", position_stride},
            {"colorStride", color_stride},
            {"colorVertices", color_vertices}};
}
void match_availability(const Json &actual, const Json &expected) {
    need(actual.at("cloth") == expected.at("cloth"),
         "Cached cloth dimensions/index mapping disagree with loaded buffers");
    auto loaded_names = actual.at("weightProfiles").get<std::vector<std::string>>();
    auto cached_names = expected.at("weightProfiles").get<std::vector<std::string>>();
    std::sort(loaded_names.begin(), loaded_names.end());
    std::sort(cached_names.begin(), cached_names.end());
    need(loaded_names == cached_names, "Skeletal cached profile names disagree with loaded buffers");
    for (const char *key : {"vertices", "indices", "indexBytes", "uvChannels", "fullPrecisionUV",
                            "highPrecisionTangents", "colorsStored", "variableInfluences", "maxInfluences",
                            "influences", "boneIndexBytes", "boneWeightBytes", "lookupVertices"})
        need(actual.at(key) == expected.at(key),
             "Skeletal availability disagrees with loaded render buffers");
}
void buffers(Cursor &r, CookedSkeletalLod &lod, Budget &b, const CookedSkeletalMesh &mesh, bool colors,
             std::uint32_t stored_size, const std::vector<std::string> &names) {
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
    std::shared_ptr<MeshCloth> cloth;
    for (const auto &section : lod.sections)
        if (section.cloth && section.cloth->has_stored_data()) {
            cloth = std::make_shared<MeshCloth>();
            break;
        }
    Json cloth_availability = nullptr;
    if (cloth) {
        b.reserve(sizeof(MeshCloth) + lod.sections.size() * sizeof(std::shared_ptr<const ClothSection>));
        for (const auto &s : lod.sections)
            cloth->sections.push_back(s.cloth);
        if (has_cloth_buffer(lod)) {
            cloth->buffer_stored = true;
            const auto start = r.pos;
            need(r.strip() == 0, "Unknown cloth buffer strip flags");
            const auto records_at = r.pos + 8;
            cloth->buffer = cloth_mappings(r, b, true);
            cloth->index_mapping = cloth_indices(r, b);
            Json mapping_rows = Json::array();
            for (const auto &i : cloth->index_mapping)
                mapping_rows.push_back(i.to_json());
            cloth_availability = {
                {"stride", 64}, {"vertices", cloth->buffer.size()}, {"indexMapping", mapping_rows}};
            cloth->buffer_info = {
                {"stride", 64},
                {"records", cloth->buffer.size()},
                {"recordBytes", std::uint64_t(cloth->buffer.size()) * 64},
                {"recordSha256", detail::hash_range(r.bytes, records_at,
                                                    std::uint64_t(cloth->buffer.size()) * 64, b.o.control)},
                {"serializedBytes", r.pos - start},
                {"serializedSha256", detail::hash_range(r.bytes, start, r.pos - start, b.o.control)}};
        }
        cloth->validate(n, b.o.max_cloth_influences, b.o.control);
        lod.cloth = cloth;
        if (g)
            g->cloth = cloth;
    }
    const auto profile_count = r.count(b.o.max_weight_profiles);
    b.add(b.profiles, profile_count, b.o.max_weight_profiles, "Skeletal LODs exceed profile budget");
    b.reserve(std::uint64_t(profile_count) * 1024);
    std::set<std::string> profile_names;
    Json profile_list = Json::array();
    std::uint64_t profile_name_bytes = 0;
    for (unsigned pi = 0; pi < profile_count; ++pi) {
        b.o.control.checkpoint("cooked-skeletal-profiles", pi, profile_count);
        const auto start = r.pos;
        CookedSkeletalWeightProfile profile;
        profile.name = lod.inlined ? r.name(names) : r.string(b.o);
        profile_name_bytes += r.pos - start;
        limit(profile.name.size() <= b.o.max_profile_name_bytes, "Profile name exceeds byte budget");
        need(!profile.name.empty() && profile_names.insert(folded(profile.name)).second,
             "Empty or duplicate cooked weight profile");
        // Reserve the base stream identity even when an authored profile is named default.
        profile.skin_profile_key = "profile:" + profile.name;
        b.reserve(256 + 4 * profile.name.size());
        const auto ids_size = r.count(std::numeric_limits<std::int32_t>::max());
        r.range(ids_size);
        const auto ids = r.pos;
        r.pos += ids_size;
        const auto weights_size = r.count(std::numeric_limits<std::int32_t>::max());
        r.range(weights_size);
        const auto weights_start = r.pos;
        r.pos += weights_size;
        const auto k = profile.influences_per_vertex = static_cast<std::uint32_t>(r.number(1));
        limit(k <= b.o.max_influences, "Profile influence count exceeds budget");
        need(k <= maxinf, "Profile influence count exceeds base stream maximum");
        const std::uint64_t id_stride = std::uint64_t(k) * bone_bytes,
                            weight_stride = std::uint64_t(k) * weight_bytes;
        need(k ? ids_size % id_stride == 0 && weights_size % weight_stride == 0 &&
                     ids_size / id_stride == weights_size / weight_stride
               : ids_size == 0 && weights_size == 0,
             "Profile record byte counts disagree");
        const auto records = k ? ids_size / id_stride : 0;
        std::map<std::uint32_t, std::vector<MeshInfluence>> *resolved_profile = nullptr;
        if (skin) {
            auto &destination = skin->profiles[profile.skin_profile_key];
            const auto &base = skin->profiles["default"];
            for (const auto &v : base) {
                b.o.control.check();
                b.reserve(128 + v.second.size() * sizeof(MeshInfluence));
                destination.emplace(v.first, v.second);
            }
            resolved_profile = &destination;
        }
        const auto entries = r.count(b.o.max_profile_overrides);
        b.add(b.overrides, entries, b.o.max_profile_overrides,
              "Skeletal LODs exceed profile override budget");
        limit(records <= b.o.max_profile_overrides, "Skeletal profile records exceed budget");
        for (unsigned vi = 0; vi < entries; ++vi) {
            b.o.control.checkpoint("cooked-skeletal-profile-weights", vi, entries);
            const auto vertex = r.u32(), record = r.u32();
            b.reserve(128);
            need(vertex < n && record < records && profile.vertex_to_record.emplace(vertex, record).second,
                 "Profile has an absent or duplicate vertex/record");
            need(owners[vertex] >= 0, "Profile vertex has no owning section");
            const auto &section = lod.sections[owners[vertex]];
            auto count = maxinf;
            std::uint64_t offset = std::uint64_t(vertex) * maxinf * (bone_bytes + weight_bytes);
            if (variable) {
                Cursor v{r.bytes, lookup + std::uint64_t(vertex) * 4, lookup + std::uint64_t(vertex + 1) * 4};
                const auto packed = v.u32();
                offset = packed >> 8;
                count = packed & 255;
            }
            need(k <= count && offset <= raw_bytes &&
                     std::uint64_t(count) * (bone_bytes + weight_bytes) <= raw_bytes - offset,
                 "Profile replacement exceeds stored vertex influence slots");
            b.reserve(128 + std::uint64_t(k) * sizeof(MeshInfluence) + std::uint64_t(count) * 48);
            Cursor bi{r.bytes, raw + offset, raw + raw_bytes},
                wi{r.bytes, raw + offset + std::uint64_t(count) * bone_bytes, raw + raw_bytes},
                oi{r.bytes, ids + record * id_stride, ids + ids_size},
                ow{r.bytes, weights_start + record * weight_stride, weights_start + weights_size};
            auto &replacement = profile.overrides[vertex];
            replacement.reserve(k);
            std::vector<MeshInfluence> resolved;
            std::set<std::uint32_t> positive;
            for (unsigned j = 0; j < count; ++j) {
                const auto base_bone = static_cast<std::uint32_t>(bi.number(bone_bytes));
                const auto base_weight = static_cast<std::uint16_t>(wi.number(weight_bytes));
                const auto local = j < k ? static_cast<std::uint32_t>(oi.number(bone_bytes)) : base_bone;
                auto weight = j < k ? static_cast<std::uint16_t>(ow.number(weight_bytes)) : base_weight;
                need(local < section.bone_map.size(), "Profile references an absent section bone");
                if (!wide_weight)
                    weight = static_cast<std::uint16_t>(weight * 257u);
                const MeshInfluence value{section.bone_map[local], weight};
                if (j < k)
                    replacement.push_back(value);
                if (weight) {
                    need(positive.insert(value.bone).second, "Duplicate nonzero profile influence bone");
                    resolved.push_back(value);
                }
            }
            need(positive.size() <= section.max_influences, "Profile exceeds section influence maximum");
            if (resolved_profile)
                (*resolved_profile)[vertex] = std::move(resolved);
        }
        profile.buffers = {
            {"records", records},
            {"overriddenVertices", entries},
            {"boneIndexBytes", bone_bytes},
            {"boneWeightBytes", weight_bytes},
            {"boneIDsBytes", ids_size},
            {"boneWeightsBytes", weights_size},
            {"boneIDsSha256", detail::hash_range(r.bytes, ids, ids_size, b.o.control)},
            {"boneWeightsSha256", detail::hash_range(r.bytes, weights_start, weights_size, b.o.control)},
            {"serializedBytes", r.pos - start},
            {"serializedSha256", detail::hash_range(r.bytes, start, r.pos - start, b.o.control)}};
        profile_list.push_back(profile.name);
        lod.weight_profiles.push_back(std::move(profile));
    }
    auto ray_bytes = r.count(std::numeric_limits<std::int32_t>::max());
    r.range(ray_bytes);
    Json ray = {{"bytes", ray_bytes},
                {"sha256", detail::hash_range(r.bytes, r.pos, ray_bytes, b.o.control)},
                {"decoded", false}};
    r.pos += ray_bytes;
    if (r.boolean()) {
        CompressedMorphOptions options;
        options.profile = MorphCompressionProfile::unreal_5_3_windows;
        options.control = b.o.control;
        options.max_vertices = b.o.max_vertices;
        options.max_morphs = b.o.max_morph_targets - static_cast<std::uint32_t>(b.morphs);
        options.max_batches = b.o.max_morph_batches - static_cast<std::uint32_t>(b.morph_batches);
        options.max_deltas = b.o.max_morph_deltas - b.morph_deltas;
        options.max_data_bytes = b.o.max_read_bytes;
        options.max_output_bytes = b.o.max_output_bytes - b.output;
        auto buffer = std::make_shared<CompressedMorphBuffer>(
            CompressedMorphBuffer::from_native_window(r.bytes, r.pos, r.end, n, options));
        r.pos += buffer->consumed_bytes();
        b.reserve(buffer->accounted_output_bytes() + 512);
        b.morphs += buffer->morph_count();
        b.morph_batches += buffer->batch_count();
        for (std::size_t mi = 0; mi < buffer->morph_count(); ++mi) {
            b.morph_deltas += buffer->delta_count(mi);
            if (g)
                b.reserve(buffer->delta_count(mi) * 256 + 1024);
        }
        if (mesh.morph_binding_unavailable.empty())
            need(mesh.morph_names.size() == buffer->morph_count(),
                 "Mapped MorphTargets count disagrees with native compression table");
        for (const auto &name : mesh.morph_names)
            b.reserve(name.size() + 64);
        lod.morphs =
            std::make_shared<MeshRenderMorphs>(buffer, mesh.morph_names, mesh.morph_binding_unavailable);
        if (g)
            g->render_morphs = lod.morphs;
    }
    supported(r.count(b.o.max_sections) == 0, "Cooked skeletal vertex attributes require another decoder");
    // The native counting archive inherits FArchive's no-op FName operator.
    // Physical inline FNames and streamed FMemoryArchive FStrings are excluded
    // from BuffersSize, even though both are present in the actual payload.
    need(r.pos - begin >= profile_name_bytes && r.pos - begin - profile_name_bytes == stored_size,
         "Skeletal counted buffer size disagrees with payload");
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
        auto overlaps = std::make_shared<MeshVertexOverlaps>();
        for (unsigned si = 0; si < lod.sections.size(); ++si) {
            const auto &section = lod.sections[si];
            if (section.overlaps) {
                section.overlaps->validate(n, b.o.control, b.o.max_duplicate_indices);
                overlaps->sections.push_back(section.overlaps);
            } else
                overlaps->unavailable_sections.push_back(si);
        }
        if (!overlaps->sections.empty())
            g->vertex_overlaps = overlaps;
        g->skin = skin;
        g->bounds = {{"min", low}, {"max", high}};
        g->attributes = {{"source", "cooked-skeletal-render-lod"},
                         {"colorsStored", colors},
                         {"weightEncoding", wide_weight ? "uint16" : "uint8-expanded-to-uint16"},
                         {"weightNormalizationApplied", false},
                         {"zeroWeightPaddingInSemanticSkin", false}};
        lod.geometry = g;
    }
    lod.buffers = {{"cloth", cloth_availability},
                   {"weightProfiles", profile_list},
                   {"vertices", n},
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
                   {"serializedBytes", r.pos - begin},
                   {"declaredBuffersSize", stored_size},
                   {"profileNameBytes", profile_name_bytes}};
}
} // namespace
Json CookedSkeletalWeightProfile::to_json(bool include) const {
    Json out = {{"name", name},
                {"skinProfileKey", skin_profile_key},
                {"influencesPerVertex", influences_per_vertex},
                {"buffers", buffers}};
    if (include) {
        out["vertexToRecord"] = Json::object();
        out["overrides"] = Json::object();
        for (const auto &v : vertex_to_record)
            out["vertexToRecord"][std::to_string(v.first)] = v.second;
        for (const auto &v : overrides) {
            auto &weights = out["overrides"][std::to_string(v.first)] = Json::array();
            for (const auto &w : v.second)
                weights.push_back({{"bone", w.bone}, {"rawWeight", w.raw_weight}});
        }
    }
    return out;
}
Json CookedSkeletalSection::to_json(bool include) const {
    Json out = {{"material", material},        {"firstIndex", first_index}, {"triangles", triangles},
                {"firstVertex", first_vertex}, {"vertices", vertices},      {"maxInfluences", max_influences},
                {"boneMap", bone_map},         {"fields", fields}};
    if (overlaps)
        out["vertexOverlaps"] = overlaps->to_json(include);
    if (cloth && cloth->has_stored_data())
        out["cloth"] = cloth->to_json();
    return out;
}
Json CookedSkeletalLod::to_json(bool include) const {
    Json s = Json::array();
    for (const auto &v : sections)
        s.push_back(v.to_json(include));
    Json j = {{"cookedOut", cooked_out},     {"inlined", inlined},
              {"stripFlags", strip_flags},   {"requiredBones", required_bones},
              {"activeBones", active_bones}, {"sections", s},
              {"buffers", buffers},          {"bulk", bulk},
              {"unavailable", unavailable}};
    j["weightProfiles"] = Json::array();
    for (const auto &profile : weight_profiles)
        j["weightProfiles"].push_back(profile.to_json(include));
    if (cloth)
        j["cloth"] = cloth->to_json(include);
    if (morphs)
        j["morphs"] = morphs->to_json(include);
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
            {"morphBinding", {{"names", morph_names}, {"unavailable", morph_binding_unavailable}}},
            {"bones", b},
            {"lods", l},
            {"tail",
             {{"offset", tail_offset}, {"bytes", tail_bytes}, {"sha256", tail_sha256}, {"decoded", false}}}};
}
CookedSkeletalMesh read_zen_skeletal_mesh(const Container &container, std::size_t entry,
                                          const ZenReadOptions &zen, const ContainerReadOptions &read,
                                          const CookedSkeletalOptions &input, std::size_t selected) {
    auto o = input;
    if (o.narrow_string_encoding == NarrowStringEncoding::unspecified && zen.schema_context)
        o.narrow_string_encoding = zen.schema_context->narrow_string_encoding;
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
    const auto morph_property = properties.find("/Script/Engine.SkeletalMesh.MorphTargets");
    if (morph_property == properties.end())
        out.morph_binding_unavailable =
            "MorphTargets property is omitted; its producer defaults are unresolved";
    else {
        need(morph_property->value("type", std::string{}) == "ArrayProperty" &&
                 morph_property->contains("value") && morph_property->at("value").is_array(),
             "Invalid mapped MorphTargets property");
        const auto &refs = morph_property->at("value");
        limit(refs.size() <= o.max_morph_targets, "Mapped morph target inventory exceeds budget");
        std::map<std::string, const ZenExport *> targets;
        for (const auto &ref : refs)
            if (ref.is_object() && ref.value("kind", std::string{}) == "export" && ref.contains("path") &&
                ref.at("path").is_string()) {
                const auto path = ref.at("path").get<std::string>();
                budget.reserve(128 + path.size());
                targets.emplace(path, nullptr);
            }
        for (const auto &candidate : package.exports) {
            control.check();
            const auto found = targets.find(candidate.path);
            if (found != targets.end())
                found->second = &candidate;
        }
        std::set<std::string> seen;
        for (const auto &ref : refs) {
            control.check();
            const ZenExport *target = nullptr;
            if (ref.is_object() && ref.value("kind", std::string{}) == "export" && ref.contains("path") &&
                ref.at("path").is_string()) {
                const auto found = targets.find(ref.at("path").get<std::string>());
                if (found != targets.end())
                    target = found->second;
            }
            if (!target || target->class_name != "/Script/Engine.MorphTarget" ||
                target->outer_index != selected || (target->flags & 0x30)) {
                out.morph_binding_unavailable = "MorphTargets includes an unresolved, external or custom "
                                                "morph; native target names are unknown";
                out.morph_names.clear();
                break;
            }
            need(!target->name.empty() && seen.insert(folded(target->name)).second,
                 "Empty or duplicate mapped MorphTarget identity");
            budget.reserve(128 + target->name.size());
            out.morph_names.push_back(target->name);
        }
    }
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
    detail::ZenBulkReader bulk_reader(container, package, member, bytes, reads, o.max_read_bytes,
                                      "cooked-skeletal");
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
            if (lod.inlined)
                buffers(r, lod, budget, out, colors, size, package.names);
            else {
                const auto index = r.u32();
                auto window = bulk_reader.describe(index, r.pos, r.end);
                Json cached = nullptr;
                if (window.bytes)
                    cached = availability(r, budget, colors, package.names, lod);
                lod.unavailable = window.unavailable;
                bulk_reader.load(window);
                if (lod.unavailable.empty()) {
                    Cursor payload{*window.data, window.offset, window.offset + window.bytes};
                    buffers(payload, lod, budget, out, colors, size, package.names);
                    need(payload.pos == payload.end, "Trailing bytes in skeletal streamed LOD payload");
                    match_availability(lod.buffers, cached);
                }
                lod.bulk = std::move(window.metadata);
                lod.bulk["availability"] = std::move(cached);
            }
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
