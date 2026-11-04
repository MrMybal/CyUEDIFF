#pragma once
#include <cyuediff/view_state.hpp>
#include <cyui/Core.h>
namespace cyuediff {
// Owner-thread UI layout. Only validated proportions and section flags cross the host boundary.
class ViewLayout {
    Json state_ = Json::object();
    unsigned generation_ = 0;
    std::string columns_;

  public:
    const Json &snapshot() const {
        return state_;
    }
    void restore(const Json &state, std::string_view kind);
    bool section(const char *key, const char *label, bool default_open = false, bool tree = false);
    bool begin_table(const char *id, const char *key, int count, ImGuiTableFlags flags = 0, ImVec2 size = {});
    void column(const char *label, int index, float default_weight = 1);
    void end_table();
    float preview_fraction(float fallback) const;
    void resize_preview(float available_height);
    float sidebar_width() const;
    void resize_sidebar();
};
} // namespace cyuediff
