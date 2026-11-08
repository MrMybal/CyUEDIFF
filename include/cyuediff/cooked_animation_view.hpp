#pragma once
#include <cyuediff/cooked_animation.hpp>
#include <cyuediff/property_view.hpp>
namespace cyuediff {
// Immutable Core inputs; all sampling/catalogue/key preparation runs on a cancellable
// latest-request worker. Owner-thread setters/poll/draw never open files or decode assets.
class CookedAnimationView {
    struct Worker;
    struct Review;
    std::unique_ptr<Worker> worker_;
    std::shared_ptr<const Review> review_;
    std::shared_ptr<const CookedAnimation> before_, after_;
    cyui::TextFilter filter_;
    PropertyView details_;
    Json details_state_ = nullptr;
    std::vector<std::size_t> visible_;
    std::string selected_, error_;
    double frame_ = 0, after_frame_ = 0, position_tolerance_ = 0, rotation_tolerance_ = 0,
           scale_tolerance_ = 0;
    std::uint32_t keys_[2]{};
    unsigned component_ = 0;
    std::uint64_t generation_ = 0;
    bool comparing_ = false, independent_ = false, changed_only_ = false, busy_ = false;
    std::size_t rendered_rows_ = 0;
    bool selected_visible_ = false, scroll_selection_ = false;
    void request();
    void rebuild();
    void inspect();
    void chart();

  public:
    CookedAnimationView();
    ~CookedAnimationView();
    CookedAnimationView(CookedAnimationView &&) noexcept;
    CookedAnimationView &operator=(CookedAnimationView &&) noexcept;
    CookedAnimationView(const CookedAnimationView &) = delete;
    CookedAnimationView &operator=(const CookedAnimationView &) = delete;
    static void register_translations();
    void set_animations(std::shared_ptr<const CookedAnimation> before,
                        std::shared_ptr<const CookedAnimation> after = {}, bool comparing = false);
    bool poll();
    bool busy() const {
        return busy_;
    }
    bool select_track(const std::string &);
    void set_frames(double before, double after, bool independent);
    void set_component(unsigned);
    void set_key(std::uint32_t, bool after = false);
    void set_tolerances(double position, double rotation_degrees, double scale);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool next_change(bool previous = false);
    Json view_state() const;
    void restore_view_state(const Json &);
    Json report() const;
    void draw(const char *id = "Cooked animation");
};
} // namespace cyuediff
