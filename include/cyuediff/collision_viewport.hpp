#pragma once
#include <cyuediff/collision_wireframe.hpp>
#include <cyuediff/visual_diff.hpp>
#include <cyui/modules/Viewport.h>
namespace cyuediff {
// Prepared geometry, one selected body's coordinate system, shared before/after camera.
class CollisionViewport {
    CollisionWireframe before_, after_;
    std::map<std::pair<std::string, std::string>, ChangeKind> changes_;
    std::string body_, shape_;
    cyui::ViewportState camera_;
    int navigation_pane_ = 0;
    bool comparing_ = false, changed_only_ = false, fit_pending_ = true, fit_changes_ = false;
    std::size_t rendered_edges_ = 0;
    bool visible(const CollisionWireShape &, bool changes = false) const;
    CollisionBounds bounds(bool changes = false) const;
    void fit(float aspect);
    void pane(const char *, const CollisionWireframe &, ImVec2, int, const cyui::OrbitCamera &);

  public:
    void set_models(CollisionWireframe before, CollisionWireframe after, const Json *before_snapshot,
                    const Json *after_snapshot, bool comparing);
    void select(const std::string &body, const std::string &shape = {});
    void set_changed_only(bool);
    bool focus_changes();
    std::size_t visible_edge_count(bool after = false) const;
    std::size_t rendered_edges() const { return rendered_edges_; }
    ChangeCounts changes() const;
    void draw(const char *id = "Collision geometry");
};
} // namespace cyuediff
