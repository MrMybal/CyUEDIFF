#pragma once
#include <cyuediff/collision.hpp>
namespace cyuediff {
using CollisionPoint = std::array<double, 3>;
struct CollisionLine {
    CollisionPoint a{}, b{};
};
struct CollisionBounds {
    CollisionPoint low{}, high{};
    bool valid = false;
};
struct CollisionWireShape {
    std::string body, identity, kind;
    std::vector<CollisionLine> lines;
    std::vector<std::string> diagnostics;
    CollisionBounds bounds;
    bool ready() const { return !lines.empty(); }
};
struct CollisionWireframeOptions {
    CollisionOptions collision;
    std::size_t max_edges = 100000;
    // Multiple of four, from 8 through 256; fixed sampling, independent of camera.
    unsigned circle_segments = 32;
};
struct CollisionWireframe {
    std::vector<CollisionWireShape> shapes;
    CollisionBounds bounds;
    bool source_complete = false;
    std::size_t edge_count() const;
    Json to_json() const;
};
// Body-local stored geometry only. Does not apply bone, component or world transforms.
// Missing geometry stays unavailable; budget exhaustion and cancellation throw atomically.
CollisionWireframe collision_wireframe(const Package &, const CollisionWireframeOptions & = {});
CollisionWireframe collision_wireframe(const ZenPackage &, const CollisionWireframeOptions & = {});
} // namespace cyuediff
