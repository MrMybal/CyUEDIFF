#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct MeshOverlapWindow {
    std::uint32_t length = 0, offset = 0;
};
// Section-local stored correspondence for runtime tangent seam accumulation.
// This does not certify coincident positions, symmetry, welding or a runtime pose.
struct MeshSectionOverlaps {
    std::uint32_t section = 0, first_vertex = 0, vertices = 0;
    std::vector<std::uint32_t> indices;
    std::vector<MeshOverlapWindow> windows;
    std::string serialized_sha256;
    void validate(std::uint32_t render_vertices, const OperationControl & = {},
                  std::uint64_t max_resolved_indices = 12'000'000) const;
    std::vector<std::uint32_t> neighbors(std::uint32_t render_vertex, const OperationControl & = {}) const;
    Json to_json(bool include_values = false, const OperationControl & = {}) const;
    Json semantic_snapshot(const OperationControl & = {}) const;
};
struct MeshVertexOverlaps {
    std::vector<std::shared_ptr<const MeshSectionOverlaps>> sections;
    // Stripped tables are unavailable, rather than proof of an empty relation.
    std::vector<std::uint32_t> unavailable_sections;
    Json semantic_snapshot(const OperationControl & = {}) const;
};
} // namespace cyuediff
