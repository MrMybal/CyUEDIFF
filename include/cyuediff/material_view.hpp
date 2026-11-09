#pragma once
#include <cyuediff/view_layout.hpp>
#include <cyuediff/graph_inspector.hpp>
#include <cyuediff/view_state.hpp>
#include <cyuediff/visual_diff.hpp>
#include <cyui/modules/Nodes.h>
namespace cyuediff {
// Serialized material graphs and instance overrides, prepared outside the render loop.
class MaterialView {
    ViewLayout layout_;
    struct Document {
        std::string path, parent;
        bool instance = false, complete = false, outputs_known = false, needs_fit = true;
        std::vector<std::string> diagnostics;
        struct ParameterRow {
            std::string name, kind, identity, before, after;
            ChangeKind change;
        };
        std::vector<ParameterRow> parameters;
        cyui::TextFilter parameter_filter;
        bool changed_parameters_only = false;
        std::vector<std::size_t> visible_parameters;
        std::map<cyui::ItemId, ChangeKind> changes;
        ChangeCounts counts;
        bool comparing = false, comparison_unavailable = false;
        std::vector<cyui::Node> nodes;
        std::vector<cyui::NodeLink> links;
        cyui::NodeState state;
        GraphInspector inspector;
        cyui::Point low{}, high{};
        std::size_t pins = 0;
    };
    std::vector<Document> documents_;
    std::size_t selected_ = 0;
    static void rebuild_parameters(Document &);

  public:
    // Owner-thread snapshot/restore after binding the current data and previews; no file I/O.
    Json view_state() const;
    void restore_view_state(const Json &);
    void set_package(const Package &, const Package *peer = nullptr, bool after = false);
    bool comparison_available() const { return !documents_.empty() && documents_[selected_].comparing; }
    bool empty() const {
        return documents_.empty();
    }
    bool select_object(const std::string &path);
    bool select_node(const std::string &material, const std::string &node);
    bool next_changed_node(bool previous = false);
    bool set_parameter_filter(std::string query, bool changed_only = false);
    std::size_t visible_parameter_count() const;
    void draw(const char *id = "Material");
};
} // namespace cyuediff
