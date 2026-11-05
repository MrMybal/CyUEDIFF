#pragma once
#include <cyuediff/skeleton_pose.hpp>
namespace cyuediff::detail {
// Internal: the body must come from the validated, source-indexed skeleton projection.
ReferencePose compose_reference_pose(const Json &body, const std::string &object_path, bool complete_input,
                                     const ReferencePoseOptions &);
} // namespace cyuediff::detail
