#pragma once
#include "container_document.hpp"
#include <cyuediff/property_view.hpp>
#include <cyuediff/property_browser_view.hpp>
#include <cyuediff/texture_view.hpp>
#include <cyuediff/mesh_view.hpp>
#include <cyuediff/mesh_skin_view.hpp>
#include <cyuediff/render_morph_view.hpp>
#include <cyuediff/cloth_view.hpp>
#include <cyuediff/physical_cloth_view.hpp>
#include <cyuediff/vertex_attribute_view.hpp>
#include <cyuediff/collision_view.hpp>
#include <cyuediff/cooked_animation_view.hpp>
#include <cyuediff/nanite_view.hpp>
namespace cyuediff::desktop {
enum class ContainerAction { none, inspect, select_object };
class ContainerView {
    ViewLayout layout_;
    std::shared_ptr<const ContainerDocument> document_;
    cyui::TextFilter filter_, object_filter_;
    struct MemberRow {
        const ContainerEntry *before = nullptr, *after = nullptr;
        std::string key, state;
        bool zen = false;
    };
    struct ObjectRow {
        std::string name, path, state, search;
        Json before = nullptr, after = nullptr, before_read = nullptr, after_read = nullptr;
        Json before_values = nullptr, after_values = nullptr;
    };
    struct PixelRow {
        std::string key;
        bool after = false;
        TextureView view;
    };
    struct MeshRow {
        std::string key;
        bool after = false;
        MeshView view;
        MeshSkinView skin;
        ClothView cloth;
        VertexAttributeView attributes;
        bool attributes_available = false;
        bool cloth_available = false;
        RenderMorphView morphs;
        bool morphs_available = false;
        bool skin_available = false, comparison_known = true;
    };
    PhysicalClothView physical_cloth_;
    int before_physical_cloth_ = -1, after_physical_cloth_ = -1;
    bool physical_cloth_present_ = false, physical_pair_explicit_ = false;
    void bind_physical_cloth();
    NaniteView nanite_;
    int before_nanite_ = -1, after_nanite_ = -1;
    bool nanite_present_ = false;
    void bind_nanite();
    CookedAnimationView animation_;
    int before_animation_ = -1, after_animation_ = -1;
    bool animation_present_ = false;
    void bind_animation();
    CollisionView physics_;
    bool physics_present_ = false;
    CollisionView collisions_;
    bool collisions_present_ = false;
    std::vector<MeshRow> mesh_rows_;
    int mesh_ = 0;
    bool mesh_skin_ = false, mesh_morphs_ = false, mesh_cloth_ = false, mesh_attributes_ = false;
    std::vector<PixelRow> pixel_rows_;
    int pixel_ = 0;
    std::vector<MemberRow> member_rows_;
    std::vector<ObjectRow> object_rows_;
    std::vector<int> members_, objects_;
    std::vector<std::string> diagnostics_;
    int member_ = -1, object_ = -1, pending_object_ = -1, initial_tab_ = 0, active_tab_ = 0;
    bool select_tab_ = false, packages_only_ = false, changed_only_ = false, compare_objects_ = false;
    bool property_mode_ = false, properties_comparing_ = false, property_available_ = false;
    std::string property_notice_;
    Json object_data_, after_object_data_, schema_details_;
    PropertyView inspection_, properties_, values_, comparison_, schema_;
    PropertyBrowserView property_browser_;
    bool browse_properties_ = false;
    std::pair<const Json *, const Json *> property_snapshots() const;
    void rebuild_members();
    void rebuild_objects();

  public:
    Json view_state() const;
    void restore_view_state(const Json &);
    static void register_translations();
    void set_document(std::shared_ptr<const ContainerDocument>);
    void set_filter(std::string);
    void set_packages_only(bool);
    void set_changed_only(bool);
    void set_object_filter(std::string);
    bool select_member(const std::string &);
    bool select_tab(int);
    void set_property_mode(bool value) {
        property_mode_ = value;
    }
    const PropertyTable &property_table() const {
        return browse_properties_ ? property_browser_.properties().table() : values_.table();
    }
    Json property_report() const;
    bool set_property_scope(const std::string &, std::size_t first = 0);
    bool set_pixel_preview(const std::string &, bool after, const cyui::ImageData &);
    bool set_pixel_difference(const ContainerTextureDifference &, const cyui::ImageData &);
    std::size_t pixel_count() const {
        return pixel_rows_.size();
    }
    void poll() {
        animation_.poll();
        nanite_.poll();
    }
    bool busy() const {
        return animation_.busy() || nanite_.busy();
    }
    Json physical_cloth_report() const {
        return physical_cloth_present_ ? physical_cloth_.report() : Json(nullptr);
    }
    Json animation_report() const;
    Json nanite_report() const;
    Json mesh_report() const;
    Json physics_report() const {
        return physics_present_ ? physics_.report() : Json(nullptr);
    }
    Json collision_report() const {
        return collisions_present_ ? collisions_.report() : Json(nullptr);
    }
    bool select_object(std::size_t);
    void apply_object_selection();
    const ContainerEntry *selected_member() const;
    std::string selected_key() const;
    std::size_t visible_count() const {
        return members_.size();
    }
    bool comparing_objects() const {
        return compare_objects_;
    }
    std::size_t visible_object_count() const {
        return objects_.size();
    }
    int selected_object() const {
        return object_;
    }
    int active_tab() const {
        return active_tab_;
    }
    ContainerAction draw(bool busy);
};
} // namespace cyuediff::desktop
