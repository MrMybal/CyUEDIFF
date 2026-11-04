#include <cyuediff/graph_inspector.hpp>
#include "diff_style.hpp"
#include <algorithm>
namespace cyuediff {
void GraphInspector::register_translations() {
    cyui::registerTranslations("fr", {{"Find nodes", "Rechercher des n\u0153uds"},
                                      {"Node", "N\u0153ud"},
                                      {"Select a node", "S\u00e9lectionner un n\u0153ud"},
                                      {"No matching node", "Aucun n\u0153ud correspondant"},
                                      {"Previous changed node", "N\u0153ud modifi\u00e9 pr\u00e9c\u00e9dent"},
                                      {"Next changed node", "N\u0153ud modifi\u00e9 suivant"},
                                      {"Focus selected node", "Centrer le n\u0153ud s\u00e9lectionn\u00e9"},
                                      {"Clear node selection", "Effacer la s\u00e9lection du n\u0153ud"},
                                      {"Node details", "D\u00e9tails du n\u0153ud"},
                                      {"%zu matching nodes", "%zu n\u0153uds correspondants"},
                                      {"Select a node to compare its stored properties and pins.",
                                       "S\u00e9lectionnez un n\u0153ud pour comparer ses "
                                       "propri\u00e9t\u00e9s et broches enregistr\u00e9es."}});
}
void GraphInspector::set_nodes(const Json *before, const Json *after, std::vector<GraphInspectionItem> items,
                               bool comparing) {
    before_ = before ? *before : Json::object();
    after_ = after ? *after : Json::object();
    items_ = std::move(items);
    comparing_ = comparing;
    filter_ = {};
    changed_only_ = false;
    selected_.clear();
    focus_pending_ = false;
    rebuild();
    bind_selection();
}
void GraphInspector::bind_selection() {
    const auto a = before_.find(selected_), b = after_.find(selected_);
    properties_.set_values(a == before_.end() ? nullptr : &*a, b == after_.end() ? nullptr : &*b, comparing_);
}
void GraphInspector::rebuild() {
    visible_.clear();
    bool selected_visible = false;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto &item = items_[i];
        if ((!changed_only_ || item.change != ChangeKind::unchanged) &&
            filter_.matches(item.label, item.key)) {
            visible_.push_back(i);
            selected_visible = selected_visible || item.key == selected_;
        }
    }
    if (!selected_.empty() && !selected_visible) {
        selected_.clear();
        focus_pending_ = false;
        bind_selection();
    }
}
void GraphInspector::set_filter(std::string query) {
    filter_.query = std::move(query);
    rebuild();
}
void GraphInspector::set_changed_only(bool enabled) {
    changed_only_ = comparing_ && enabled;
    rebuild();
}
bool GraphInspector::select_node(const std::string &key) {
    if (key.empty()) {
        selected_.clear();
        focus_pending_ = false;
        bind_selection();
        return true;
    }
    for (auto i : visible_)
        if (items_[i].key == key) {
            if (selected_ != key) {
                selected_ = key;
                bind_selection();
            }
            focus_pending_ = true;
            return true;
        }
    return false;
}
cyui::ItemId GraphInspector::selected_canvas_id() const {
    for (const auto &item : items_)
        if (item.key == selected_)
            return item.id;
    return 0;
}
bool GraphInspector::next_change(bool previous) {
    std::vector<std::size_t> changed;
    for (auto i : visible_)
        if (items_[i].change != ChangeKind::unchanged)
            changed.push_back(i);
    if (changed.empty())
        return false;
    std::size_t index = previous ? changed.size() - 1 : 0;
    for (std::size_t i = 0; i < changed.size(); ++i)
        if (items_[changed[i]].key == selected_) {
            index = previous ? (i + changed.size() - 1) % changed.size() : (i + 1) % changed.size();
            break;
        }
    return select_node(items_[changed[index]].key);
}
Json GraphInspector::view_state() const {
    Json result = Json::object();
    if (!filter_.query.empty())
        result["filter"] = filter_.query;
    if (changed_only_)
        result["changedOnly"] = true;
    if (!selected_.empty()) {
        result["selected"] = selected_;
        result["properties"] = properties_.view_state();
    }
    return result;
}
void GraphInspector::restore_view_state(const Json &state) {
    view_state::validate(state, "graph-inspection");
    if (state.contains("filter"))
        filter_.query = state["filter"].get<std::string>();
    if (state.contains("changedOnly"))
        changed_only_ = comparing_ && state["changedOnly"].get<bool>();
    rebuild();
    if (state.contains("selected")) {
        select_node("");
        if (state["selected"].is_string())
            select_node(state["selected"].get<std::string>());
    }
    if (!selected_.empty() && state.contains("properties"))
        properties_.restore_view_state(state["properties"]);
    // Restore selection without overwriting an independently restored graph camera.
    focus_pending_ = false;
}
void GraphInspector::draw(cyui::NodeState &state, const std::vector<cyui::Node> &nodes,
                          const std::vector<cyui::NodeLink> &links,
                          const std::map<cyui::ItemId, ChangeKind> &changes, bool &needs_fit, cyui::Point low,
                          cyui::Point high) {
    cyui::FilterOptions filter_options;
    filter_options.hint = "Find nodes";
    filter_options.width = cyui::dp(220);
    bool update = cyui::filterBox("##node-filter", filter_, filter_options);
    cyui::tooltip(cyui::tr("Find nodes").c_str());
    if (comparing_) {
        ImGui::SameLine();
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
    }
    if (update)
        rebuild();
    ImGui::SameLine();
    cyui::textDim(cyui::tr("%zu matching nodes").c_str(), visible_.size());
    const auto selected_id = selected_canvas_id();
    const auto current =
        std::find_if(items_.begin(), items_.end(), [&](const auto &i) { return i.id == selected_id; });
    ImGui::SetNextItemWidth(cyui::dp(250));
    if (ImGui::BeginCombo(cyui::trLabel("Node").c_str(), current == items_.end()
                                                             ? cyui::tr("Select a node").c_str()
                                                             : current->label.c_str())) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_.size()));
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto &item = items_[visible_[static_cast<std::size_t>(row)]];
                cyui::ItemScope scope(item.id);
                if (ImGui::Selectable(item.label.c_str(), item.key == selected_))
                    select_node(item.key);
                cyui::tooltip(item.key.c_str());
            }
        if (visible_.empty())
            cyui::textDim("%s", cyui::tr("No matching node").c_str());
        ImGui::EndCombo();
    }
    if (comparing_) {
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed node").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed node").c_str()))
            next_change();
    }
    if (!selected_.empty()) {
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::zoomIn, cyui::tr("Focus selected node").c_str()))
            focus_pending_ = true;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::close, cyui::tr("Clear node selection").c_str()))
            select_node("");
    }
    auto available = ImGui::GetContentRegionAvail();
    if (available.x <= 0 || available.y <= 0)
        return;
    const bool details = !selected_.empty();
    const bool horizontal = available.x >= cyui::dp(820);
    const auto gap = ImGui::GetStyle().ItemSpacing;
    ImVec2 canvas_size = available;
    if (details) {
        if (horizontal)
            canvas_size.x = available.x * .55f;
        else
            canvas_size.y = available.y * .5f;
    }
    if (needs_fit && !nodes.empty()) {
        state.canvas.view.fit(low, high, canvas_size, cyui::dp(24));
        needs_fit = false;
    }
    auto selected = selected_canvas_id();
    state.selection = selected ? std::vector<cyui::ItemId>{selected} : std::vector<cyui::ItemId>{};
    if (focus_pending_) {
        for (const auto &node : nodes)
            if (node.id == selected) {
                const double height = 40. + 24. * std::max(node.inputs.size(), node.outputs.size());
                auto &v = state.canvas.view;
                v.zoom = std::clamp(v.zoom, .4, 1.5);
                v.origin = {node.position.x + 105. - canvas_size.x / (2 * v.zoom),
                            node.position.y + height / 2 - canvas_size.y / (2 * v.zoom)};
                needs_fit = false;
            }
        focus_pending_ = false;
    }
    const auto position = ImGui::GetCursorScreenPos();
    cyui::NodeOptions options;
    options.readOnly = true;
    options.revision = 1;
    options.drawBody = [&](const cyui::Node &node, const cyui::Rect &rect) {
        detail::node_marker(node, rect, changes);
    };
    cyui::nodeEditor("##inspection-canvas", state, nodes, links, canvas_size, options);
    const auto clicked = state.selection.empty() ? 0 : state.selection.back();
    if (clicked != selected) {
        if (!clicked)
            select_node("");
        else
            for (const auto &item : items_)
                if (item.id == clicked)
                    select_node(item.key);
        focus_pending_ = false;
    }
    if (details) {
        if (horizontal) {
            ImGui::SetCursorScreenPos({position.x + canvas_size.x + gap.x, position.y});
            ImGui::BeginChild("##node-details",
                              {std::max(1.f, available.x - canvas_size.x - gap.x), available.y});
        } else {
            ImGui::BeginChild("##node-details",
                              {available.x, std::max(1.f, available.y - canvas_size.y - gap.y)});
        }
        cyui::sectionTitle(cyui::tr("Node details").c_str());
        properties_.draw("Node properties");
        ImGui::EndChild();
    } else
        cyui::tooltip(cyui::tr("Select a node to compare its stored properties and pins.").c_str());
}
} // namespace cyuediff
