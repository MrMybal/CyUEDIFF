#pragma once
#include <cyuediff/catalog.hpp>
#include <cyui/Core.h>
#include <limits>
namespace cyuediff {
enum class CatalogAction { none, open, use_before, use_after };
class CatalogView {
    std::shared_ptr<const std::vector<CatalogItem>> items_;
    std::vector<std::size_t> filtered_;
    cyui::TextFilter filter_;
    std::size_t selected_ = std::numeric_limits<std::size_t>::max();
    int kind_ = 0;
    bool revisions_ = false, dirty_ = true;
    void filter();

  public:
    static void register_translations();
    void set_items(std::shared_ptr<const std::vector<CatalogItem>>, bool revisions = false);
    void set_filter(std::string text);
    bool select(std::size_t);
    std::size_t selected() const {
        return selected_;
    }
    std::size_t visible_count();
    // Metadata only: draw never scans directories, reads packages or calls Git.
    CatalogAction draw(const char *id = "CyUEDiff catalog");
};
} // namespace cyuediff
