#pragma once
#include <cyuediff/render_morphs.hpp>
#include <cyuediff/property_view.hpp>
#include <optional>
namespace cyuediff {
// Named targets, clipped sparse render-vertex list and focused decoded deltas.
class RenderMorphView {
    struct Row {
        std::uint32_t vertex;
        ChangeKind change;
        std::string search;
    };
    Json before_, after_;
    std::vector<std::string> targets_;
    std::vector<Row> rows_;
    std::vector<std::size_t> visible_;
    std::string target_;
    std::optional<std::uint32_t> vertex_;
    cyui::TextFilter filter_;
    PropertyView details_;
    bool comparing_ = false, ready_ = true, changed_only_ = false;
    bool focus_pending_ = false, selected_visible_ = false;
    std::size_t rendered_rows_ = 0;
    void rebuild();
    void bind_selection();

  public:
    static void register_translations();
    void set_models(std::shared_ptr<const MeshRenderMorphs>, std::shared_ptr<const MeshRenderMorphs> = {},
                    bool comparing = false);
    bool select_target(const std::string &);
    bool select_vertex(std::uint32_t);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool next_change(bool previous = false);
    std::size_t visible_count() const {
        return visible_.size();
    }
    const PropertyTable &details() const {
        return details_.table();
    }
    Json report() const;
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Render morphs");
};
} // namespace cyuediff
