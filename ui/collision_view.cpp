#include <cyuediff/collision_view.hpp>
#include "diff_style.hpp"
#include "../src/source_availability.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
const Json *CollisionView::selected(const Json &model, bool shape) const {
    if (model.is_null())
        return nullptr;
    if (body_.empty())
        return &model;
    auto body = model.at("bodies").find(body_);
    if (body == model.at("bodies").end())
        return nullptr;
    if (!shape || shape_.empty())
        return &*body;
    auto entry = body->at("shapes").find(shape_);
    return entry == body->at("shapes").end() ? nullptr : &*entry;
}
void CollisionView::bind(bool retain) {
    auto saved = properties_.view_state();
    std::set<std::string> keys;
    for (const auto *model : {&before_, &after_}) {
        const auto *body = selected(*model, false);
        if (!body_.empty() && body)
            for (auto it = body->at("shapes").begin(); it != body->at("shapes").end(); ++it)
                keys.insert(it.key());
    }
    shapes_.assign(keys.begin(), keys.end());
    viewport_.select(body_, shape_);
    ValueCoverage coverage;
    auto escape = [](const std::string &s) {
        std::string out;
        for (char c : s)
            out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
        return out;
    };
    const auto prefix =
        body_.empty() ? std::string{}
                      : "/bodies/" + escape(body_) + (shape_.empty() ? "" : "/shapes/" + escape(shape_));
    for (const auto &path : source_coverage_.comparison_unavailable) {
        if (path == prefix || (prefix.size() > path.size() && prefix.compare(0, path.size(), path) == 0 &&
                               prefix[path.size()] == '/'))
            coverage.comparison_unavailable.push_back("");
        else if (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 &&
                 path[prefix.size()] == '/')
            coverage.comparison_unavailable.push_back(path.substr(prefix.size()));
    }
    properties_.set_values(selected(before_), selected(after_), comparing_, coverage);
    if (retain)
        properties_.restore_view_state(saved);
}
void CollisionView::set_packages(const Package *before, const Package *after, bool comparing) {
    CollisionOptions options;
    options.source_indices = false;
    before_ = before ? collision_snapshot(*before, options) : Json(nullptr);
    after_ = after ? collision_snapshot(*after, options) : Json(nullptr);
    comparing_ = comparing || (before && after);
    source_coverage_ = {};
    if (before && after) {
        const auto raw = detail::source_coverage(*before, *after);
        const auto coverage =
            detail::projection_coverage(raw, *before, *after, before_, after_, "collisions");
        source_coverage_.comparison_unavailable.assign(coverage.suppressed.begin(),
                                                       coverage.suppressed.end());
    }
    preview_errors_.clear();
    auto preview = [&](const Package *package, const char *side) {
        try {
            return package ? collision_wireframe(*package) : CollisionWireframe{};
        } catch (const Error &error) {
            if (error.code == "cancelled")
                throw;
            preview_errors_.push_back(std::string(side) + ": " + error.what());
            return CollisionWireframe{};
        }
    };
    auto before_preview = preview(before, "Before"), after_preview = preview(after, "After");
    bind_models(std::move(before_preview), std::move(after_preview));
}
void CollisionView::set_models(Json before, Json after, CollisionWireframe before_preview,
                               CollisionWireframe after_preview, bool comparing) {
    source_coverage_ = {};
    before_ = std::move(before);
    after_ = std::move(after);
    comparing_ = comparing || (!before_.is_null() && !after_.is_null());
    preview_errors_.clear();
    bind_models(std::move(before_preview), std::move(after_preview));
}
void CollisionView::bind_models(CollisionWireframe before_preview, CollisionWireframe after_preview) {
    const bool before = !before_.is_null(), after = !after_.is_null();
    complete_ = (!before || before_.at("complete").get<bool>()) &&
                (!after || after_.at("complete").get<bool>()) &&
                (comparing_ ? before && after : before || after);
    viewport_.set_models(std::move(before_preview), std::move(after_preview), &before_, &after_, comparing_,
                         source_coverage_);
    if (!source_coverage_.comparison_unavailable.empty())
        complete_ = false;
    std::set<std::string> keys;
    for (const auto *model : {&before_, &after_})
        if (!model->is_null())
            for (auto it = model->at("bodies").begin(); it != model->at("bodies").end(); ++it)
                keys.insert(it.key());
    bodies_.assign(keys.begin(), keys.end());
    body_ = bodies_.empty() ? std::string{} : bodies_.front();
    shape_.clear();
    bind(false);
}
Json CollisionView::report() const {
    auto changes = viewport_.changes();
    return {{"bodies", bodies_.size()},
            {"body", body_},
            {"shape", shape_},
            {"complete", complete_},
            {"beforeEdges", viewport_.visible_edge_count()},
            {"afterEdges", viewport_.visible_edge_count(true)},
            {"changes",
             {{"unchanged", changes.unchanged},
              {"added", changes.added},
              {"removed", changes.removed},
              {"modified", changes.modified},
              {"unknown", changes.unknown}}}};
}
bool CollisionView::empty() const {
    if (!bodies_.empty())
        return false;
    for (const auto *model : {&before_, &after_})
        if (!model->is_null() && !model->at("owners").empty())
            return false;
    return true;
}
bool CollisionView::select_body(const std::string &path) {
    if (!path.empty() && std::find(bodies_.begin(), bodies_.end(), path) == bodies_.end())
        return false;
    body_ = path;
    shape_.clear();
    bind();
    return true;
}
bool CollisionView::select_shape(const std::string &key) {
    if (!key.empty() && std::find(shapes_.begin(), shapes_.end(), key) == shapes_.end())
        return false;
    shape_ = key;
    bind();
    return true;
}
bool CollisionView::next_changed_shape(bool previous) {
    if (!comparing_ || shapes_.empty())
        return false;
    std::vector<std::string> changed;
    for (const auto &key : shapes_)
        if (viewport_.stored_change(body_, key) != ChangeKind::unchanged)
            changed.push_back(key);
    if (changed.empty())
        return false;
    auto found = std::find(changed.begin(), changed.end(), shape_);
    std::size_t index = previous ? changed.size() - 1 : 0;
    if (found != changed.end()) {
        const auto current = std::size_t(found - changed.begin());
        index = previous ? (current + changed.size() - 1) % changed.size() : (current + 1) % changed.size();
    }
    return select_shape(changed[index]);
}
Json CollisionView::view_state() const {
    if (empty())
        return Json::object();
    Json result = {{"body", body_.empty() ? Json(nullptr) : Json(body_)},
                   {"shape", shape_.empty() ? Json(nullptr) : Json(shape_)},
                   {"properties", properties_.view_state()}};
    const auto camera = viewport_.view_state();
    if (!camera.empty())
        result["viewport"] = camera;
    return result;
}
void CollisionView::restore_view_state(const Json &state) {
    cyuediff::view_state::validate(state, "collision");
    if (state.contains("body")) {
        const auto path = state["body"].is_null() ? std::string{} : state["body"].get<std::string>();
        if (!select_body(path))
            select_body("");
    }
    if (state.contains("shape")) {
        const auto key = state["shape"].is_null() ? std::string{} : state["shape"].get<std::string>();
        if (!select_shape(key))
            select_shape("");
    }
    if (state.contains("viewport"))
        viewport_.restore_view_state(state["viewport"]);
    if (state.contains("properties"))
        properties_.restore_view_state(state["properties"]);
}
void CollisionView::draw(const char *id) {
    if (!source_coverage_.comparison_unavailable.empty())
        cyui::callout(cyui::Tone::info,
                      cyui::tr("Comparison is unavailable for this view because source values are unread or "
                               "use incompatible decoders. Inspect each revision or use scoped properties.")
                          .c_str());
    cyui::ScopedID scope(id);
    if (empty()) {
        cyui::emptyState(cyui::tr("No stored collision bodies").c_str(),
                         cyui::tr("This package has no supported BodySetup projection.").c_str(),
                         cyui::icon::document);
        return;
    }
    const bool cooked = (!before_.is_null() && before_.contains("provenance")) ||
                        (!after_.is_null() && after_.contains("provenance"));
    const auto native = [](const Json &m) {
        return !m.is_null() && m.contains("provenance") &&
               m["provenance"].value("nativePhysicsDecoded", false);
    };
    cyui::textDim("%s", cyui::tr(native(before_) || native(after_)
                                     ? "Decoded native Chaos geometry in cooker coordinates; no simulation."
                                 : cooked ? "Stored collision shapes; native physics data is not decoded."
                                          : "Stored collision properties; physics data is not evaluated.")
                            .c_str());
    if (!preview_errors_.empty()) {
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("Collision preview unavailable; stored properties remain inspectable.").c_str());
        for (const auto &error : preview_errors_)
            cyui::textDim("%s", error.c_str());
    }
    if (!complete_)
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("Collision data is incomplete. Missing fields and external bodies remain explicit.")
                .c_str());
    if (ImGui::BeginCombo(cyui::trLabel("Collision body").c_str(),
                          body_.empty() ? cyui::tr("All bodies and references").c_str() : body_.c_str())) {
        if (ImGui::Selectable(cyui::trLabel("All bodies and references").c_str(), body_.empty()))
            select_body("");
        for (const auto &path : bodies_)
            if (ImGui::Selectable(path.c_str(), body_ == path))
                select_body(path);
        ImGui::EndCombo();
    }
    if (!body_.empty() &&
        ImGui::BeginCombo(cyui::trLabel("Collision shape").c_str(),
                          shape_.empty() ? cyui::tr("All stored shapes").c_str() : shape_.c_str())) {
        if (ImGui::Selectable(cyui::trLabel("All stored shapes").c_str(), shape_.empty()))
            select_shape("");
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shapes_.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                if (ImGui::Selectable(shapes_[i].c_str(), shape_ == shapes_[i]))
                    select_shape(shapes_[i]);
        ImGui::EndCombo();
    }
    if (comparing_ && !body_.empty()) {
        if (cyui::button(cyui::trLabel("Previous changed shape").c_str()))
            next_changed_shape(true);
        ImGui::SameLine();
        if (cyui::button(cyui::trLabel("Next changed shape").c_str()))
            next_changed_shape();
    }
    const float available = ImGui::GetContentRegionAvail().y;
    if (available > cyui::dp(160)) {
        ImGui::BeginChild("Collision geometry panel",
                          {0, std::clamp(available * .48f, cyui::dp(140), cyui::dp(450))},
                          ImGuiChildFlags_Borders);
        viewport_.draw();
        ImGui::EndChild();
        auto picked = viewport_.take_picked_shape();
        if (!picked.empty())
            select_shape(picked);
    }
    properties_.draw("Collision properties");
}
} // namespace cyuediff
