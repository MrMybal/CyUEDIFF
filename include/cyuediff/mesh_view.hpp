#pragma once
#include <cyuediff/core.hpp>
#include <cyui/modules/Viewport.h>
#include <memory>
namespace cyuediff {
// CPU wireframe with CyUI orbit navigation. Geometry is prepared outside draw().
class MeshView {
    std::shared_ptr<const MeshGeometry> mesh_;
    std::vector<std::array<cyui::Vec3, 2>> edges_;
    cyui::ViewportState state_;
    cyui::Vec3 center_{};
    std::size_t rendered_edges_ = 0;
    double radius_ = 1;
    bool fit_pending_ = true, truncated_ = false;
    void fit(float aspect);

  public:
    void set_mesh(std::shared_ptr<const MeshGeometry> mesh);
    std::size_t rendered_edges() const {
        return rendered_edges_;
    }
    bool empty() const {
        return !mesh_;
    }
    void draw(const char *id = "CyUEDiffMesh");
};
} // namespace cyuediff
