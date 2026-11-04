#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct CollisionOptions {
    std::size_t max_bodies = 4096, max_shapes = 20000, max_nodes = 1000000, max_text_bytes = 16 * 1024 * 1024;
    unsigned max_depth = 48;
    OperationControl control;
};
// Projects reflected BodySetup/SkeletalBodySetup properties already read by the Core.
// Missing fields stay missing. No engine defaults, physics cooking or native blob execution.
Json collision_snapshot(const Package &, const CollisionOptions & = {});
} // namespace cyuediff
