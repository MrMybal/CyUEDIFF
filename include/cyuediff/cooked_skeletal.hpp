#pragma once
#include <cyuediff/zen.hpp>
#include <cyuediff/mesh_skin.hpp>
#include <cyuediff/render_morphs.hpp>
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
    // Aggregate loaded profiles/overridden vertices across LODs.
    std::uint32_t max_weight_profiles = 64, max_profile_overrides = 1'000'000;
    std::uint32_t max_profile_name_bytes = 4096;
    NarrowStringEncoding narrow_string_encoding = NarrowStringEncoding::unspecified;
    // Aggregate compressed morph tables/entries across loaded LODs.
    std::uint32_t max_morph_targets = 4096, max_morph_batches = 1'000'000;
    std::uint64_t max_morph_deltas = 4'000'000;
};
struct CookedSkeletalWeightProfile {
    std::string name, skin_profile_key;
    std::uint32_t influences_per_vertex = 0;
    std::map<std::uint32_t, std::uint32_t> vertex_to_record;
    // Stored replacement slots, including zero padding; section-local IDs are
    // resolved to reference bones, uint8 weights expand losslessly by 257.
    std::map<std::uint32_t, std::vector<MeshInfluence>> overrides;
    Json buffers = Json::object();
    Json to_json(bool include_weights = false) const;
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
    std::vector<CookedSkeletalWeightProfile> weight_profiles;
    Json buffers = nullptr, bulk = nullptr;
    std::string unavailable;
    std::shared_ptr<const MeshGeometry> geometry;
    std::shared_ptr<const MeshRenderMorphs> morphs;
    Json to_json(bool include_geometry = false) const;
};
struct CookedSkeletalMesh {
    std::string object_path, package_sha256, schema_context_sha256, script_objects_sha256;
    std::uint64_t native_offset = 0, parsed_native_bytes = 0, tail_offset = 0, tail_bytes = 0;
    std::string tail_sha256;
    std::size_t export_index = 0;
    std::vector<MeshBone> bones;
    std::vector<std::string> morph_names;
    std::string morph_binding_unavailable;
    std::vector<CookedSkeletalLod> lods;
    Json native_prefix = Json::object(), materials = Json::array();
    Json to_json(bool include_geometry = false) const;
};
// Explicit UE 5.3 Windows editor-stripped render LODs, including reference
// skeleton, section-local skin indices and raw weights mapped to reference bones.
// Inline and streamed Zen bulk windows use mandatory whole used-chunk integrity.
// Named alternate profiles preserve sparse stored replacements and expose complete
// stored-slot profiles under distinct "profile:" keys beside the base "default".
// Compressed morph buffers retain exact stored render deltas and bind names only
// through explicit MorphTargets references to native child UMorphTarget exports.
// Cloth, duplicate-vertex tables and custom vertex attributes require another decoder.
// Later object-native data remains an identified opaque tail. No Unreal execution.
CookedSkeletalMesh read_zen_skeletal_mesh(const Container &, std::size_t package_entry,
                                          const ZenReadOptions &, const ContainerReadOptions &,
                                          const CookedSkeletalOptions &, std::size_t export_index = SIZE_MAX);
} // namespace cyuediff
