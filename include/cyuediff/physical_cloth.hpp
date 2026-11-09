#pragma once
#include <cyuediff/cloth.hpp>
#include <optional>
namespace cyuediff {
struct ZenPackage;
enum class PhysicalClothProfile { unspecified, unreal_5_3_windows };
struct PhysicalClothOptions {
    PhysicalClothProfile profile = PhysicalClothProfile::unspecified;
    bool geometry = true;
    std::size_t max_lods = 32, max_vertices = 1'000'000, max_indices = 12'000'000;
    std::size_t max_tethers = 4'000'000, max_transitions = 4'000'000, max_values = 4'000'000;
    std::size_t max_model_bytes = 256 * 1024 * 1024, max_text_bytes = 16 * 1024 * 1024;
    std::size_t max_report_values = 4'000'000;
    OperationControl control;
};
struct PhysicalClothBone {
    std::int32_t influences = 0;
    std::array<std::uint16_t, 12> indices{};
    std::array<float, 12> weights{};
};
struct PhysicalClothTether {
    std::int32_t anchor = 0, particle = 0;
    float length = 0;
};
struct PhysicalClothTransition {
    std::optional<std::uint32_t> source_lod;
    std::vector<ClothMapping> records;
    bool source_bounds_verified = false, wrap_available = false;
};
struct PhysicalClothLod {
    std::vector<std::array<float, 3>> vertices, normals;
    std::vector<std::uint32_t> indices, self_collision_indices;
    std::vector<float> inverse_masses;
    std::vector<PhysicalClothBone> bones;
    std::map<std::uint32_t, std::vector<float>> weight_maps;
    bool weight_maps_replaced = false;
    std::vector<std::uint32_t> removed_weight_maps;
    std::vector<std::vector<PhysicalClothTether>> euclidean_tethers, geodesic_tethers;
    PhysicalClothTransition up, down;
    // Only serialized parameters; omitted defaults stay absent.
    Json parameters = Json::object(), bounds = nullptr;
    std::shared_ptr<const MeshGeometry> geometry;
};
struct PhysicalClothConfig {
    std::string key, object_path, class_name;
    bool available = false;
    Json values = Json::object();
    std::vector<std::string> unavailable_fields;
};
class PhysicalClothAsset {
    std::string object_, package_sha_, context_sha_, scripts_sha_, guid_;
    bool available_ = false;
    std::vector<PhysicalClothLod> lods_;
    std::vector<std::int32_t> lod_map_, bone_indices_;
    std::vector<std::string> bone_names_, diagnostics_;
    std::optional<std::int32_t> reference_bone_;
    std::vector<PhysicalClothConfig> configs_;
    bool configs_replaced_ = false;
    std::vector<std::string> removed_configs_;
    std::size_t report_values_ = 4'000'000;
    Json provenance_ = nullptr;
    friend struct PhysicalClothBuilder;
    friend PhysicalClothAsset read_physical_cloth(const ZenPackage &, const PhysicalClothOptions &,
                                                  std::size_t);

  public:
    bool available() const {
        return available_;
    }
    const std::string &object_path() const {
        return object_;
    }
    const std::string &guid() const {
        return guid_;
    }
    const std::vector<PhysicalClothLod> &lods() const {
        return lods_;
    }
    const std::vector<PhysicalClothConfig> &configs() const {
        return configs_;
    }
    const std::vector<std::string> &diagnostics() const {
        return diagnostics_;
    }
    Json to_json(bool include_values = false, const OperationControl & = {}) const;
    Json vertex(std::uint32_t lod, std::uint32_t vertex, const OperationControl & = {}) const;
    Json transition(std::uint32_t lod, bool up, std::uint32_t vertex, const OperationControl & = {}) const;
    // Actual selected Chaos 5.3 single-record transition algebra applied to stored source positions/normals.
    // Weight and the renderer's fourth-index flag are not used by that CPU operation.
    std::vector<std::array<float, 3>> wrap_transition(std::uint32_t target_lod, bool up,
                                                      const OperationControl & = {}) const;
};
// Exact qualified mapped properties plus modern 64-byte native transition records; no defaults or Engine
// execution. Missing decoded property regions return available=false; malformed known data fails atomically.
PhysicalClothAsset read_physical_cloth(const ZenPackage &, const PhysicalClothOptions &,
                                       std::size_t export_index = SIZE_MAX);
// Explicit source profile, stored tagged metadata/native tails; no Engine/default evaluation.
PhysicalClothAsset read_physical_cloth(const Package &, const PhysicalClothOptions &,
                                       std::size_t export_index = SIZE_MAX);
struct PhysicalClothComparisonOptions {
    bool explicit_object_pair = false;
    std::size_t max_values = 8'000'000, max_changes = 100'000, max_report_bytes = 64 * 1024 * 1024;
    OperationControl control;
};
Json compare_physical_cloth(const PhysicalClothAsset &, const PhysicalClothAsset &,
                            const PhysicalClothComparisonOptions & = {});
// Base-LOD binding identity is certified by the stored GUID/LOD; biased bindings require an explicit source
// selection.
Json verify_cloth_source_bounds(const ClothSection &, std::uint32_t bias, const PhysicalClothAsset &,
                                std::uint32_t physical_lod, bool explicit_biased_source = false,
                                const OperationControl & = {});
} // namespace cyuediff
