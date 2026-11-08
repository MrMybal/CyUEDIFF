#pragma once
#include <cyuediff/cooked_nanite.hpp>
#include <cyuediff/property_view.hpp>
#include <cyui/modules/Viewport.h>
namespace cyuediff {
// Immutable Core resources; a cancellable worker prepares comparisons, wire packets and focused values.
class NaniteView {
    struct Worker;
    struct Review;
    std::unique_ptr<Worker> worker_;
    std::shared_ptr<const Review> review_;
    std::shared_ptr<const CookedNaniteResource> models_[2];
    cyui::TextFilter filter_;
    PropertyView details_;
    ViewLayout layout_;
    Json details_state_ = nullptr;
    cyui::ViewportState camera_;
    std::vector<std::size_t> visible_;
    std::string error_;
    std::uint32_t page_ = 0, cluster_ = 0, vertex_ = 0, triangle_ = 0, node_ = 0;
    int inspector_ = 1;
    std::uint64_t generation_ = 0;
    bool compact_surface_ = true;
    bool comparing_ = false, after_ = false, cluster_preview_ = false, attributes_ = true,
         changed_only_ = false, busy_ = false, fit_ = true, scroll_ = false;
    std::size_t rendered_rows_ = 0, rendered_edges_ = 0;
    bool selected_visible_ = false, frame_capped_ = false;
    void request();
    void rebuild();
    void bind_details();
    void viewport();
    void inspector();

  public:
    NaniteView();
    ~NaniteView();
    NaniteView(NaniteView &&) noexcept;
    NaniteView &operator=(NaniteView &&) noexcept;
    NaniteView(const NaniteView &) = delete;
    NaniteView &operator=(const NaniteView &) = delete;
    static void register_translations();
    void set_resources(std::shared_ptr<const CookedNaniteResource>,
                       std::shared_ptr<const CookedNaniteResource> = {}, bool comparing = false);
    bool poll();
    bool busy() const {
        return busy_;
    }
    void set_after(bool);
    void set_cluster_preview(bool);
    void set_compare_attributes(bool);
    void select_cluster(std::uint32_t page, std::uint32_t cluster);
    void select_vertex(std::uint32_t);
    void select_triangle(std::uint32_t);
    void select_node(std::uint32_t);
    void set_inspector(int);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool next_change(bool previous = false);
    Json view_state() const;
    void restore_view_state(const Json &);
    Json report() const;
    void draw(const char *id = "Nanite");
};
} // namespace cyuediff
