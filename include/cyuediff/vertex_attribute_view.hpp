#pragma once
#include <cyuediff/vertex_attributes.hpp>
#include <cyuediff/property_view.hpp>
#include <optional>
namespace cyuediff {
class VertexAttributeView {
    struct Row {
        std::uint32_t vertex;
        ChangeKind change;
        std::string search;
    };
    std::shared_ptr<const MeshVertexAttributes> before_, after_;
    std::vector<std::string> channels_;
    std::string channel_;
    std::vector<Row> rows_;
    std::vector<std::size_t> visible_;
    std::optional<std::uint32_t> vertex_;
    cyui::TextFilter filter_;
    PropertyView details_;
    bool comparing_ = false, changed_only_ = false, focus_pending_ = false, selected_visible_ = false;
    std::size_t rendered_ = 0;
    void rebuild();
    void refilter();
    void bind_selection();

  public:
    static void register_translations();
    void set_attributes(std::shared_ptr<const MeshVertexAttributes>,
                        std::shared_ptr<const MeshVertexAttributes> = {}, bool comparing = true);
    bool select_channel(const std::string &);
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
    void draw(const char *id = "Vertex attributes");
};
} // namespace cyuediff
