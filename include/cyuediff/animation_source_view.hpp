#pragma once
#include <cyuediff/animation.hpp>
#include <cyuediff/property_view.hpp>
namespace cyuediff {
// Owned projections; binding and selection prepare data outside drawing.
class AnimationSourceView {
    struct Entry {
        std::string name;
        bool known = true;
        ChangeKind change = ChangeKind::unchanged;
    };
    Json roots_[2], samples_[2];
    bool present_[2]{}, input_complete_[2]{}, comparing_ = false, changed_only_ = false, truncated_ = false;
    std::string objects_[2], selected_;
    std::uint32_t frame_ = 0, after_frame_ = 0;
    bool independent_frames_ = false;
    double position_tolerance_ = 0, rotation_tolerance_ = 0, scale_tolerance_ = 0;
    Json frame_difference_ = Json::object();
    int component_ = 0;
    std::map<std::string, Entry> entries_;
    std::vector<std::string> visible_;
    std::vector<std::array<double, 2>> curves_[2];
    cyui::TextFilter filter_;
    PropertyView details_;
    const Json *model(bool after) const;
    const Json *track(bool after) const;
    void bind_models();
    void rebuild();
    void prepare();
    void sample();
    void chart();

  public:
    static void register_translations();
    void set_packages(const Package *before, const Package *after = nullptr, bool comparing = false);
    bool empty() const;
    bool select_models(std::string before, std::string after);
    bool select_track(const std::string &key);
    void set_frame(std::uint32_t);
    void set_after_frame(std::uint32_t);
    void set_independent_frames(bool);
    void set_tolerances(double position, double rotation_degrees, double scale);
    const Json &frame_difference() const { return frame_difference_; }
    void set_component(unsigned);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool next_change(bool previous = false);
    bool selected_known() const;
    std::size_t visible_count() const { return visible_.size(); }
    const Json &frame_sample(bool after = false) const { return samples_[after ? 1 : 0]; }
    std::size_t curve_samples(bool after = false) const { return curves_[after ? 1 : 0].size(); }
    const PropertyTable &details() const { return details_.table(); }
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Animation source");
};
} // namespace cyuediff
