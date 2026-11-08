#pragma once
#include <cyuediff/zen.hpp>
#include <cyuediff/collision_wireframe.hpp>
namespace cyuediff {
enum class CookedPhysicsProfile { unspecified, unreal_5_3_windows };
struct CookedPhysicsOptions {
    CookedPhysicsProfile profile = CookedPhysicsProfile::unspecified;
    std::uint64_t max_read_bytes = 512ull * 1024 * 1024;
    std::uint64_t max_output_bytes = 512ull * 1024 * 1024;
    std::uint32_t max_bodies = 4096, max_formats = 64, max_objects = 20000, max_references = 40000;
    std::uint32_t max_vertices = 1000000, max_triangles = 4000000, max_topology = 12000000;
    std::uint32_t max_bvh_nodes = 1000000, max_uv_channels = 8, max_changes = 100000;
    OperationControl control;
};
// Explicit UE 5.3 Windows Chaos cooker stream. The legacy platform label PhysXPC
// is not used to infer a wire format. Every byte must belong to the selected profile.
// Objects are stored once; convex/trimesh arrays retain nullable graph tag references.
Json read_chaos_physics(const std::vector<std::uint8_t> &, const CookedPhysicsOptions &);
// Reads exact non-template Engine BodySetup/SkeletalBodySetup exports with successful
// explicit property boundaries. Whole used external chunks retain source integrity.
// Missing/unused formats remain unavailable. No defaults or owner transforms are inferred.
Json read_zen_cooked_physics(const Container &, std::size_t package_entry, const ZenReadOptions &,
                             const ContainerReadOptions &, const CookedPhysicsOptions &);
// Exact geometry/parameter diff of snapshots returned above. Transport hashes, offsets,
// graph tag values and storage index widths do not create geometry changes.
// Missing payloads remain unresolved rather than added/removed geometry.
Json compare_cooked_physics(const Json &before, const Json &after, const CookedPhysicsOptions &);
struct CookedPhysicsReview {
    Json model;
    CollisionWireframe wire;
};
// Prepared cooker-coordinate geometry and native parameters from a snapshot returned
// by read_zen_cooked_physics. No hull/default/transform reconstruction or draw-time I/O.
CookedPhysicsReview cooked_physics_review(const Json &, const CookedPhysicsOptions &,
                                          std::size_t max_edges = 300000);
} // namespace cyuediff
