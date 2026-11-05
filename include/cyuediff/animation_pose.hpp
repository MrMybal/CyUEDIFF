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
    // Ready source-space affine matrices, row-major 3x4, applied to column vectors.
    // Missing/invalid ancestors have no entry. Values are never reconstructed from joint origins.
    std::map<std::string, std::array<double, 12>> source_affines;
    Json to_json() const;
};
// Explicit caller-supplied Skeleton/SkeletalMesh. Exact, case-sensitive track names only.
// Source integer keys are composed as affine matrices. No rest-pose fallback, retargeting,
// root-motion extraction, interpolation, virtual-bone evaluation or skinning is performed.
AnimationPose animation_pose(const Package &animation, const Package &skeleton, std::uint32_t frame,
                             const std::string &animation_object = {},
                             const std::string &skeleton_object = {}, const AnimationPoseOptions & = {});
struct AnimationPoseSelection {
    std::uint32_t frame = 0;
    std::string animation_object, skeleton_object;
};
struct AnimationPoseDiffOptions {
    AnimationPoseOptions pose;
    double position_tolerance = 0;
    std::size_t max_joint_pairs = 100000;
};
// Derived component-space joint origins and parent identities only. Local rotation/scale
// differences that do not move a joint remain the scope of compare_animation_frames().
Json compare_animation_poses(const AnimationPose &, const AnimationPose &,
                             const AnimationPoseDiffOptions & = {});
Json compare_animation_poses(const Package &before_animation, const Package &before_skeleton,
                             const Package &after_animation, const Package &after_skeleton,
                             const AnimationPoseSelection &before = {},
                             const AnimationPoseSelection &after = {}, const AnimationPoseDiffOptions & = {});
} // namespace cyuediff
