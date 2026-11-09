#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::detail {
// Mathematical numeric equality without converting an integer to a rounded double.
bool numeric_equal(const Json &, const Json &);
// Borrowed comparison; cancellation is checked during large equal subtrees too.
bool json_equal(const Json &, const Json &, const OperationControl & = {});
} // namespace cyuediff::detail
