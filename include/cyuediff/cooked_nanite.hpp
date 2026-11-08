#pragma once
#include <cyuediff/nanite_geometry.hpp>
namespace cyuediff {
namespace detail {
struct NaniteResourceReader;
}
struct CookedNaniteOptions : NaniteGeometryOptions {
    std::uint32_t max_hierarchy_nodes = 65536, max_dependencies = 4000000, max_imposter_values = 4000000;
};
class CookedNaniteResource {
  public:
    struct Impl;
    Json information(const OperationControl & = {}) const;
    Json catalog(const OperationControl & = {}) const;
    std::size_t page_count() const;
    Json page_metadata(std::size_t, const OperationControl & = {}) const;
    Json hierarchy_node(std::size_t, const OperationControl & = {}) const;
    std::size_t hierarchy_node_count() const;
    bool geometry_known() const;
    std::string encoding_sha256() const;
    std::shared_ptr<const NaniteGeometry> geometry() const;
    std::uint64_t accounted_output_bytes() const;

  private:
    std::shared_ptr<const Impl> impl_;
    explicit CookedNaniteResource(std::shared_ptr<const Impl>);
    friend struct detail::NaniteResourceReader;
};
struct NaniteTriangleReference {
    std::uint32_t page = 0, cluster = 0, triangle = 0;
};
struct NaniteSurfaceDiffOptions {
    std::uint64_t max_triangles = 4000000, max_output_bytes = 256ull * 1024 * 1024;
    OperationControl control;
    bool attributes = false;
};
enum NaniteAttributeChannel : std::uint8_t {
    nanite_normals = 1,
    nanite_tangents = 2,
    nanite_colors = 4,
    nanite_uvs = 8
};
struct NaniteAttributeChange {
    NaniteTriangleReference before, after;
    std::uint8_t changed = 0, unavailable = 0;
    bool ambiguous = false;
    // Corner i pairs triangle.vertices[(i + rotation) % 3] on each side.
    std::uint8_t before_rotation = 0, after_rotation = 0;
};
struct NaniteSurfaceComparison {
    Json summary;
    std::vector<NaniteTriangleReference> removed, added;
    std::vector<NaniteAttributeChange> modified;
    Json to_json(bool include_references = true) const;
};
// Exact native-float, orientation- and multiplicity-preserving full-leaf surface comparison.
// Encodings are separate. options.attributes additionally compares exact decoded shading values
// on common oriented surfaces, preserving duplicates. Unavailable channels cannot prove equality.
// Runtime LOD cuts and material evaluation remain excluded.
NaniteSurfaceComparison compare_nanite_surfaces(const CookedNaniteResource &, const CookedNaniteResource &,
                                                const NaniteSurfaceDiffOptions & = {});
} // namespace cyuediff
