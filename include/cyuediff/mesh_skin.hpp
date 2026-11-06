#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct MeshBone {
    std::uint32_t id = 0;
    std::int32_t parent = -1;
    std::string name;
    std::array<double, 4> rotation{0, 0, 0, 1};
    std::array<double, 3> translation{}, scale{1, 1, 1};
};
struct MeshInfluence {
    std::uint32_t bone = 0;
    std::uint16_t raw_weight = 0;
};
// Stored source weights, never normalized or evaluated by the reader.
struct MeshSkin {
    std::vector<MeshBone> bones;
    std::map<std::string, std::map<std::uint32_t, std::vector<MeshInfluence>>> profiles;
    // Named bones and per-vertex/profile raw weights; array order remains available above.
    void validate(const OperationControl &control = {}) const;
    Json to_json() const;
};
} // namespace cyuediff
