#pragma once
#include <cyuediff/property_view.hpp>
#include <cyui/modules/Nodes.h>
namespace cyuediff {
struct GraphInspectionItem {
    cyui::ItemId id = 0;
    std::string key, label;
    ChangeKind change = ChangeKind::unchanged;
};
// Read-only node inspection shared by Blueprint and material graph hosts.
// Keys are snapshot identities; numeric canvas IDs never enter saved state.
class GraphInspector {
    Json before_ = Json::object(), after_ = Json::object();
    std::vector<GraphInspectionItem> items_;
    std::vector<std::size_t> visible_;
    cyui::TextFilter filter_;
    PropertyView properties_;
    ImVec2 last_canvas_size_{};
    std::string selected_;
    bool comparing_ = false, changed_only_ = false, focus_pending_ = false;
    void rebuild();
    void bind_selection();

  public:
    static void register_translations();
    void set_nodes(const Json *before, const Json *after, std::vector<GraphInspectionItem>, bool comparing);
    void set_filter(std::string);
    void set_changed_only(bool);
    bool select_node(const std::string &key);
    // An explicit canvas click reveals nodes hidden by the navigation-list filter.
    bool select_canvas_node(cyui::ItemId);
    bool next_change(bool previous = false);
    const std::string &selected_node() const {
        return selected_;
    }
    cyui::ItemId selected_canvas_id() const;
    std::size_t visible_count() const {
        return visible_.size();
    }
    const PropertyTable &details() const {
        return properties_.table();
    }
    Json view_state() const;
    void restore_view_state(const Json &);
    // Draws search/navigation, the canvas and a responsive selected-node inspector.
    void draw(cyui::NodeState &, const std::vector<cyui::Node> &, const std::vector<cyui::NodeLink> &,
              const std::map<cyui::ItemId, ChangeKind> &, bool &needs_fit, cyui::Point low, cyui::Point high);
};
} // namespace cyuediff
