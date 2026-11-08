#include <cyuediff/cloth.hpp>
#include <cmath>
#include <cstring>
#include <set>
namespace cyuediff {
namespace {
void need(bool v, const char *m) {
    if (!v)
        throw Error("cloth-layout", m);
}
std::uint32_t bits(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    return b;
}
template <std::size_t N> Json vector(const std::array<float, N> &v) {
    Json out = Json::array();
    for (auto f : v)
        out.push_back(std::isfinite(f) ? Json(f) : Json{{"nonFiniteBits", bits(f)}});
    return out;
}
} // namespace
void ClothMapping::validate() const {
    // Inactive entries can contain uninitialized producer bytes. Preserve them
    // without exposing a NaN as a usable coefficient or a semantic change.
    if (!simulated())
        return;
    for (const auto *v : {&position, &normal, &tangent})
        for (auto f : *v)
            need(std::isfinite(f), "Non-finite active cloth coefficient");
    need(std::isfinite(weight), "Non-finite active cloth weight");
}
Json ClothMapping::to_json(bool raw) const {
    if (!raw && !simulated())
        return {{"simulationFlag", source_indices[3]}};
    Json out = {{"position", vector(position)},
                {"normal", vector(normal)},
                {"tangent", vector(tangent)},
                {"sourceIndices", source_indices},
                {"weight", std::isfinite(weight) ? Json(weight) : Json{{"nonFiniteBits", bits(weight)}}}};
    if (raw)
        out["padding"] = padding;
    return out;
}
bool ClothMapping::stored_equal(const ClothMapping &b) const {
    for (unsigned i = 0; i < 4; ++i)
        if (bits(position[i]) != bits(b.position[i]) || bits(normal[i]) != bits(b.normal[i]) ||
            bits(tangent[i]) != bits(b.tangent[i]) || source_indices[i] != b.source_indices[i])
            return false;
    return bits(weight) == bits(b.weight) && padding == b.padding;
}
bool ClothSection::has_stored_data() const {
    if (asset_index != -1 || asset_lod != -1 || asset_guid != std::array<std::uint32_t, 4>{})
        return true;
    for (const auto &bias : lod_biases)
        if (!bias.empty())
            return true;
    return false;
}
void ClothSection::validate(std::uint32_t n, std::uint32_t max, const OperationControl &c) const {
    need(first_vertex <= n && vertices <= n - first_vertex, "Cloth section exceeds render vertices");
    need(asset_index >= -1 && asset_index <= 32767 && asset_lod >= -1,
         "Invalid stored cloth asset index/LOD");
    for (const auto &bias : lod_biases) {
        need(bias.empty() || (vertices && bias.size() % vertices == 0 && bias.size() / vertices <= max),
             "Cloth bias does not partition into bounded render vertex influences");
        for (std::size_t i = 0; i < bias.size(); ++i) {
            if (!(i & 1023))
                c.check();
            bias[i].validate();
        }
    }
}
Json ClothSection::to_json(bool include, const OperationControl &c) const {
    Json out = {{"section", section},
                {"firstVertex", first_vertex},
                {"vertices", vertices},
                {"assetIndex", asset_index},
                {"assetLOD", asset_lod},
                {"assetGuidWords", asset_guid},
                {"mappingCounts", Json::array()}};
    if (include)
        out["mappings"] = Json::array();
    for (const auto &bias : lod_biases) {
        out["mappingCounts"].push_back(bias.size());
        if (include) {
            Json rows = Json::array();
            for (std::size_t i = 0; i < bias.size(); ++i) {
                if (!(i & 1023))
                    c.check();
                rows.push_back(bias[i].to_json(true));
            }
            out["mappings"].push_back(std::move(rows));
        }
    }
    return out;
}
Json ClothBufferIndex::to_json() const {
    return Json::array({first_vertex, offset, lod_bias_stride});
}
void MeshCloth::validate(std::uint32_t n, std::uint32_t max, const OperationControl &c) const {
    bool expected = false;
    std::set<std::uint32_t> ids;
    for (const auto &s : sections) {
        c.check();
        need(bool(s) && ids.insert(s->section).second, "Absent or duplicate cloth section");
        s->validate(n, max, c);
        expected = expected || s->has_render_mapping();
    }
    need(buffer_stored == expected, "Cloth buffer presence disagrees with section mapping");
    if (!buffer_stored) {
        need(buffer.empty() && index_mapping.empty(), "Unowned cloth buffer data");
        return;
    }
    need(index_mapping.size() == sections.size(), "Cloth index mapping does not cover section table");
    std::uint64_t offset = 0;
    for (std::size_t si = 0; si < sections.size(); ++si) {
        const auto &s = *sections[si];
        const auto &index = index_mapping[si];
        if (!s.has_render_mapping()) {
            need(!index.first_vertex && !index.offset && !index.lod_bias_stride,
                 "Unclothed section has a nonempty cloth index mapping");
            continue;
        }
        need(index.first_vertex == s.first_vertex && index.offset == offset &&
                 index.lod_bias_stride == s.lod_biases[0].size(),
             "Cloth index mapping disagrees with section window");
        for (const auto &bias : s.lod_biases) {
            need(bias.size() == index.lod_bias_stride && offset <= buffer.size() &&
                     bias.size() <= buffer.size() - offset,
                 "Cloth LOD bias stride exceeds buffer");
            for (const auto &m : bias) {
                if (!(offset & 1023))
                    c.check();
                need(m.stored_equal(buffer[std::size_t(offset++)]),
                     "Cloth buffer record differs from section mapping");
            }
        }
    }
    need(offset == buffer.size(), "Trailing unowned cloth buffer records");
}
const ClothSection &MeshCloth::section(std::uint32_t id) const {
    for (const auto &s : sections)
        if (s && s->section == id)
            return *s;
    throw Error("cloth-selection", "Selected cloth section is absent");
}
Json MeshCloth::vertex(std::uint32_t id, std::uint32_t bi, std::uint32_t vertex, bool raw,
                       const OperationControl &c) const {
    c.check();
    const auto &s = section(id);
    if (bi >= s.lod_biases.size() || vertex < s.first_vertex || vertex - s.first_vertex >= s.vertices)
        throw Error("cloth-selection", "Selected cloth LOD bias or render vertex is absent");
    const auto &bias = s.lod_biases[bi];
    need(s.vertices && bias.size() % s.vertices == 0, "Cloth mapping cardinality is invalid");
    const auto count = bias.size() / s.vertices;
    Json out = Json::array();
    for (std::size_t i = 0; i < count; ++i) {
        c.check();
        out.push_back(bias[std::size_t(vertex - s.first_vertex) * count + i].to_json(raw));
    }
    return out;
}
Json MeshCloth::semantic_snapshot(const OperationControl &c) const {
    Json out = {{"schema", "cyuediff.cloth/1"},
                {"scope", "stored-cloth-deformer-mapping"},
                {"sections", Json::object()}};
    for (const auto &s : sections) {
        c.check();
        if (!s->has_stored_data())
            continue;
        auto &j = out["sections"][std::to_string(s->section)];
        j = {{"assetIndex", s->asset_index},
             {"assetLOD", s->asset_lod},
             {"assetGuidWords", s->asset_guid},
             {"lodBiases", Json::object()}};
        for (std::size_t bi = 0; bi < s->lod_biases.size(); ++bi) {
            const auto &bias = s->lod_biases[bi];
            auto &vertices = j["lodBiases"][std::to_string(bi)] = Json::object();
            const auto count = s->vertices ? bias.size() / s->vertices : 0;
            if (!count)
                continue;
            for (unsigned vi = 0; vi < s->vertices; ++vi) {
                if (!(vi & 255))
                    c.check();
                auto &v = vertices[std::to_string(s->first_vertex + vi)] = Json::array();
                for (std::size_t k = 0; k < count; ++k)
                    v.push_back(bias[std::size_t(vi) * count + k].to_json());
            }
        }
    }
    return out;
}
Json MeshCloth::to_json(bool include, const OperationControl &c) const {
    Json out = {{"schema", "cyuediff.cloth/1"},
                {"scope", "stored-cloth-deformer-mapping"},
                {"simulationMeshBoundsVerified", false},
                {"bufferStored", buffer_stored},
                {"bufferInfo", buffer_info},
                {"sections", Json::array()},
                {"indexMapping", Json::array()}};
    for (const auto &s : sections)
        out["sections"].push_back(s->to_json(include, c));
    for (const auto &i : index_mapping) {
        c.check();
        out["indexMapping"].push_back(i.to_json());
    }
    if (include) {
        out["buffer"] = Json::array();
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            if (!(i & 1023))
                c.check();
            out["buffer"].push_back(buffer[i].to_json(true));
        }
    }
    return out;
}
} // namespace cyuediff
