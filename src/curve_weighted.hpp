#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::detail {
Json weighted_curve_segment(const Json &, const Json &, double alpha, double duration,
                            const OperationControl &);
}
