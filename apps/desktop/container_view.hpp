#pragma once
#include "container_document.hpp"
#include <cyuediff/property_view.hpp>
namespace cyuediff::desktop {
enum class ContainerAction { none, inspect, select_object };
class ContainerView {
    std::shared_ptr<const ContainerDocument> document_;
    cyui::TextFilter filter_, object_filter_;
    struct MemberRow {
        const ContainerEntry *before = nullptr, *after = nullptr;
        std::string key, state;
        bool zen = false;
    };
    struct ObjectRow {
        std::string name, path, state, search;
        Json before = nullptr, after = nullptr;
    };
    std::vector<MemberRow> member_rows_;
    std::vector<ObjectRow> object_rows_;
    std::vector<int> members_, objects_;
    std::vector<std::string> diagnostics_;
    int member_ = -1, object_ = -1, pending_object_ = -1, initial_tab_ = 0, active_tab_ = 0;
    bool select_tab_ = false, packages_only_ = false, changed_only_ = false, compare_objects_ = false;
    Json object_data_, after_object_data_;
    PropertyView inspection_, properties_, comparison_;
    void rebuild_members();
    void rebuild_objects();

  public:
    static void register_translations();
    void set_document(std::shared_ptr<const ContainerDocument>);
    void set_filter(std::string);
    void set_packages_only(bool);
    void set_changed_only(bool);
    void set_object_filter(std::string);
    bool select_member(const std::string &);
    bool select_tab(int);
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
