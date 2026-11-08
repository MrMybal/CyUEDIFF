#pragma once
#include <cyuediff/cloth.hpp>
#include <cyuediff/property_view.hpp>
#include <optional>
namespace cyuediff {
class ClothView {
    struct Row {
        std::uint32_t vertex;
        ChangeKind change;
        std::string search;
    };
    Json before_, after_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> mappings_;
    std::vector<Row> rows_;
    std::vector<std::size_t> visible_;
    std::uint32_t section_ = 0, bias_ = 0;
    std::optional<std::uint32_t> vertex_;
    cyui::TextFilter filter_;
    PropertyView details_;
    bool comparing_ = false, changed_only_ = false, focus_pending_ = false, selected_visible_ = false;
    std::size_t rendered_ = 0;
    void rebuild();
    void bind_selection();

  public:
    static void register_translations();
    void set_cloths(std::shared_ptr<const MeshCloth>, std::shared_ptr<const MeshCloth> = {},
                    bool comparing = false);
    bool select_mapping(std::uint32_t section, std::uint32_t lod_bias);
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
    void draw(const char *id = "Cloth mappings");
};
} // namespace cyuediff
