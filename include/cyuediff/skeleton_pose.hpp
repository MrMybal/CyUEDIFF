#pragma once
#include <cyuediff/skeleton.hpp>
namespace cyuediff {
struct ReferenceJoint {
    std::string name, parent;
    std::array<double, 3> position{};
    bool ready = false;
    std::vector<std::string> diagnostics;
};
struct ReferencePoseOptions {
    SkeletonOptions skeleton;
    unsigned max_hierarchy_depth = 512;
    std::size_t max_joints = 100000;
};
struct ReferencePose {
    std::string object_path;
    std::vector<ReferenceJoint> joints;
    std::vector<std::string> diagnostics;
    bool complete = false;
    Json to_json() const;
};
// Ordered local TRS matrices are composed as affine transforms, without animation or skinning.
// This does not emulate Unreal FTransform decomposition when nonuniform scales produce shear.
ReferencePose reference_pose(const Package &, const std::string &object_path = {},
                             const ReferencePoseOptions & = {});
} // namespace cyuediff
