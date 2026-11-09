#include <cyuediff/curve_view.hpp>
#include <cyuediff/animation_source_view.hpp>
#include <cyuediff/animation_pose_view.hpp>
#pragma once
#include <cyuediff/view_layout.hpp>
#include <cyuediff/view_state.hpp>
#include <cyuediff/core.hpp>
#include <cyuediff/blueprint_view.hpp>
#include <cyuediff/texture_view.hpp>
#include <cyuediff/mesh_view.hpp>
#include <cyuediff/material_view.hpp>
#include <cyuediff/property_view.hpp>
#include <cyuediff/property_browser_view.hpp>
#include <cyuediff/collision_view.hpp>
#include <cyuediff/skeleton_view.hpp>
#include <cyui/modules/Diff.h>
#include <memory>
#include <optional>
namespace cyuediff {
// The host owns the window/backend and loads packages outside its render loop.
// No platform window, Unreal dependency or file I/O is introduced by draw().
enum class AssetTab { properties, blueprint, materials, textures, meshes, comparison, collisions, skeleton, animations, curves };
class AssetView {
    ViewLayout layout_;
    std::shared_ptr<const Package> before_, after_;
    cyui::DiffDocument document_;
    cyui::DiffState diff_state_;
    bool text_comparison_available_ = false;
    cyui::TextFilter filter_;
    std::size_t selected_ = 0;
    bool properties_after_ = false, comparison_ = false, raw_properties_ = false;
    std::vector<PropertyView> before_inspection_, after_inspection_;
    PropertyBrowserView property_browser_;
    bool browse_properties_ = false, browser_after_ = false;
    int browser_root_ = 0, bound_browser_root_ = -1;
    std::string browser_object_;
    struct BrowserInputs {
        const Json *before = nullptr, *after = nullptr;
        bool comparing = false, unknown = false;
    };
    BrowserInputs property_browser_inputs() const;
    void bind_property_browser();
    std::shared_ptr<const MeshGeometry> before_geometry_, after_geometry_;
    std::vector<std::string> before_properties_, after_properties_;
    std::optional<AssetTab> requested_tab_;
    AssetTab active_tab_ = AssetTab::properties;
    BlueprintView before_graph_, after_graph_;
    MaterialView before_material_, after_material_;
    CollisionView collisions_;
    SkeletonView skeleton_;
    AnimationSourceView animations_;
    AnimationPoseView animation_pose_, animation_sockets_;
    bool sockets_bound_ = false, sockets_preview_ = false;
    bool pose_bound_ = false, pose_preview_ = false;
    CurveView curves_;
    bool material_after_ = false;
    MeshView before_mesh_, after_mesh_;
    bool mesh_after_ = false, skin_inspection_ = false, morph_inspection_ = false;
    PropertyView skin_, morphs_;
    bool graph_after_ = false, texture_after_ = false;
    std::size_t texture_selected_ = 0;
    std::vector<TextureView> before_textures_, after_textures_;

  public:
    // Owner-thread snapshot/restore after binding the current data and previews; no file I/O.
    Json view_state() const;
    bool set_property_scope(const std::string &, std::size_t first = 0, int root = 0);
    bool text_comparison_available() const {
        return text_comparison_available_;
    }
    const PropertyBrowserView &property_browser() const {
        return property_browser_;
    }
    void restore_view_state(const Json &);
    static void register_translations();
    // compare_missing_side enables an addition/removal diff when one side is absent.
    // Paired reads that failed a source limit/count guard reject before changing the view.
    void set_packages(std::shared_ptr<const Package> before, std::shared_ptr<const Package> after = {},
                      bool compare_missing_side = false);
    // Prepared by the host worker; drawing never resolves an external Skeleton.
    void set_animation_poses(const AnimationPose *before, const AnimationPose *after = nullptr, bool comparing = false);
    void set_animation_socket_poses(const AnimationSocketPose *before, const AnimationSocketPose *after = nullptr, bool comparing = false);
    bool set_texture_preview(const std::string &object_path, const cyui::ImageData &, bool after = false);
    bool set_texture_difference(const std::string &before_path, const std::string &after_path,
                                const cyui::ImageData &, const ImageComparison &);
    bool set_texture_difference(const std::string &before_path, const std::string &after_path,
                                const cyui::ImageData &, const HdrImageComparison &);
    bool clear_texture_preview(const std::string &object_path, bool after = false);
    void set_mesh_geometry(std::shared_ptr<const MeshGeometry> geometry, bool after = false);
    AssetTab active_tab() const { return requested_tab_.value_or(active_tab_); }
    void select_tab(AssetTab tab) { requested_tab_ = tab; }
    void draw(const char *id = "CyUEDiff", bool show_language = true);
};
} // namespace cyuediff
