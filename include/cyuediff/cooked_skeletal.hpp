#pragma once
#include <cyuediff/zen.hpp>
#include <cyuediff/mesh_skin.hpp>
#include <optional>
namespace cyuediff {
enum class CookedSkeletalProfile { unspecified, unreal_5_3_windows };
struct CookedSkeletalOptions {
    CookedSkeletalProfile profile = CookedSkeletalProfile::unspecified;
    // Producer presence of MinMobileLOD; the wire has no presence marker.
    std::optional<bool> keep_mobile_min_lod_setting;
    // Required only when bHasVertexColors is omitted from the explicit property map.
    std::optional<bool> has_vertex_colors;
    bool geometry = true;
    std::uint64_t max_read_bytes = 512ull * 1024 * 1024, max_output_bytes = 512ull * 1024 * 1024;
    std::uint32_t max_lods = 8, max_sections = 4096, max_bones = 65536;
    std::uint32_t max_vertices = 1'000'000, max_indices = 12'000'000, max_triangles = 4'000'000;
    std::uint32_t max_uv_channels = 8, max_influences = 256;
    OperationControl control;
};
struct CookedSkeletalSection {
    std::uint32_t material = 0, first_index = 0, triangles = 0, first_vertex = 0, vertices = 0;
    std::uint32_t max_influences = 0;
    std::vector<std::uint16_t> bone_map;
    Json fields = Json::object();
    Json to_json() const;
};
struct CookedSkeletalLod {
    bool cooked_out = false, inlined = false;
    std::uint8_t strip_flags = 0;
    std::vector<std::uint16_t> required_bones, active_bones;
    std::vector<CookedSkeletalSection> sections;
    Json buffers = nullptr;
    std::string unavailable;
    std::shared_ptr<const MeshGeometry> geometry;
    Json to_json(bool include_geometry = false) const;
};
struct CookedSkeletalMesh {
    std::string object_path, package_sha256, schema_context_sha256, script_objects_sha256;
    std::uint64_t native_offset = 0, parsed_native_bytes = 0, tail_offset = 0, tail_bytes = 0;
    std::string tail_sha256;
    std::size_t export_index = 0;
    std::vector<MeshBone> bones;
    std::vector<CookedSkeletalLod> lods;
    Json native_prefix = Json::object(), materials = Json::array();
    Json to_json(bool include_geometry = false) const;
};
// Explicit UE 5.3 Windows editor-stripped inline render LODs, including reference
// skeleton, section-local skin indices and raw weights mapped to reference bones.
// Streaming LODs, cloth, duplicate-vertex tables, alternate weights, compressed
// morph buffers and custom vertex attributes currently require another decoder.
// Later object-native data remains an identified opaque tail. No Unreal execution.
CookedSkeletalMesh read_zen_skeletal_mesh(const Container &, std::size_t package_entry,
                                          const ZenReadOptions &, const ContainerReadOptions &,
                                          const CookedSkeletalOptions &, std::size_t export_index = SIZE_MAX);
} // namespace cyuediff
