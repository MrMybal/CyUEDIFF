#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
enum class NaniteProfile { unspecified, unreal_5_3_windows };
struct NanitePageSource {
    // Complete stored page: fixup chunk followed by the exact disk page, without container compression.
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint32_t> dependencies;
    std::uint8_t flags = 0;
};
struct NaniteGeometrySource {
    std::uint32_t root_pages = 0;
    // Resource page order. Dependency entries are resource page indices, not GPU residency IDs.
    std::vector<NanitePageSource> pages;
};
struct NaniteGeometryOptions {
    NaniteProfile profile = NaniteProfile::unspecified;
    std::uint32_t max_pages = 16384, max_clusters = 1000000;
    std::uint64_t max_vertices = 4000000, max_triangles = 8000000, max_read_bytes = 512ull * 1024 * 1024,
                  max_output_bytes = 512ull * 1024 * 1024;
    OperationControl control;
};
struct NaniteTriangle {
    std::array<std::uint32_t, 3> vertices{};
    std::uint32_t material = 0;
};
class NaniteGeometry {
  public:
    struct Impl;
    std::size_t page_count() const;
    std::size_t cluster_count(std::size_t page) const;
    Json catalog(const OperationControl & = {}) const;
    Json cluster_metadata(std::size_t page, std::size_t cluster, const OperationControl & = {}) const;
    // At most 256 positions/128 triangles per selected cluster. Owned immutable result, no source I/O.
    std::array<std::int32_t, 3> quantized_position(std::size_t page, std::size_t cluster, std::size_t vertex,
                                                   const OperationControl & = {}) const;
    std::array<float, 3> position(std::size_t page, std::size_t cluster, std::size_t vertex,
                                  const OperationControl & = {}) const;
    NaniteTriangle triangle(std::size_t page, std::size_t cluster, std::size_t triangle,
                            const OperationControl & = {}) const;

  private:
    std::shared_ptr<const Impl> impl_;
    explicit NaniteGeometry(std::shared_ptr<const Impl>);
    friend NaniteGeometry decode_nanite_geometry(const NaniteGeometrySource &, const NaniteGeometryOptions &);
};
// Host must establish the complete page/dependency set and matching producer profile.
// Decodes positions, oriented triangles and material indices. Vertex attributes remain explicit opaque data.
NaniteGeometry decode_nanite_geometry(const NaniteGeometrySource &, const NaniteGeometryOptions &);
} // namespace cyuediff
