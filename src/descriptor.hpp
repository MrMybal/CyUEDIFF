#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::detail {
// Unreal descriptor JSON: comments and array trailing commas; bounded nesting.
Json parse_descriptor(std::string source);
} // namespace cyuediff::detail
