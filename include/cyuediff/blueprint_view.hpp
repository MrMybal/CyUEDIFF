#pragma once
#include <cyuediff/visual_diff.hpp>
#include <cyui/modules/Nodes.h>
namespace cyuediff {
// Prepared read-only graph view. set_package performs all model conversion;
// draw only navigates the cached CyUI model and never reads files.
class BlueprintView {
    struct Graph {
        std::string id, path;
        bool complete = false, needs_fit = true;
        std::vector<std::string> diagnostics;
        std::vector<cyui::Node> nodes;
        std::map<cyui::ItemId, ChangeKind> changes;
        ChangeCounts counts;
        bool comparing = false;
        std::vector<cyui::NodeLink> links;
        cyui::NodeState state;
        cyui::Point low{}, high{};
        std::size_t pins = 0;
    };
    std::vector<Graph> graphs_;
    std::size_t selected_ = 0;

  public:
    void set_package(const Package &package, const Package *peer = nullptr, bool after = false);
    bool empty() const {
        return graphs_.empty();
    }
    void draw(const char *id = "Blueprint");
};
} // namespace cyuediff
