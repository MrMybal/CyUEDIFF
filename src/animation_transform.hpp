#pragma once
#include <cyuediff/animation.hpp>
namespace cyuediff::detail {
// Raw sampled bone-local TRS only. Quaternion normalization is used for the metric, not source repair.
Json compare_local_animation_values(const Json &, const Json &, double position_tolerance = 0,
                                    double rotation_tolerance_degrees = 0, double scale_tolerance = 0,
                                    const OperationControl & = {});
void validate_animation_tolerances(double position, double rotation_degrees, double scale);
} // namespace cyuediff::detail
