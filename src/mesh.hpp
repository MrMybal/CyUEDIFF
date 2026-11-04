#pragma once
#include "texture.hpp"
namespace cyuediff::detail {
bool is_mesh_description(const ObjectExport &);
struct MeshSourceResult {
    MeshSource source;
    Json native;
    std::uint64_t end = 0;
};
MeshSourceResult decode_mesh_source(Archive &, const Package &, std::size_t, std::uint64_t,
                                    const ReadOptions &);
MeshGeometry decode_mesh_description(const std::vector<std::uint8_t> &, const MeshSource &,
                                     const MeshDecodeOptions &);
} // namespace cyuediff::detail
