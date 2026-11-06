#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
// Stored deltas in source geometry space. Vertex instances keep split-normal identities.
struct MeshMorphTarget {
    std::map<std::uint32_t, std::array<float, 3>> positions;
    std::map<std::uint32_t, std::array<float, 3>> normals;
    bool normals_present = false;
};
struct MeshMorphs {
    std::map<std::string, MeshMorphTarget> targets;
    // Requires exactly the live source vertex/instance IDs, including stored zero deltas.
    void validate(const MeshGeometry &, const OperationControl &control = {}) const;
    Json to_json() const;
};
} // namespace cyuediff
