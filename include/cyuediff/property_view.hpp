#pragma once
#include <cyuediff/view_state.hpp>
#include <cyuediff/visual_diff.hpp>
#include <cyui/Core.h>
namespace cyuediff {
class PropertyView {
    PropertyTable table_;
    cyui::TextFilter filter_;
    std::vector<int> visible_;
    int selected_ = -1;
    bool comparing_ = false, changed_only_ = false;
    void rebuild();

  public:
    // Owner-thread snapshot/restore after binding the current data and previews; no file I/O.
    Json view_state() const;
    void restore_view_state(const Json &);
    static void register_translations();
    void set_filter(std::string);
    void set_changed_only(bool);
    bool select_row(std::size_t);
    std::size_t visible_count() const {
        return visible_.size();
    }
    const PropertyRow *selected_row() const {
        return selected_ < 0 ? nullptr : &table_.rows[static_cast<std::size_t>(selected_)];
    }
    void set_values(const Json *before, const Json *after, bool comparing);
    const PropertyTable &table() const {
        return table_;
    }
    void draw(const char *id = "Properties");
};
} // namespace cyuediff
