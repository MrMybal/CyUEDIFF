#pragma once
#include <cyuediff/schema_context.hpp>
#include <cyuediff/cooked_texture.hpp>
#include <cyuediff/cooked_mesh.hpp>
#include <cyuediff/cooked_skeletal.hpp>
#include <cyuediff/cooked_physics.hpp>
#include <cyuediff/cooked_animation.hpp>
#include <cyuediff/physical_cloth.hpp>
#include <cyuediff/collision_wireframe.hpp>
#include <optional>
namespace cyuediff::detail {
class JsonObject;
}
namespace cyuediff::desktop {
struct ContainerSource {
    fs::path path, script_objects, schema;
    std::string entry;
    ZenProfile profile = ZenProfile::unspecified;
    fs::path after_path, after_script_objects, after_schema;
    ZenProfile after_profile = ZenProfile::unspecified;
    CookedMeshProfile mesh_profile = CookedMeshProfile::unspecified,
                      after_mesh_profile = CookedMeshProfile::unspecified;
    std::optional<bool> keep_mobile_min_lod_setting, after_keep_mobile_min_lod_setting;
    NaniteProfile nanite_profile = NaniteProfile::unspecified,
                  after_nanite_profile = NaniteProfile::unspecified;
    bool nanite_attributes = true, after_nanite_attributes = true;
    CookedPhysicsProfile physics_profile = CookedPhysicsProfile::unspecified,
                         after_physics_profile = CookedPhysicsProfile::unspecified;
    CookedSkeletalProfile skeletal_profile = CookedSkeletalProfile::unspecified,
                          after_skeletal_profile = CookedSkeletalProfile::unspecified;
    std::optional<bool> skeletal_mobile_lod, after_skeletal_mobile_lod;
    std::optional<bool> skeletal_colors, after_skeletal_colors;
    std::string after_entry;
    CookedAnimationProfile animation_profile = CookedAnimationProfile::unspecified,
                           after_animation_profile = CookedAnimationProfile::unspecified;
    std::optional<CompressedAnimationInterpolation> animation_interpolation, after_animation_interpolation;
    fs::path bone_settings_container, skeleton_container, after_bone_settings_container,
        after_skeleton_container;
    std::string bone_settings_entry, skeleton_entry, after_bone_settings_entry, after_skeleton_entry;
    fs::path curve_settings_container, after_curve_settings_container;
    std::string curve_settings_entry, after_curve_settings_entry;
    PhysicalClothProfile physical_cloth_profile = PhysicalClothProfile::unspecified,
                         after_physical_cloth_profile = PhysicalClothProfile::unspecified;
    bool compare_payloads = false;
    std::uint64_t max_comparison_bytes = 512ull * 1024 * 1024;
    std::uint64_t max_payload_bytes = 64ull * 1024 * 1024;
};
struct ContainerNanitePreview {
    std::string object_path;
    std::shared_ptr<const CookedNaniteResource> resource;
};
struct ContainerAnimationPreview {
    std::string object_path;
    std::shared_ptr<const CookedAnimation> model;
};
struct ContainerMeshPreview {
    std::string key, object_path;
    std::uint32_t lod = 0;
    std::shared_ptr<const MeshGeometry> geometry;
};
struct ContainerTexturePreview {
    std::string key, object_path, format;
    std::uint32_t mip = 0;
    ImageRgba8 image;
    std::optional<ImageRgba32f> hdr;
};
struct ContainerTextureDifference {
    std::string key;
    ImageComparison comparison;
    std::optional<HdrImageComparison> hdr;
};
struct ContainerPhysicalClothPreview {
    std::string object_path;
    std::shared_ptr<const PhysicalClothAsset> model;
};
struct ContainerDocument {
    ContainerSource source;
    std::shared_ptr<const Container> input;
    Json metadata, inspection = nullptr;
    std::optional<ZenPackage> zen;
    std::optional<ZenSchemaManifest> schema_manifest;
    bool schema_loaded = false;
    Json physics = nullptr, physics_difference = nullptr, physics_model = nullptr;
    CollisionWireframe physics_wire;
    std::vector<std::string> physics_diagnostics;
    Json collisions = nullptr, collision_difference = nullptr;
    CollisionWireframe collision_wire;
    std::vector<std::string> collision_diagnostics;
    Json meshes = Json::array(), mesh_differences = Json::array();
    std::vector<ContainerMeshPreview> mesh_previews;
    std::vector<ContainerNanitePreview> nanite_previews;
    std::vector<std::string> mesh_diagnostics;
    std::vector<ContainerPhysicalClothPreview> physical_cloth_previews;
    std::vector<std::string> physical_cloth_diagnostics;
    Json animations = Json::array();
    std::vector<ContainerAnimationPreview> animation_previews;
    std::vector<std::string> animation_diagnostics;
    Json textures = Json::array();
    std::vector<ContainerTexturePreview> previews;
    std::vector<ContainerTextureDifference> pixel_differences;
    std::vector<std::string> texture_diagnostics;
    std::shared_ptr<const ContainerDocument> after;
    std::vector<ContainerPair> members;
    std::vector<std::string> member_states;
    Json comparison = nullptr, selected_comparison = nullptr;
    std::vector<fs::path> protected_files;
    Json report() const;
    detail::JsonObject report_view() const;
};
// Exact object/LOD identity first. Explicit independently selected package members
// can also pair the same LOD when each package contains exactly one mesh export.
const ContainerMeshPreview *paired_mesh_preview(const ContainerDocument &, const ContainerMeshPreview &,
                                                bool after);
// Every request indexes current files again. No virtual member path becomes a disk destination.
std::shared_ptr<const ContainerDocument>
load_container_document(const ContainerSource &, const fs::path &codec, const OperationControl & = {});
} // namespace cyuediff::desktop
