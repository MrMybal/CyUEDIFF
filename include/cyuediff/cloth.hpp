#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
// Stored render-to-simulation triangle mapping. The fourth source index is the
// producer's static/simulated contribution flag, including the 0xffff sentinel.
struct ClothMapping {
    std::array<float, 4> position{}, normal{}, tangent{};
    std::array<std::uint16_t, 4> source_indices{};
    float weight = 0;
    std::uint32_t padding = 0;
    bool simulated() const {
        return source_indices[3] != 65535;
    }
    void validate() const;
    Json to_json(bool raw = false) const;
    bool stored_equal(const ClothMapping &) const;
};
struct ClothSection {
    std::uint32_t section = 0, first_vertex = 0, vertices = 0;
    std::int32_t asset_index = -1, asset_lod = -1;
    std::array<std::uint32_t, 4> asset_guid{};
    std::vector<std::vector<ClothMapping>> lod_biases;
    bool has_render_mapping() const {
        return !lod_biases.empty() && !lod_biases[0].empty();
    }
    bool has_stored_data() const;
    void validate(std::uint32_t render_vertices, std::uint32_t max_influences = 5,
                  const OperationControl & = {}) const;
    Json to_json(bool include_mappings = false, const OperationControl & = {}) const;
};
struct ClothBufferIndex {
    std::uint32_t first_vertex = 0, offset = 0, lod_bias_stride = 0;
    Json to_json() const;
};
// Explicit stored cloth deformer scope. No simulation asset vertex bounds,
// normals, collision response, blend weights or runtime cloth state are inferred.
struct MeshCloth {
    std::vector<std::shared_ptr<const ClothSection>> sections;
    std::vector<ClothMapping> buffer;
    std::vector<ClothBufferIndex> index_mapping;
    bool buffer_stored = false;
    Json buffer_info = nullptr;
    void validate(std::uint32_t render_vertices, std::uint32_t max_influences = 5,
                  const OperationControl & = {}) const;
    const ClothSection &section(std::uint32_t section_id) const;
    Json vertex(std::uint32_t section_id, std::uint32_t lod_bias, std::uint32_t render_vertex,
                bool raw = false, const OperationControl & = {}) const;
    Json semantic_snapshot(const OperationControl & = {}) const;
    Json to_json(bool include_mappings = false, const OperationControl & = {}) const;
};
} // namespace cyuediff
