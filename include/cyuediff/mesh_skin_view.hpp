#pragma once
#include <cyuediff/mesh_skin.hpp>
#include <cyuediff/property_view.hpp>
namespace cyuediff {
// Prepared immutable skin snapshots, a clipped identity list, and focused typed details.
// No I/O, normalization, hierarchy evaluation or geometry reconstruction in draw().
class MeshSkinView {
    struct Entry {
        std::string key, label, search;
        bool vertex = false;
        ChangeKind change = ChangeKind::unchanged;
    };
    Json before_ = Json::object(), after_ = Json::object();
    std::vector<Entry> entries_;
    std::vector<std::size_t> visible_;
    cyui::TextFilter filter_;
    PropertyView details_;
    std::string selected_;
    bool comparing_ = false, changed_only_ = false, focus_pending_ = false, selected_visible_ = false;
    std::size_t rendered_rows_ = 0;
    void rebuild();
    void bind_selection();
    bool select(const std::string &);

  public:
    static void register_translations();
    void set_skins(std::shared_ptr<const MeshSkin> before, std::shared_ptr<const MeshSkin> after = {},
                   bool comparing = false);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool select_bone(const std::string &);
    bool select_vertex(std::uint32_t);
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
    void draw(const char *id = "Mesh skin");
};
} // namespace cyuediff
