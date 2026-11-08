#include <cyuediff/render_morph_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
namespace {
const Json *target(const Json &j, const std::string &name) {
    return j.is_object() && j.contains("targets") && j["targets"].contains(name)
               ? &j["targets"][name]["vertices"]
               : nullptr;
}
const Json *entry(const Json *t, std::uint32_t vertex) {
    const auto key = std::to_string(vertex);
    return t && t->contains(key) ? &(*t)[key] : nullptr;
}
const Json zero = Json::array({{{"position", {0, 0, 0}}, {"normal", {0, 0, 0}}}});
} // namespace
void RenderMorphView::register_translations() {
    cyui::registerTranslations(
        "fr", {{"Render morph targets", "Morph targets de rendu"},
               {"Stored render deltas; missing entries are zero. No blending or normal recomputation.",
                "Deltas de rendu enregistrés ; les entrées absentes valent zéro. Sans mélange ni recalcul "
                "des normales."},
               {"Morph identities unavailable; differences are unknown.",
                "Identités des morphs indisponibles ; différences inconnues."},
               {"Morph target", "Morph target"},
               {"Find render vertices or values", "Rechercher un sommet de rendu ou une valeur"},
               {"Previous changed morph vertex", "Sommet de morph modifié précédent"},
               {"Next changed morph vertex", "Sommet de morph modifié suivant"},
               {"No stored delta for this target", "Aucun delta enregistré pour cette cible"},
               {"No matching morph vertex", "Aucun sommet de morph correspondant"},
               {"Render vertex", "Sommet de rendu"}});
}
void RenderMorphView::set_models(std::shared_ptr<const MeshRenderMorphs> before,
                                 std::shared_ptr<const MeshRenderMorphs> after, bool comparing) {
    ready_ = (!before || before->ready()) && (!after || after->ready());
    comparing_ = ready_ && (comparing || bool(before && after));
    before_ = before && before->ready() ? before->semantic_snapshot() : Json::object();
    after_ = after && after->ready() ? after->semantic_snapshot() : Json::object();
    std::set<std::string> names;
    for (const auto *model : {before.get(), after.get()})
        if (model && model->ready())
            for (const auto &name : model->names())
                names.insert(name);
    targets_.assign(names.begin(), names.end());
    target_ = targets_.empty() ? std::string{} : targets_[0];
    vertex_.reset();
    filter_ = {};
    changed_only_ = false;
    rendered_rows_ = 0;
    selected_visible_ = false;
    rebuild();
}
void RenderMorphView::rebuild() {
    rows_.clear();
    visible_.clear();
    const auto *a = target(before_, target_), *b = target(after_, target_);
    std::set<std::uint32_t> vertices;
    for (const auto *t : {a, b})
        if (t)
            for (auto i = t->begin(); i != t->end(); ++i)
                vertices.insert(static_cast<std::uint32_t>(std::stoul(i.key())));
    for (const auto vertex : vertices) {
        const auto *av = entry(a, vertex), *bv = entry(b, vertex);
        const auto change =
            !comparing_ ? ChangeKind::unchanged
            : !a        ? ChangeKind::added
            : !b        ? ChangeKind::removed
                 : ((av ? *av : zero) == (bv ? *bv : zero) ? ChangeKind::unchanged : ChangeKind::modified);
        rows_.push_back(
            {vertex, change,
             std::to_string(vertex) + " " + (av ? av->dump() : "") + " " + (bv ? bv->dump() : "")});
        if ((!changed_only_ || change != ChangeKind::unchanged) && filter_.matches(rows_.back().search))
            visible_.push_back(rows_.size() - 1);
    }
    if (vertex_ &&
        std::none_of(visible_.begin(), visible_.end(), [&](auto i) { return rows_[i].vertex == *vertex_; }))
        vertex_.reset();
    bind_selection();
}
void RenderMorphView::bind_selection() {
    const auto *a = target(before_, target_), *b = target(after_, target_);
    const auto *av = vertex_ ? entry(a, *vertex_) : nullptr, *bv = vertex_ ? entry(b, *vertex_) : nullptr;
    details_.set_values(vertex_ && a ? (av ? av : &zero) : nullptr,
                        vertex_ && b ? (bv ? bv : &zero) : nullptr, comparing_);
}
bool RenderMorphView::select_target(const std::string &name) {
    if (std::find(targets_.begin(), targets_.end(), name) == targets_.end())
        return false;
    target_ = name;
    vertex_.reset();
    rebuild();
    return true;
}
bool RenderMorphView::select_vertex(std::uint32_t vertex) {
    if (std::none_of(visible_.begin(), visible_.end(), [&](auto i) { return rows_[i].vertex == vertex; }))
        return false;
    vertex_ = vertex;
    focus_pending_ = true;
    bind_selection();
    return true;
}
void RenderMorphView::set_filter(std::string value) {
    filter_.query = std::move(value);
    rebuild();
}
void RenderMorphView::set_changed_only(bool value) {
    changed_only_ = comparing_ && value;
    rebuild();
}
bool RenderMorphView::next_change(bool previous) {
    if (visible_.empty())
        return false;
    auto it = std::find_if(visible_.begin(), visible_.end(),
                           [&](auto i) { return vertex_ && rows_[i].vertex == *vertex_; });
    const auto start =
        it == visible_.end() ? (previous ? 0 : visible_.size() - 1) : std::size_t(it - visible_.begin());
    for (std::size_t k = 1; k <= visible_.size(); ++k) {
        const auto at =
            previous ? (start + visible_.size() - k) % visible_.size() : (start + k) % visible_.size();
        if (rows_[visible_[at]].change != ChangeKind::unchanged)
            return select_vertex(rows_[visible_[at]].vertex);
    }
    return false;
}
Json RenderMorphView::report() const {
    std::uint64_t modified = 0, added = 0, removed = 0;
    for (const auto &r : rows_) {
        modified += r.change == ChangeKind::modified;
        added += r.change == ChangeKind::added;
        removed += r.change == ChangeKind::removed;
    }
    return {{"ready", ready_},
            {"comparing", comparing_},
            {"targets", targets_},
            {"target", target_},
            {"vertices", rows_.size()},
            {"visible", visible_.size()},
            {"rendered", rendered_rows_},
            {"modified", modified},
            {"added", added},
            {"removed", removed},
            {"selected", vertex_ ? Json(*vertex_) : Json(nullptr)},
            {"selectedVisible", selected_visible_},
            {"detailRows", details_.table().rows.size()}};
}
Json RenderMorphView::view_state() const {
    return {{"target", target_},
            {"vertex", vertex_ ? Json(*vertex_) : Json(nullptr)},
            {"filter", filter_.query},
            {"changedOnly", changed_only_},
            {"properties", details_.view_state()}};
}
void RenderMorphView::restore_view_state(const Json &state) {
    view_state::validate(state, "render-morph");
    if (state.contains("target"))
        select_target(state["target"].get<std::string>());
    if (state.contains("filter"))
        filter_.query = state["filter"].get<std::string>();
    if (state.contains("changedOnly"))
        changed_only_ = comparing_ && state["changedOnly"].get<bool>();
    rebuild();
    if (state.contains("vertex")) {
        vertex_.reset();
        if (!state["vertex"].is_null())
            select_vertex(state["vertex"].get<std::uint32_t>());
        else
            bind_selection();
    }
    if (vertex_ && state.contains("properties"))
        details_.restore_view_state(state["properties"]);
}
void RenderMorphView::draw(const char *id) {
    cyui::ScopedID scope(id);
    rendered_rows_ = 0;
    selected_visible_ = false;
    ImGui::TextWrapped(
        "%s", cyui::tr("Stored render deltas; missing entries are zero. No blending or normal recomputation.")
                  .c_str());
    if (!ready_)
        cyui::callout(cyui::Tone::warning,
                      cyui::tr("Morph identities unavailable; differences are unknown.").c_str());
    if (ImGui::BeginCombo(cyui::trLabel("Morph target").c_str(), target_.c_str())) {
        for (const auto &name : targets_)
            if (ImGui::Selectable(name.c_str(), name == target_))
                select_target(name);
        ImGui::EndCombo();
    }
    cyui::FilterOptions options;
    options.hint = "Find render vertices or values";
    options.width = std::min(cyui::dp(270), ImGui::GetContentRegionAvail().x);
    bool update = cyui::filterBox("##morph-filter", filter_, options);
    if (comparing_) {
        if (ImGui::GetContentRegionAvail().x > cyui::dp(240))
            ImGui::SameLine();
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed morph vertex").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed morph vertex").c_str()))
            next_change();
    }
    if (update)
        rebuild();
    const float height = vertex_ ? std::max(cyui::dp(100), ImGui::GetContentRegionAvail().y * .4f) : 0;
    ImGui::BeginChild("##morph-vertices", {0, height}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##morph-table", 2,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                          {0, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupColumn(cyui::tr("Render vertex").c_str());
        ImGui::TableSetupColumn(cyui::tr("Change").c_str());
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_.size()));
        if (focus_pending_)
            for (std::size_t i = 0; i < visible_.size(); ++i)
                if (vertex_ && rows_[visible_[i]].vertex == *vertex_) {
                    clipper.IncludeItemByIndex(static_cast<int>(i));
                    break;
                }
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto &r = rows_[visible_[i]];
                const auto label = std::to_string(r.vertex);
                cyui::ScopedID item(label.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(label.c_str(), vertex_ && *vertex_ == r.vertex,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    select_vertex(r.vertex);
                ++rendered_rows_;
                if (vertex_ && *vertex_ == r.vertex) {
                    selected_visible_ = ImGui::IsItemVisible();
                    if (focus_pending_) {
                        ImGui::SetScrollHereY(.4f);
                        focus_pending_ = false;
                    }
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::PushStyleColor(ImGuiCol_Text, detail::change_color(r.change));
                ImGui::TextUnformatted(cyui::tr(change_name(r.change)).c_str());
                ImGui::PopStyleColor();
            }
        ImGui::EndTable();
    }
    if (visible_.empty())
        cyui::textDim(
            cyui::tr(rows_.empty() ? "No stored delta for this target" : "No matching morph vertex").c_str());
    ImGui::EndChild();
    if (vertex_) {
        ImGui::Text("%s | %u", cyui::tr("Render vertex").c_str(), *vertex_);
        details_.draw("##morph-details");
    }
}
} // namespace cyuediff
