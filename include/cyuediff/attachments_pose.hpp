#pragma once
#include <cyuediff/attachments.hpp>
#include <cyuediff/skeleton_pose.hpp>
namespace cyuediff {
// Prepared point contract: names are exact socket object paths, parents are raw bone names.
// Only explicitly stored local locations are transformed. Rotation/scale are not defaulted.
struct SocketPose : ReferencePose {
    Json to_json() const;
};
struct SocketPoseComparison : ReferencePoseComparison {
    Json to_json() const;
};
SocketPose socket_pose(const Package &, const std::string &object_path = {},
                       const ReferencePoseOptions & = {}, const AttachmentOptions & = {});
SocketPoseComparison compare_socket_poses(const Package &, const Package &,
                                          const std::string &before_object = {},
                                          const std::string &after_object = {},
                                          const ReferencePoseDiffOptions & = {},
                                          const AttachmentOptions & = {});
} // namespace cyuediff
