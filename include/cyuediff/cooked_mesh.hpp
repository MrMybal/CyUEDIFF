#pragma once
#include <cyuediff/zen.hpp>
#include <optional>
namespace cyuediff {
enum class CookedMeshProfile { unspecified, unreal_5_3_windows };
struct CookedMeshOptions {
    CookedMeshProfile profile = CookedMeshProfile::unspecified;
    // Producer setting; it has no serialized presence marker.
    std::optional<bool> keep_mobile_min_lod_setting;
    bool geometry = true;
    std::uint64_t max_read_bytes = 512ull * 1024 * 1024, max_output_bytes = 512ull * 1024 * 1024;
    std::uint32_t max_lods = 8, max_sections = 4096, max_vertices = 1'000'000;
    std::uint32_t max_indices = 12'000'000, max_triangles = 4'000'000, max_uv_channels = 8;
    OperationControl control;
};
struct CookedMeshSection {
    std::uint32_t material = 0, first_index = 0, triangles = 0, min_vertex = 0, max_vertex = 0;
    bool collision = false, shadow = false, opaque = false, ray_tracing = false, distance_field = false;
    Json to_json() const;
};
struct CookedMeshLod {
    bool cooked_out = false, inlined = false;
    std::uint8_t strip_flags = 0;
    float max_deviation = 0;
    std::vector<CookedMeshSection> sections;
    Json buffers = nullptr, bulk = nullptr;
    std::string unavailable;
    std::shared_ptr<const MeshGeometry> geometry;
    Json to_json(bool include_geometry = false) const;
};
struct CookedStaticMesh {
    std::string object_path, package_sha256, schema_context_sha256, script_objects_sha256;
    std::uint64_t native_offset = 0, parsed_native_bytes = 0, tail_offset = 0, tail_bytes = 0;
    std::string tail_sha256;
    std::size_t export_index = 0;
    std::vector<CookedMeshLod> lods;
    Json native_prefix = Json::object(), materials = Json::array();
    Json to_json(bool include_geometry = false) const;
};
// Reads the supported render-LOD windows from an explicit decoded property boundary.
// Later Nanite/distance-field/object native data remains an identified opaque tail.
// Whole used external chunks are integrity-verified; missing LOD payloads are explicit.
CookedStaticMesh read_zen_static_mesh(const Container &, std::size_t package_entry, const ZenReadOptions &,
                                      const ContainerReadOptions &, const CookedMeshOptions &,
                                      std::size_t export_index = SIZE_MAX);
} // namespace cyuediff
