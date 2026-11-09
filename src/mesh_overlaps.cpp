#include <cyuediff/mesh_overlaps.hpp>
#include <algorithm>
#include <limits>
namespace cyuediff {
namespace {
void need(bool value, const char *message) {
    if (!value)
        throw Error("mesh-overlaps-layout", message);
}
} // namespace
void MeshSectionOverlaps::validate(std::uint32_t render_vertices, const OperationControl &control,
                                   std::uint64_t max_resolved_indices) const {
    control.check();
    need(first_vertex <= render_vertices && vertices <= render_vertices - first_vertex,
         "Overlap section exceeds render vertices");
    need(windows.size() == std::max(1u, vertices) && !indices.empty(),
         "Overlap buffer dimensions disagree with section");
    std::uint64_t resolved = 0;
    for (std::uint32_t vi = 0; vi < vertices; ++vi) {
        control.check();
        const auto &w = windows[vi];
        if (resolved > max_resolved_indices || w.length > max_resolved_indices - resolved)
            throw Error("mesh-overlaps-limit", "Resolved overlap indices exceed budget");
        resolved += w.length;
        need(w.offset <= indices.size() && w.length <= indices.size() - w.offset,
             "Overlap neighbor window exceeds stored data");
        for (std::uint32_t i = 0; i < w.length; ++i) {
            if ((i & 4095) == 0)
                control.check();
            need(indices[w.offset + i] < vertices, "Overlap neighbor exceeds section-local vertices");
        }
    }
}
std::vector<std::uint32_t> MeshSectionOverlaps::neighbors(std::uint32_t vertex,
                                                          const OperationControl &control) const {
    control.check();
    if (vertex < first_vertex || std::uint64_t(vertex) >= std::uint64_t(first_vertex) + vertices)
        throw Error("mesh-overlaps-selection", "Render vertex is absent from overlap section");
    const auto local = vertex - first_vertex;
    need(local < windows.size(), "Overlap vertex has no window");
    const auto &w = windows[local];
    need(w.offset <= indices.size() && w.length <= indices.size() - w.offset,
         "Overlap neighbor window exceeds stored data");
    std::vector<std::uint32_t> out;
    out.reserve(w.length);
    for (std::uint32_t i = 0; i < w.length; ++i) {
        if ((i & 4095) == 0)
            control.check();
        const auto index = indices[w.offset + i];
        need(index < vertices && std::uint64_t(first_vertex) + index <= UINT32_MAX,
             "Overlap neighbor exceeds section-local vertices");
        out.push_back(first_vertex + index);
    }
    return out;
}
Json MeshSectionOverlaps::to_json(bool include_values, const OperationControl &control) const {
    control.check();
    Json out = {{"scope", "stored-section-vertex-overlaps"},
                {"firstVertex", first_vertex},
                {"vertices", vertices},
                {"indicesStored", indices.size()},
                {"windowsStored", windows.size()},
                {"serializedSha256", serialized_sha256},
                {"coincidenceVerified", false},
                {"symmetryVerified", false},
                {"runtimeTangentsEvaluated", false}};
    if (include_values) {
        out["indices"] = Json::array();
        for (auto value : indices) {
            control.check();
            out["indices"].push_back(value);
        }
        out["windows"] = Json::array();
        for (const auto &w : windows) {
            control.check();
            out["windows"].push_back({w.length, w.offset});
        }
    }
    return out;
}
Json MeshSectionOverlaps::semantic_snapshot(const OperationControl &control) const {
    control.check();
    Json values = Json::object();
    for (std::uint32_t vi = 0; vi < vertices; ++vi) {
        control.check();
        auto row = neighbors(first_vertex + vi, control);
        // Order of integer seam accumulation does not change the stored relation.
        // Multiplicity remains observable; no inferred symmetry/deduplication.
        std::sort(row.begin(), row.end());
        values[std::to_string(first_vertex + vi)] = std::move(row);
    }
    return {{"firstVertex", first_vertex}, {"vertices", std::move(values)}};
}
Json MeshVertexOverlaps::semantic_snapshot(const OperationControl &control) const {
    control.check();
    Json rows = Json::object();
    for (const auto &s : sections) {
        control.check();
        need(bool(s) && !rows.contains(std::to_string(s->section)), "Null or duplicate overlap section");
        rows[std::to_string(s->section)] = s->semantic_snapshot(control);
    }
    return {{"schema", "cyuediff.mesh-overlaps/1"},
            {"scope", "stored-section-vertex-overlaps"},
            {"sections", std::move(rows)}};
}
} // namespace cyuediff
