#pragma once
#include <cyuediff/skeleton.hpp>
#include <cyuediff/graph_inspector.hpp>
namespace cyuediff {
// Owned snapshots; hierarchy and typed details are prepared when binding/selecting.
class SkeletonView {
    Json before_, after_;
    std::vector<std::string> objects_;
    std::string object_, rendered_locale_;
    GraphInspector inspector_;
    PropertyView metadata_;
    cyui::NodeState state_;
    std::vector<cyui::Node> nodes_;
    std::vector<cyui::NodeLink> links_;
    std::map<cyui::ItemId, ChangeKind> changes_;
    cyui::Point low_{}, high_{};
    bool comparing_ = false, complete_ = false, fit_ = true, truncated_ = false;
    const Json *body(const Json &) const;
    void bind();

  public:
    void set_packages(const Package *before, const Package *after = nullptr, bool comparing = false);
    bool empty() const { return objects_.empty(); }
    bool select_object(const std::string &);
    bool select_bone(const std::string &);
    bool next_changed_bone(bool previous = false);
    void set_filter(std::string);
    void set_changed_only(bool);
    std::size_t visible_count() const { return inspector_.visible_count(); }
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Skeleton");
};
} // namespace cyuediff
