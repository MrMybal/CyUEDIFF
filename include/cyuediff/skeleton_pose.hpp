#pragma once
#include <cyuediff/skeleton.hpp>
#include <optional>
namespace cyuediff {
struct ReferenceJoint {
    std::string name, parent;
    std::array<double, 3> position{};
    bool ready = false;
    std::vector<std::string> diagnostics;
};
struct ReferencePoseOptions {
    SkeletonOptions skeleton;
    // Empty selects the stored raw reference pose; named selection never falls back to it.
    std::string retarget_source;
    unsigned max_hierarchy_depth = 512;
    std::size_t max_joints = 100000;
};
struct ReferencePose {
    std::string object_path, retarget_source;
    std::vector<ReferenceJoint> joints;
    std::vector<std::string> diagnostics;
    bool complete = false;
    Json to_json() const;
};
// Ordered local TRS matrices are composed as affine transforms, without animation or skinning.
// This does not emulate Unreal FTransform decomposition when nonuniform scales produce shear.
ReferencePose reference_pose(const Package &, const std::string &object_path = {},
                             const ReferencePoseOptions & = {});
struct ReferencePoseDiffOptions {
    ReferencePoseOptions pose;
    // Unset inherits pose.retarget_source. An explicit empty string selects raw after.
    std::optional<std::string> after_retarget_source;
    double position_tolerance = 0;
    std::size_t max_joint_pairs = 100000;
};
struct ReferencePoseComparison {
    std::string before_object, after_object, before_retarget_source, after_retarget_source;
    Json joints = Json::object();
    bool complete = false;
    double position_tolerance = 0;
    std::size_t changed_joints = 0, unavailable_joints = 0;
    std::string status() const;
    Json to_json() const;
};
// Exact bone names pair derived positions; parent changes remain visible at any position tolerance.
ReferencePoseComparison compare_reference_poses(const ReferencePose &, const ReferencePose &,
                                                const ReferencePoseDiffOptions & = {});
ReferencePoseComparison compare_reference_poses(const Package &, const Package &,
                                                const std::string &before_object = {},
                                                const std::string &after_object = {},
                                                const ReferencePoseDiffOptions & = {});
} // namespace cyuediff
