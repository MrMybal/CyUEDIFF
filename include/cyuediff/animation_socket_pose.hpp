#pragma once
#include <cyuediff/animation_pose.hpp>
#include <cyuediff/attachments.hpp>
namespace cyuediff {
struct AnimationSocketOptions {
    AttachmentOptions attachments;
    std::size_t max_sockets = 100000, max_parent_joints = 100000;
    OperationControl control;
};
struct AnimationSocketPose {
    std::string animation_object, skeleton_object;
    std::uint32_t frame = 0;
    std::optional<double> time_seconds;
    ReferencePose display_pose;
    bool membership_complete = false;
    Json bindings = Json::object();
    Json to_json() const;
};
// Explicit owner socket definitions applied to ready source_affines. Exact object paths/bone names;
// no reference-pose fallback, virtual-bone evaluation, socket orientation or runtime playback.
// The caller supplies the socket package corresponding to the prepared source's skeleton_object.
AnimationSocketPose animation_socket_pose(const AnimationPose &, const Package &socket_definitions,
                                          const AnimationSocketOptions & = {});
AnimationSocketPose animation_socket_pose(const Package &animation, const Package &skeleton,
                                          const AnimationPoseSelection & = {},
                                          const AnimationPoseOptions & = {},
                                          const AnimationSocketOptions & = {});
Json compare_animation_socket_poses(const AnimationSocketPose &, const AnimationSocketPose &,
                                    const AnimationPoseDiffOptions & = {});
} // namespace cyuediff
