#pragma once
#include <cyuediff/view_state.hpp>
#include <cyuediff/visual_diff.hpp>
#include <cyui/modules/Nodes.h>
namespace cyuediff {
// Serialized material graphs and instance overrides, prepared outside the render loop.
class MaterialView {
    struct Document {
        std::string path, parent;
        bool instance = false, complete = false, outputs_known = false, needs_fit = true;
        std::vector<std::string> diagnostics;
        struct ParameterRow {
            std::string name, kind, identity, before, after;
            ChangeKind change;
        };
        std::vector<ParameterRow> parameters;
        std::map<cyui::ItemId, ChangeKind> changes;
        ChangeCounts counts;
        bool comparing = false;
        std::vector<cyui::Node> nodes;
        std::vector<cyui::NodeLink> links;
        cyui::NodeState state;
        cyui::Point low{}, high{};
        std::size_t pins = 0;
    };
    std::vector<Document> documents_;
    std::size_t selected_ = 0;

  public:
    // Owner-thread snapshot/restore after binding the current data and previews; no file I/O.
    Json view_state() const;
    void restore_view_state(const Json &);
    void set_package(const Package &, const Package *peer = nullptr, bool after = false);
    bool empty() const {
        return documents_.empty();
    }
    bool select_object(const std::string &path);
    void draw(const char *id = "Material");
};
} // namespace cyuediff
