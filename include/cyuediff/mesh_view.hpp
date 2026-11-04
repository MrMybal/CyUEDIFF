#pragma once
#include <cyuediff/view_state.hpp>
#include <cyuediff/visual_diff.hpp>
#include <cyui/modules/Viewport.h>
#include <memory>
namespace cyuediff {
// CPU wireframe with CyUI orbit navigation. Geometry is prepared outside draw().
class MeshView {
    std::shared_ptr<const MeshGeometry> mesh_;
    struct Edge {
        std::array<cyui::Vec3, 2> points;
        ChangeKind change;
    };
    std::vector<Edge> edges_;
    ChangeCounts counts_;
    bool comparing_ = false;
    cyui::ViewportState state_;
    cyui::Vec3 center_{};
    std::size_t rendered_edges_ = 0;
    double radius_ = 1;
    bool fit_pending_ = true, truncated_ = false;
    void fit(float aspect);

  public:
    // Owner-thread snapshot/restore after binding the current data and previews; no file I/O.
    Json view_state() const;
    void restore_view_state(const Json &);
    void set_mesh(std::shared_ptr<const MeshGeometry> mesh, std::shared_ptr<const MeshGeometry> peer = {},
                  bool after = false, bool comparing = false);
    const ChangeCounts &changes() const {
        return counts_;
    }
    std::size_t rendered_edges() const {
        return rendered_edges_;
    }
    bool empty() const {
        return !mesh_;
    }
    void draw(const char *id = "CyUEDiffMesh");
};
} // namespace cyuediff
