#pragma once
#include <cyuediff/curves.hpp>
namespace cyuediff::detail {
Json compare_curve_lanes(const Json *, const Json *, bool before_complete, bool after_complete,
                         const CurveValueDiffOptions &);
// Accepts a validated curve_snapshot lane. Bounded logarithmic lookup; never loads or repairs data.
Json evaluate_curve_lane(const Json &lane, double time, bool complete_input, const OperationControl & = {});
} // namespace cyuediff::detail
