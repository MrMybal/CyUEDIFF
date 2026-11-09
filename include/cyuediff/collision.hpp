#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct ZenPackage;
struct CollisionOptions {
    std::size_t max_bodies = 4096, max_shapes = 20000, max_nodes = 1000000, max_text_bytes = 16 * 1024 * 1024;
    std::size_t max_vertices = 1000000, max_indices = 3000000;
    unsigned max_depth = 48;
    bool source_indices = true;
    OperationControl control;
};
// Projects reflected BodySetup/SkeletalBodySetup properties already read by the Core.
// Missing fields stay missing. No engine defaults, physics cooking or native blob execution.
Json collision_snapshot(const Package &, const CollisionOptions & = {});
// Explicitly mapped cooked reflected shapes; native physics data remains opaque.
Json collision_snapshot(const ZenPackage &, const CollisionOptions & = {});
struct CollisionComparison {
    Json changes = Json::array();
    bool complete = false;
    Json unavailable_scopes = Json::array();
    std::string status() const;
    Json to_json() const;
};
// Unique exact stored names provide identity within each shape kind.
// Anonymous/duplicate names use explicit collection indices and remain order-sensitive.
CollisionComparison compare_collisions(const Package &, const Package &, const CollisionOptions & = {});
CollisionComparison compare_collisions(const ZenPackage &, const ZenPackage &, const CollisionOptions & = {});
} // namespace cyuediff
