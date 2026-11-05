#pragma once
#include <cyuediff/skeleton_pose.hpp>
#include <cyuediff/visual_diff.hpp>
#include <cyuediff/view_state.hpp>
#include <cyui/modules/Viewport.h>
namespace cyuediff {
// Owned, bounded display positions. No package access or pose evaluation while drawing.
enum class PosePreviewSpace { reference, source_animation };
class ReferencePoseViewport {
    PosePreviewSpace space_ = PosePreviewSpace::reference;
    std::map<std::string, ReferenceJoint> before_, after_;
    std::map<std::string, ChangeKind> changes_;
    std::string selected_, picked_;
    Json differences_ = Json::object();
    cyui::ViewportState camera_;
    bool comparing_ = false, changed_only_ = false, fit_pending_ = true, incomplete_ = false,
         truncated_ = false;
    int navigation_pane_ = 0;
    std::size_t rendered_joints_ = 0;
    bool visible(const std::string &) const;
    void fit(float aspect);
    void pane(const char *, const std::map<std::string, ReferenceJoint> &, ImVec2, int,
              const cyui::OrbitCamera &);

  public:
    void set_models(const ReferencePose *before, const ReferencePose *after, bool comparing,
                    double position_tolerance = 0, PosePreviewSpace space = PosePreviewSpace::reference);
    void select(const std::string &name) { selected_ = name; }
    std::string take_picked_joint();
    const Json *joint_difference(const std::string &name) const {
        const auto it = differences_.find(name);
        return it == differences_.end() ? nullptr : &*it;
    }
    std::size_t visible_joint_count(bool after = false) const;
    std::size_t rendered_joints() const { return rendered_joints_; }
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Reference pose");
};
} // namespace cyuediff
