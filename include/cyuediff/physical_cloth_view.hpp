#pragma once
#include <cyuediff/physical_cloth.hpp>
#include <cyuediff/mesh_view.hpp>
#include <cyuediff/property_view.hpp>
namespace cyuediff {
// Bind immutable decoded models on the owner thread; drawing performs no I/O or simulation.
class PhysicalClothView {
    struct Side {
        std::shared_ptr<const PhysicalClothAsset> asset;
        std::optional<std::uint32_t> lod;
        std::vector<Json> vertices;
        std::shared_ptr<const MeshGeometry> geometry;
        std::vector<std::array<float, 3>> wrapped;
        Json summary = nullptr;
    };
    struct Row {
        std::uint32_t vertex;
        ChangeKind change;
    };
    Side before_, after_;
    std::vector<Row> rows_;
    std::vector<std::size_t> visible_;
    std::optional<std::uint32_t> selected_;
    cyui::TextFilter filter_;
    PropertyView details_, summary_;
    MeshView mesh_;
    bool comparing_ = false, explicit_pair_ = false, known_ = false, complete_maps_ = false;
    bool after_side_ = false, changed_only_ = false, focus_ = false, selected_visible_ = false;
    int mode_ = 0, panel_ = 0;
    bool controls_open_ = false, compact_ = false;
    float details_height_ = 0;
    std::size_t rendered_ = 0;
    std::vector<std::string> diagnostics_;
    void prepare(Side &);
    void rebuild();
    void filter();
    void bind_selection();
    void bind_mesh();

  public:
    static void register_translations();
    void set_assets(std::shared_ptr<const PhysicalClothAsset> before,
                    std::shared_ptr<const PhysicalClothAsset> after = {}, bool comparing = false,
                    bool explicit_object_pair = false);
    bool select_lod(bool after, std::uint32_t);
    // 0: stored mesh, 1: transition from higher detail, 2: transition from lower detail.
    bool select_mode(int);
    bool select_vertex(std::uint32_t);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool next_change(bool previous = false);
    void set_after_side(bool);
    const PropertyTable &details() const {
        return details_.table();
    }
    Json report() const;
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Physical cloth");
};
} // namespace cyuediff
