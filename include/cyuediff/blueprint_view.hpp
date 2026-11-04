#pragma once
#include <cyuediff/view_layout.hpp>
#include <cyuediff/graph_inspector.hpp>
#include <cyuediff/view_state.hpp>
#include <cyuediff/visual_diff.hpp>
#include <cyui/modules/Nodes.h>
namespace cyuediff {
// Prepared read-only graph view. set_package performs all model conversion;
// draw only navigates the cached CyUI model and never reads files.
class BlueprintView {
    ViewLayout layout_;
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
        GraphInspector inspector;
        cyui::Point low{}, high{};
        std::size_t pins = 0;
    };
    std::vector<Graph> graphs_;
    std::size_t selected_ = 0;

  public:
    // Owner-thread snapshot/restore after binding the current data and previews; no file I/O.
    Json view_state() const;
    void restore_view_state(const Json &);
    void set_package(const Package &package, const Package *peer = nullptr, bool after = false);
    bool select_node(const std::string &graph, const std::string &node);
    bool next_changed_node(bool previous = false);
    bool empty() const {
        return graphs_.empty();
    }
    void draw(const char *id = "Blueprint");
};
} // namespace cyuediff
