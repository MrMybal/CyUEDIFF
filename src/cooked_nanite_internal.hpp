#pragma once
#include <cyuediff/cooked_nanite.hpp>
#include "zen_bulk.hpp"
namespace cyuediff::detail {
CookedNaniteResource read_nanite_resource(const std::vector<std::uint8_t> &, std::uint64_t &position,
                                          std::uint64_t end, ZenBulkReader &, const CookedNaniteOptions &,
                                          Json provenance);
}
