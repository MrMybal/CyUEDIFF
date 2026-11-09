#pragma once
#include <cyuediff/property_page.hpp>
#include <cyuediff/property_view.hpp>
namespace cyuediff {
// Owner-thread browser. Source snapshots are supplied for each operation and never retained.
class PropertyBrowserView {
    PropertyPage page_;
    PropertyView properties_;
    bool comparing_ = false, serialization_fields_ = false;
    std::int64_t jump_ = 0;
    std::string error_;
    bool bind(const Json *, const Json *, const std::string &, std::size_t, bool, bool);

  public:
    static void register_translations();
    void set_values(const Json *, const Json *, bool comparing);
    bool navigate(const Json *, const Json *, const std::string &, std::size_t first = 0);
    bool open_selected(const Json *, const Json *);
    bool go_to_index(const Json *, const Json *, std::size_t);
    void draw(const Json *, const Json *, const char *id = "Property browser");
    Json view_state() const;
    void restore_view_state(const Json *, const Json *, const Json &);
    const PropertyPage &page() const {
        return page_;
    }
    PropertyView &properties() {
        return properties_;
    }
    const PropertyView &properties() const {
        return properties_;
    }
};
} // namespace cyuediff
