#pragma once
#include <cyuediff/curves.hpp>
namespace cyuediff::detail {
// Accepts a validated curve_snapshot lane. Bounded logarithmic lookup; never loads or repairs data.
Json evaluate_curve_lane(const Json &lane, double time, bool complete_input, const OperationControl & = {});
} // namespace cyuediff::detail
