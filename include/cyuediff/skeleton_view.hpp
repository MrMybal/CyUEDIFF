#pragma once
#include <cyuediff/skeleton.hpp>
#include <cyuediff/animation_metadata_view.hpp>
#include <cyuediff/attachments_view.hpp>
#include <cyuediff/reference_pose_viewport.hpp>
#include <cyuediff/graph_inspector.hpp>
namespace cyuediff {
// Owned snapshots; hierarchy and typed details are prepared when binding/selecting.
class SkeletonView {
    Json before_, after_, animation_before_, animation_after_;
    AnimationMetadataView animation_;
    AttachmentsView attachments_;
    bool attachments_preview_ = false;
    bool animation_preview_ = false;
    std::vector<std::string> objects_;
    std::string object_, rendered_locale_;
    GraphInspector inspector_;
    PropertyView metadata_, pose_details_;
    ReferencePoseViewport pose_view_;
    std::map<std::pair<std::string, std::string>, ReferencePose> poses_before_, poses_after_;
    ReferencePose selected_before_, selected_after_;
    std::string source_before_, source_after_;
    std::vector<std::string> retarget_sources_;
    std::string pose_selection_;
    bool pose_preview_ = false;
    cyui::NodeState state_;
    std::vector<cyui::Node> nodes_;
    std::vector<cyui::NodeLink> links_;
    std::map<cyui::ItemId, ChangeKind> changes_;
    cyui::Point low_{}, high_{};
    bool comparing_ = false, complete_ = false, fit_ = true, truncated_ = false;
    const Json *body(const Json &) const;
    void bind();
    void update_pose_selection();
    void bind_pose();

  public:
    void set_packages(const Package *before, const Package *after = nullptr, bool comparing = false);
    void set_pose_preview(bool enabled) {
        pose_preview_ = enabled;
        if (enabled) {
            animation_preview_ = false;
            attachments_preview_ = false;
        }
    }
    void set_animation_preview(bool enabled) {
        animation_preview_ = enabled;
        if (enabled) {
            pose_preview_ = false;
            attachments_preview_ = false;
        }
    }
    void set_attachments_preview(bool enabled) {
        attachments_preview_ = enabled;
        if (enabled) {
            animation_preview_ = false;
            pose_preview_ = false;
        }
    }
    AttachmentsView &attachments_view() { return attachments_; }
    AnimationMetadataView &animation_view() { return animation_; }
    bool pose_preview() const { return pose_preview_; }
    const ReferencePose &displayed_pose(bool after = false) const {
        return after ? selected_after_ : selected_before_;
    }
    std::size_t rendered_pose_joints() const { return pose_view_.rendered_joints(); }
    bool empty() const { return objects_.empty(); }
    bool select_object(const std::string &);
    bool select_bone(const std::string &);
    // Missing keys stay selected and unavailable; no raw-pose fallback.
    bool select_retarget_sources(std::string before, std::string after);
    bool next_changed_bone(bool previous = false);
    void set_filter(std::string);
    void set_changed_only(bool);
    std::size_t visible_count() const { return inspector_.visible_count(); }
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Skeleton");
};
} // namespace cyuediff
