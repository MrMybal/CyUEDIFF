#pragma once
#include "history.hpp"
#include <cyui/Core.h>
namespace cyuediff::desktop {
enum class HistoryAction { none, refresh, previous, next, before, after, compare, close };
class HistoryView {
    std::shared_ptr<const HistoryDocument> document_;
    cyui::TextFilter filter_;
    std::vector<int> visible_;
    int selected_ = -1;
    void rebuild();

  public:
    static void register_translations();
    void set_document(std::shared_ptr<const HistoryDocument>);
    void set_filter(std::string);
    bool select_commit(const std::string &);
    const GitCommit *selected() const;
    std::size_t visible_count() const {
        return visible_.size();
    }
    HistoryAction draw(bool busy, const std::string &error, const std::string &before,
                       const std::string &after, bool can_compare);
};
} // namespace cyuediff::desktop
