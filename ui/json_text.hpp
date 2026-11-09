#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::detail {
// Presentation only. An empty result means the full text is unavailable, never truncated JSON.
constexpr std::size_t presentation_json_bytes = 4 * 1024 * 1024, presentation_json_lines = 50'000;
std::string presentation_json(const Json &);
} // namespace cyuediff::detail
