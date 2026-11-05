#pragma once
#include <cyuediff/attachments_pose.hpp>
namespace cyuediff::detail {
// Inputs must be validated attachment and source-indexed skeleton projections.
SocketPose compose_socket_pose(const Json &attachments, const Json &skeleton_body, const std::string &path,
                               bool complete_input, const ReferencePoseOptions &,
                               ReferencePose *parent_origins = nullptr);
} // namespace cyuediff::detail
