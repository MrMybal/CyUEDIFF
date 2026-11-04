#pragma once
#include <cyuediff/visual_diff.hpp>
#include <cyui/Core.h>
namespace cyuediff {
class PropertyView {
    PropertyTable table_;
    cyui::TextFilter filter_;
    std::vector<int> visible_;
    bool comparing_ = false, changed_only_ = false;
    void rebuild();

  public:
    void set_values(const Json *before, const Json *after, bool comparing);
    const PropertyTable &table() const {
        return table_;
    }
    void draw(const char *id = "Properties");
};
} // namespace cyuediff
