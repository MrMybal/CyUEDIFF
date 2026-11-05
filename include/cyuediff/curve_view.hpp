#pragma once
#include <cyuediff/curves.hpp>
#include <cyuediff/property_view.hpp>
namespace cyuediff {
class CurveView {
    struct Entry {
        double time = 0;
        bool known = false;
        ChangeKind change = ChangeKind::unchanged;
    };
    Json owners_[2], values_[2];
    bool present_[2]{}, input_[2]{}, comparing_ = false, changed_only_ = false, truncated_ = false;
    std::string objects_[2], lane_, selected_;
    std::map<std::string, Entry> entries_;
    std::vector<std::string> lanes_, visible_;
    std::vector<std::array<double, 3>> plot_[2];
    std::vector<std::array<double, 2>> markers_[2];
    double time_ = 0, low_ = 0, high_ = 0;
    cyui::TextFilter filter_;
    PropertyView details_;
    const Json *owner(bool) const;
    const Json *lane(bool) const;
    const Json *key(bool, const std::string &) const;
    void bind();
    void rebuild();
    void prepare_plot();
    void sample();
    void bind_details();
    void draw_plot();

  public:
    static void register_translations();
    void set_packages(const Package *, const Package * = nullptr, bool comparing = false);
    bool empty() const;
    bool select_objects(std::string, std::string);
    bool select_lane(const std::string &);
    bool select_key(const std::string &);
    void set_time(double);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool next_change(bool previous = false);
    bool selected_known() const;
    std::size_t visible_count() const { return visible_.size(); }
    const Json &value(bool after = false) const { return values_[after ? 1 : 0]; }
    const PropertyTable &details() const { return details_.table(); }
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Stored curves");
};
} // namespace cyuediff
