#pragma once
#include <cyuediff/property_view.hpp>
#include <map>
namespace cyuediff {
// Owner-thread view of one scoped Skeleton animation body; no package pointers or I/O.
class AnimationMetadataView {
    struct Entry {
        std::string name, container;
        bool marker = false, known = true;
        ChangeKind change = ChangeKind::unchanged;
    };
    Json before_ = Json::object(), after_ = Json::object();
    std::map<std::string, Entry> entries_;
    std::vector<std::string> visible_;
    cyui::TextFilter filter_;
    PropertyView details_;
    std::string selected_;
    bool comparing_ = false, changed_only_ = false, complete_ = false, truncated_ = false;
    void rebuild();
    void bind_selection();
    bool select(const std::string &);

  public:
    static void register_translations();
    void set_models(const Json *before, const Json *after = nullptr, bool comparing = false);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool select_curve(const std::string &container, const std::string &name);
    bool select_marker(const std::string &name);
    bool next_change(bool previous = false);
    bool selected_known() const;
    std::size_t visible_count() const { return visible_.size(); }
    const PropertyTable &details() const { return details_.table(); }
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Animation metadata");
};
} // namespace cyuediff
