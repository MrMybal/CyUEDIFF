#pragma once
#include <cyuediff/core.hpp>
#include <cyuediff/blueprint_view.hpp>
#include <cyuediff/texture_view.hpp>
#include <cyuediff/mesh_view.hpp>
#include <cyuediff/material_view.hpp>
#include <cyui/modules/Diff.h>
#include <memory>
#include <optional>
namespace cyuediff {
// The host owns the window/backend and loads packages outside its render loop.
// No platform window, Unreal dependency or file I/O is introduced by draw().
enum class AssetTab { properties, blueprint, materials, textures, meshes, comparison };
class AssetView {
    std::shared_ptr<const Package> before_, after_;
    cyui::DiffDocument document_;
    cyui::DiffState diff_state_;
    cyui::TextFilter filter_;
    std::size_t selected_ = 0;
    bool properties_after_ = false, comparison_ = false;
    std::vector<std::string> before_properties_, after_properties_;
    std::optional<AssetTab> requested_tab_;
    BlueprintView before_graph_, after_graph_;
    MaterialView before_material_, after_material_;
    bool material_after_ = false;
    MeshView before_mesh_, after_mesh_;
    bool mesh_after_ = false;
    bool graph_after_ = false, texture_after_ = false;
    std::size_t texture_selected_ = 0;
    std::vector<TextureView> before_textures_, after_textures_;

  public:
    static void register_translations();
    // compare_missing_side enables an addition/removal diff when one side is absent.
    void set_packages(std::shared_ptr<const Package> before, std::shared_ptr<const Package> after = {},
                      bool compare_missing_side = false);
    bool set_texture_preview(const std::string &object_path, const cyui::ImageData &, bool after = false);
    bool clear_texture_preview(const std::string &object_path, bool after = false);
    void set_mesh_geometry(std::shared_ptr<const MeshGeometry> geometry, bool after = false);
    void select_tab(AssetTab tab) { requested_tab_ = tab; }
    void draw(const char *id = "CyUEDiff", bool show_language = true);
};
} // namespace cyuediff
