#include <cyuediff/skeleton_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
const Json *SkeletonView::body(const Json &model) const {
    if (model.is_null() || object_.empty())
        return nullptr;
    auto it = model.at("skeletons").find(object_);
    return it == model.at("skeletons").end() ? nullptr : &*it;
}
void SkeletonView::bind() {
    nodes_.clear();
    links_.clear();
    changes_.clear();
    state_ = {};
    fit_ = true;
    truncated_ = false;
    const auto *a = body(before_), *b = body(after_);
    complete_ = (!a || a->at("complete").get<bool>()) && (!b || b->at("complete").get<bool>()) &&
                (comparing_ ? a && b : a || b);
    metadata_.set_values(a, b, comparing_);
    const auto *bones_a = a ? &a->at("bones") : nullptr, *bones_b = b ? &b->at("bones") : nullptr;
    auto value = [](const Json *model, const std::string &key) -> const Json * {
        if (!model)
            return nullptr;
        auto it = model->find(key);
        return it == model->end() ? nullptr : &*it;
    };
    std::set<std::string> keys;
    for (const auto *model : {bones_a, bones_b})
        if (model)
            for (auto it = model->begin(); it != model->end(); ++it)
                keys.insert(it.key());
    // Bound canvas work independently of the full Core snapshot and generic property view.
    constexpr std::size_t max_nodes = 4096;
    std::map<std::string, cyui::ItemId> ids;
    std::vector<GraphInspectionItem> inspection;
    cyui::ItemId next = 1;
    std::map<unsigned, unsigned> rows;
    low_ = high_ = {};
    for (const auto &key : keys) {
        if (nodes_.size() >= max_nodes) {
            truncated_ = true;
            break;
        }
        const auto *x = value(bones_a, key), *y = value(bones_b, key);
        unsigned depth = 0;
        std::string ancestor = key;
        std::set<std::string> visited;
        while (depth < 32 && visited.insert(ancestor).second) {
            const auto *parent = value(bones_b, ancestor);
            if (!parent)
                parent = value(bones_a, ancestor);
            if (!parent || parent->at("parent").is_null())
                break;
            ancestor = parent->at("parent").get<std::string>();
            ++depth;
        }
        cyui::Node node;
        node.id = next;
        next += 3;
        node.label = key;
        node.position = {double(depth) * 250., double(rows[depth]++) * 115.};
        node.inputs.push_back({node.id + 1, cyui::tr("Parent"), 6});
        node.outputs.push_back({node.id + 2, cyui::tr("Children"), 6});
        ids[key] = node.id;
        const auto change = comparing_ ? classify_change(x, y) : ChangeKind::unchanged;
        changes_[node.id] = change;
        inspection.push_back({node.id, key, key, change});
        high_.x = std::max(high_.x, node.position.x + 210.);
        high_.y = std::max(high_.y, node.position.y + 90.);
        nodes_.push_back(std::move(node));
    }
    rendered_locale_ = cyui::locale();
    std::set<std::pair<std::string, std::string>> relations;
    for (const auto *model : {bones_a, bones_b})
        if (model)
            for (auto it = model->begin(); it != model->end(); ++it)
                if (!it->at("parent").is_null() && ids.count(it.key()) &&
                    ids.count(it->at("parent").get<std::string>()))
                    relations.insert({it->at("parent").get<std::string>(), it.key()});
    for (const auto &relation : relations) {
        auto parent = ids.find(relation.first), child = ids.find(relation.second);
        if (parent != ids.end() && child != ids.end())
            links_.push_back({next++, parent->second + 2, child->second + 1, 6});
    }
    Json visible_a = Json::object(), visible_b = Json::object();
    for (const auto &id : ids) {
        if (const auto *entry = value(bones_a, id.first))
            visible_a[id.first] = *entry;
        if (const auto *entry = value(bones_b, id.first))
            visible_b[id.first] = *entry;
    }
    inspector_.set_nodes(bones_a ? &visible_a : nullptr, bones_b ? &visible_b : nullptr,
                         std::move(inspection), comparing_);
}
void SkeletonView::set_packages(const Package *before, const Package *after, bool comparing) {
    SkeletonOptions options;
    options.source_indices = false;
    before_ = before ? skeleton_snapshot(*before, options) : Json(nullptr);
    after_ = after ? skeleton_snapshot(*after, options) : Json(nullptr);
    comparing_ = comparing || (before && after);
    std::set<std::string> paths;
    for (const auto *model : {&before_, &after_})
        if (!model->is_null())
            for (auto it = model->at("skeletons").begin(); it != model->at("skeletons").end(); ++it)
                paths.insert(it.key());
    objects_.assign(paths.begin(), paths.end());
    object_ = objects_.empty() ? "" : objects_.front();
    bind();
}
bool SkeletonView::select_object(const std::string &path) {
    if (!path.empty() && std::find(objects_.begin(), objects_.end(), path) == objects_.end())
        return false;
    object_ = path;
    bind();
    return true;
}
bool SkeletonView::select_bone(const std::string &name) { return inspector_.select_node(name); }
bool SkeletonView::next_changed_bone(bool previous) { return comparing_ && inspector_.next_change(previous); }
void SkeletonView::set_filter(std::string query) { inspector_.set_filter(std::move(query)); }
void SkeletonView::set_changed_only(bool enabled) { inspector_.set_changed_only(enabled); }
Json SkeletonView::view_state() const {
    if (empty())
        return Json::object();
    Json out = {{"object", object_.empty() ? Json(nullptr) : Json(object_)},
                {"inspection", inspector_.view_state()}};
    if (!fit_) {
        const auto &camera = state_.canvas.view;
        out["canvas"] = {{"pan", {camera.origin.x, camera.origin.y}}, {"zoom", camera.zoom}};
    }
    const auto metadata = metadata_.view_state();
    if (!metadata.empty())
        out["metadata"] = metadata;
    return out;
}
void SkeletonView::restore_view_state(const Json &state) {
    view_state::validate(state, "skeleton");
    if (state.contains("object")) {
        const auto path = state["object"].is_null() ? "" : state["object"].get<std::string>();
        if (!select_object(path))
            select_object("");
    }
    if (state.contains("inspection"))
        inspector_.restore_view_state(state["inspection"]);
    if (state.contains("metadata"))
        metadata_.restore_view_state(state["metadata"]);
    const bool explicit_camera = state.contains("canvas") && !state["canvas"].empty();
    if (!explicit_camera && state.contains("inspection") && state["inspection"].contains("selected") &&
        state["inspection"]["selected"].is_string())
        inspector_.select_node(state["inspection"]["selected"].get<std::string>());
    if (explicit_camera && !nodes_.empty()) {
        const auto &canvas = state["canvas"];
        if (canvas.contains("pan"))
            state_.canvas.view.origin = {canvas["pan"][0].get<double>(), canvas["pan"][1].get<double>()};
        if (canvas.contains("zoom"))
            state_.canvas.view.zoom = canvas["zoom"].get<double>();
        fit_ = false;
    }
}
void SkeletonView::draw(const char *id) {
    cyui::ScopedID scope(id);
    if (rendered_locale_ != cyui::locale()) {
        rendered_locale_ = cyui::locale();
        for (auto &node : nodes_) {
            node.inputs[0].label = cyui::tr("Parent");
            node.outputs[0].label = cyui::tr("Children");
        }
    }
    if (empty()) {
        cyui::emptyState(cyui::tr("No stored reference skeleton").c_str(),
                         cyui::tr("This package has no supported Skeleton object.").c_str(),
                         cyui::icon::document);
        return;
    }
    ImGui::TextWrapped(
        "%s", cyui::tr("Raw reference hierarchy and local pose; animations and skinning are not evaluated.")
                  .c_str());
    if (ImGui::BeginCombo(cyui::trLabel("Skeleton object").c_str(), object_.c_str())) {
        for (const auto &path : objects_)
            if (ImGui::Selectable(path.c_str(), path == object_))
                select_object(path);
        ImGui::EndCombo();
    }
    if (!complete_)
        cyui::callout(cyui::Tone::warning,
                      cyui::tr("Skeleton data is incomplete. Unknown native data remains opaque.").c_str());
    if (truncated_)
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr(
                "Hierarchy diagram is limited to 4,096 bones. Complete stored data remains in Properties.")
                .c_str());
    if (nodes_.empty()) {
        metadata_.draw("Skeleton metadata");
        return;
    }
    if (ImGui::CollapsingHeader(cyui::trLabel("Skeleton properties").c_str())) {
        ImGui::BeginChild("Skeleton metadata panel", {0, cyui::dp(200)}, ImGuiChildFlags_Borders);
        metadata_.draw("Skeleton metadata");
        ImGui::EndChild();
    }
    if (comparing_)
        cyui::textDim("%s", cyui::tr("Connections include both stored parent relationships.").c_str());
    inspector_.draw(state_, nodes_, links_, changes_, fit_, low_, high_);
}
} // namespace cyuediff
