#pragma once
#include <cyuediff/animation.hpp>
#include <cyuediff/skeleton_pose.hpp>
namespace cyuediff {
struct AnimationPoseOptions {
    AnimationOptions animation;
    SkeletonOptions skeleton;
    std::size_t max_joints = 100000;
    unsigned max_hierarchy_depth = 512;
    // Shared cancellation/progress for projection, sampling and composition.
    OperationControl control;
};
struct AnimationPose {
    std::string animation_object, skeleton_object;
    std::uint32_t frame = 0;
    std::optional<double> time_seconds;
    // Geometry carrier for reusable viewports. Use AnimationPose::to_json() for source-space labels.
    ReferencePose display_pose;
    Json bindings = Json::object(), unbound_tracks = Json::array();
    bool hierarchy_complete = false;
    Json to_json() const;
};
// Explicit caller-supplied Skeleton/SkeletalMesh. Exact, case-sensitive track names only.
// Source integer keys are composed as affine matrices. No rest-pose fallback, retargeting,
// root-motion extraction, interpolation, virtual-bone evaluation or skinning is performed.
AnimationPose animation_pose(const Package &animation, const Package &skeleton, std::uint32_t frame,
                             const std::string &animation_object = {},
                             const std::string &skeleton_object = {}, const AnimationPoseOptions & = {});
} // namespace cyuediff
