#pragma once
#include "archive.hpp"
namespace cyuediff::detail {
bool is_material_input(const std::string &type);
bool native_material_input(const PackageSummary &summary);
Json decode_material_input(Archive &, const Package &, const std::string &type, std::uint64_t end);
} // namespace cyuediff::detail
