#pragma once
#include <cyuediff/cooked_animation.hpp>
#include <cyuediff/property_view.hpp>
namespace cyuediff {
// Immutable Core input; latest-generation worker prepares comparisons, keys and traces.
class CookedCurveView {
    struct Worker;
    struct Review;
    std::unique_ptr<Worker> worker_;
    std::shared_ptr<const Review> review_;
    std::shared_ptr<const CookedAnimation> models_[2];
    cyui::TextFilter filter_;
    PropertyView details_;
    Json details_state_ = nullptr;
    std::string selected_, error_;
    std::vector<std::size_t> visible_;
    double times_[2]{}, tolerance_ = 0, default_ = 0, range_[2]{0, 1};
    std::uint32_t keys_[2]{};
    std::uint64_t generation_ = 0;
    bool comparing_ = false, independent_ = false, changed_only_ = false, busy_ = false;
    std::size_t rendered_rows_ = 0;
    bool selected_visible_ = false, scroll_selection_ = false;
    void request();
    void rebuild();
    void inspect();
    void chart();

  public:
    CookedCurveView();
    ~CookedCurveView();
    CookedCurveView(CookedCurveView &&) noexcept;
    CookedCurveView &operator=(CookedCurveView &&) noexcept;
    CookedCurveView(const CookedCurveView &) = delete;
    CookedCurveView &operator=(const CookedCurveView &) = delete;
    static void register_translations();
    void set_animations(std::shared_ptr<const CookedAnimation>, std::shared_ptr<const CookedAnimation> = {},
                        bool comparing = false);
    bool poll();
    bool busy() const {
        return busy_;
    }
    bool select_curve(const std::string &);
    void set_times(double before, double after, bool independent);
    void set_tolerance(double);
    void set_empty_default(double);
    void set_range(double start, double end);
    void set_key(std::uint32_t, bool after = false);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool next_change(bool previous = false);
    Json view_state() const;
    void restore_view_state(const Json &);
    Json report() const;
    void draw(const char *id = "Cooked scalar curves");
};
} // namespace cyuediff
