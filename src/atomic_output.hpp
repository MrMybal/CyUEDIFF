#pragma once
#include <cyuediff/core.hpp>
#include <ostream>
namespace cyuediff::detail {
// Explicit output path, after caller protection checks. Flush a new sibling file,
// then replace the destination; failed generation leaves its old contents intact.
void atomic_output(const fs::path &, const std::function<void(std::ostream &)> &);
} // namespace cyuediff::detail
