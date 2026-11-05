#include <cyuediff/animation_pose.hpp>
#include <cmath>
namespace cyuediff {
Json compare_animation_poses(const AnimationPose &before, const AnimationPose &after,
                             const AnimationPoseDiffOptions &options) {
    options.pose.control.check();
    if (!std::isfinite(options.position_tolerance) || options.position_tolerance < 0)
        throw Error("animation-pose-tolerance", "Position tolerance must be finite and nonnegative");
    ReferencePoseDiffOptions ro;
    ro.position_tolerance = options.position_tolerance;
    ro.max_joint_pairs = options.max_joint_pairs;
    ro.pose.max_joints = options.pose.max_joints;
    ro.pose.skeleton = options.pose.skeleton;
    ro.pose.skeleton.control = options.pose.control;
    auto a = before.display_pose, b = after.display_pose;
    // Membership comes from the verified raw hierarchy, independently of missing source channels.
    a.complete = before.hierarchy_complete;
    b.complete = after.hierarchy_complete;
    auto comparison = compare_reference_poses(a, b, ro);
    comparison.complete = comparison.complete && before.display_pose.complete && after.display_pose.complete;
    auto identity = [](const AnimationPose &pose) {
        Json out = {{"animationObject", pose.animation_object},
                    {"skeletonObject", pose.skeleton_object},
                    {"frame", pose.frame}};
        if (pose.time_seconds)
            out["timeSeconds"] = *pose.time_seconds;
        return out;
    };
    return {{"schema", "cyuediff.animation-pose-diff/1"},
            {"space", "source-animation-affine"},
            {"scope", "derived joint origins and named parents in independently selected source frames; no "
                      "skinning or retargeting"},
            {"before", identity(before)},
            {"after", identity(after)},
            {"positionTolerance", options.position_tolerance},
            {"complete", comparison.complete},
            {"status", comparison.status()},
            {"changedJoints", comparison.changed_joints},
            {"unavailableJoints", comparison.unavailable_joints},
            {"joints", comparison.joints},
            {"beforeDiagnostics", before.display_pose.diagnostics},
            {"afterDiagnostics", after.display_pose.diagnostics}};
}
Json compare_animation_poses(const Package &before_animation, const Package &before_skeleton,
                             const Package &after_animation, const Package &after_skeleton,
                             const AnimationPoseSelection &before, const AnimationPoseSelection &after,
                             const AnimationPoseDiffOptions &options) {
    // Validate before any input projection or potentially expensive sampling.
    if (!std::isfinite(options.position_tolerance) || options.position_tolerance < 0)
        throw Error("animation-pose-tolerance", "Position tolerance must be finite and nonnegative");
    const auto a = animation_pose(before_animation, before_skeleton, before.frame, before.animation_object,
                                  before.skeleton_object, options.pose);
    const auto b = animation_pose(after_animation, after_skeleton, after.frame, after.animation_object,
                                  after.skeleton_object, options.pose);
    return compare_animation_poses(a, b, options);
}
} // namespace cyuediff
