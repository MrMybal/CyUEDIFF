#pragma once
#include <cyuediff/binary_delta.hpp>
namespace cyuediff::detail {
void binary_delta_options(const BinaryDeltaOptions &);
Json binary_delta_validate(const BinaryDelta &, const BinaryDeltaOptions &);
} // namespace cyuediff::detail
