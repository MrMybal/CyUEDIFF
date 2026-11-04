#pragma once
#include "workspace.hpp"
#include <cyui/Core.h>
namespace cyuediff::desktop {
struct DocumentTab {
    DocumentId id;
    std::string label, tooltip;
    bool busy = false, failed = false;
};
enum class DocumentTabOperation { none, select, close, create };
struct DocumentTabAction {
    DocumentTabOperation operation = DocumentTabOperation::none;
    DocumentId id = 0;
};
class DocumentTabsView {
    std::vector<DocumentTab> tabs_;
    std::vector<std::string> labels_;
    DocumentId active_ = 0, select_ = 0;
    bool can_create_ = false;

  public:
    static void register_translations();
    void set_tabs(std::vector<DocumentTab>, DocumentId active, bool can_create);
    DocumentTabAction draw();
};
} // namespace cyuediff::desktop
