#pragma once
#include <cyuediff/attachments_pose.hpp>
#include <cyuediff/property_view.hpp>
#include <cyuediff/reference_pose_viewport.hpp>
namespace cyuediff {
// Owned validated snapshots. Selection and drawing perform no package/file access.
class AttachmentsView {
    struct Entry {
        std::string label, bone, path;
        bool virtual_bone = false, known = true;
        ChangeKind change = ChangeKind::unchanged;
    };
    Json before_, after_, skeleton_before_, skeleton_after_, flat_before_, flat_after_;
    std::map<std::string, Entry> entries_;
    std::vector<std::string> visible_, sources_;
    cyui::TextFilter filter_;
    PropertyView details_;
    ReferencePoseViewport viewport_;
    SocketPose pose_before_, pose_after_;
    std::string owner_, selected_, source_before_, source_after_;
    bool comparing_ = false, input_before_ = false, input_after_ = false, changed_only_ = false,
         preview_ = false, complete_ = false, truncated_ = false, preview_truncated_ = false;
    const Json *owner(const Json &) const;
    void refresh(bool preserve = false);
    void rebuild();
    void bind_selection();
    bool select(const std::string &);

  public:
    static void register_translations();
    void set_models(Json before, Json after, const Json &skeleton_before, const Json &skeleton_after,
                    bool comparing, bool before_input, bool after_input);
    bool select_owner(const std::string &);
    bool select_socket(const std::string &);
    bool select_virtual_bone(const std::string &key);
    bool next_change(bool previous = false);
    void set_filter(std::string);
    void set_changed_only(bool);
    void set_preview(bool v) { preview_ = v; }
    bool select_sources(std::string before, std::string after);
    std::size_t visible_count() const { return visible_.size(); }
    bool selected_known() const;
    const SocketPose &displayed_pose(bool after = false) const { return after ? pose_after_ : pose_before_; }
    const PropertyTable &details() const { return details_.table(); }
    std::size_t rendered_origins() const { return viewport_.rendered_joints(); }
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Attachments");
};
} // namespace cyuediff
