#pragma once
#include <cyuediff/core.hpp>
#include <optional>
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
    bool attributes = false;
    OperationControl control;
};
struct NaniteTriangle {
    std::array<std::uint32_t, 3> vertices{};
    std::uint32_t material = 0;
};
struct NaniteVertexAttributes {
    std::uint32_t packed_normal = 0;
    std::optional<std::uint32_t> packed_tangent;
    unsigned normal_precision = 0, tangent_precision = 0;
    std::optional<std::array<float, 3>> normal;
    std::optional<std::array<float, 4>> tangent;
    std::array<std::uint8_t, 4> color{};
    std::vector<std::array<std::uint32_t, 2>> packed_uvs;
    std::vector<std::array<std::int32_t, 2>> quantized_uvs;
    std::vector<std::array<float, 2>> uvs;
    Json to_json() const;
};
class NaniteGeometry {
  public:
    struct Impl;
    bool attributes_decoded() const;
    NaniteVertexAttributes attributes(std::size_t page, std::size_t cluster, std::size_t vertex,
                                      const OperationControl & = {}) const;
    std::size_t page_count() const;
    std::uint64_t accounted_output_bytes() const;
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
// Decodes positions, oriented triangles and material indices. Vertex attributes require options.attributes;
// runtime LOD selection is excluded.
NaniteGeometry decode_nanite_geometry(const NaniteGeometrySource &, const NaniteGeometryOptions &);
} // namespace cyuediff
