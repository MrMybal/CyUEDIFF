#pragma once
#include <cyuediff/animation_socket_pose.hpp>
namespace cyuediff::detail {
// Geometry-only adapter. Socket membership certifies absence independently of position readiness.
AnimationPose animation_socket_points(const AnimationSocketPose &);
Json animation_socket_comparison(Json);
} // namespace cyuediff::detail
