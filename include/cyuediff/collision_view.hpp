#pragma once
#include <cyuediff/collision.hpp>
#include <cyuediff/collision_viewport.hpp>
#include <cyuediff/property_view.hpp>
namespace cyuediff {
class CollisionView {
    Json before_, after_;
    std::vector<std::string> bodies_, shapes_, preview_errors_;
    std::string body_, shape_;
    PropertyView properties_;
    CollisionViewport viewport_;
    bool comparing_ = false, complete_ = false;
    const Json *selected(const Json &, bool shape = true) const;
    void bind(bool retain = true);
    void bind_models(CollisionWireframe before, CollisionWireframe after);

  public:
    void set_packages(const Package *before, const Package *after = nullptr, bool comparing = false);
    // Prepared Core models; no decoding or file I/O on the draw thread.
    void set_models(Json before, Json after, CollisionWireframe before_preview,
                    CollisionWireframe after_preview, bool comparing = false);
    Json report() const;
    bool empty() const;
    bool select_body(const std::string &path);
    bool select_shape(const std::string &identity);
    bool next_changed_shape(bool previous = false);
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Collisions");
};
} // namespace cyuediff
