#include <cyuediff/vertex_attribute_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
namespace {
const MeshVertexAttribute *channel(const std::shared_ptr<const MeshVertexAttributes> &model,
                                   const std::string &name) {
    if (!model)
        return nullptr;
    const auto found = model->channels.find(name);
    return found == model->channels.end() ? nullptr : found->second.get();
}
Json value(const MeshVertexAttribute *channel, std::uint32_t id) {
    return channel && id < channel->vertex_count() ? Json(channel->vertex(id)) : Json(nullptr);
}
} // namespace
void VertexAttributeView::register_translations() {
    cyui::registerTranslations(
        "fr", {{"Vertex attributes", "Attributs des sommets"},
               {"Stored render vertex values; no shader or deformation evaluation.",
                "Valeurs des sommets de rendu ; sans évaluation de shader ni de déformation."},
               {"Find attribute vertices or values", "Rechercher un sommet ou une valeur d’attribut"},
               {"Previous changed attribute vertex", "Sommet d’attribut modifié précédent"},
               {"Next changed attribute vertex", "Sommet d’attribut modifié suivant"},
               {"No stored channel with this name", "Aucun canal enregistré sous ce nom"},
               {"No matching attribute vertex", "Aucun sommet d’attribut correspondant"},
               {"Storage encoding is excluded from value changes.",
                "Le format de stockage est exclu des différences de valeurs."},
               {"Render vertex", "Sommet de rendu"},
               {"Components", "Composantes"}});
}
void VertexAttributeView::set_attributes(std::shared_ptr<const MeshVertexAttributes> a,
                                         std::shared_ptr<const MeshVertexAttributes> b, bool comparing) {
    before_ = std::move(a);
    after_ = std::move(b);
    comparing_ = comparing && bool(before_ && after_);
    std::set<std::string> names;
    for (const auto *m : {before_.get(), after_.get()})
        if (m)
            for (const auto &entry : m->channels)
                names.insert(entry.first);
    channels_.assign(names.begin(), names.end());
    channel_ = channels_.empty() ? std::string() : channels_[0];
    vertex_.reset();
    filter_ = {};
    changed_only_ = false;
    rendered_ = 0;
    selected_visible_ = false;
    rebuild();
}
void VertexAttributeView::rebuild() {
    rows_.clear();
    const auto *a = channel(before_, channel_), *b = channel(after_, channel_);
    const auto count = std::max(a ? a->vertex_count() : 0, b ? b->vertex_count() : 0);
    if (count > 1'000'000)
        throw Error("vertex-attribute-view-limit", "Attribute widget exceeds vertex row budget");
    rows_.reserve(count);
    for (std::uint32_t id = 0; id < count; ++id) {
        const auto av = value(a, id), bv = value(b, id);
        const auto change = !comparing_    ? ChangeKind::unchanged
                            : av.is_null() ? ChangeKind::added
                            : bv.is_null() ? ChangeKind::removed
                            : av != bv     ? ChangeKind::modified
                                           : ChangeKind::unchanged;
        rows_.push_back({id, change, std::to_string(id) + " " + av.dump() + " " + bv.dump()});
    }
    refilter();
}
void VertexAttributeView::refilter() {
    visible_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if ((!changed_only_ || rows_[i].change != ChangeKind::unchanged) && filter_.matches(rows_[i].search))
            visible_.push_back(i);
    if (vertex_ &&
        std::none_of(visible_.begin(), visible_.end(), [&](auto i) { return rows_[i].vertex == *vertex_; }))
        vertex_.reset();
    bind_selection();
}
void VertexAttributeView::bind_selection() {
    const auto detail = [&](const auto &model) {
        const auto *a = channel(model, channel_);
        if (!a)
            return Json(nullptr);
        Json out = {{"name", channel_}, {"components", a->components()}};
        if (vertex_)
            out["value"] = value(a, *vertex_);
        return out;
    };
    const auto a = detail(before_), b = detail(after_);
    details_.set_values(a.is_null() ? nullptr : &a, b.is_null() ? nullptr : &b, comparing_);
}
bool VertexAttributeView::select_channel(const std::string &name) {
    if (std::find(channels_.begin(), channels_.end(), name) == channels_.end())
        return false;
    channel_ = name;
    vertex_.reset();
    rebuild();
    return true;
}
bool VertexAttributeView::select_vertex(std::uint32_t id) {
    if (std::none_of(visible_.begin(), visible_.end(), [&](auto i) { return rows_[i].vertex == id; }))
        return false;
    vertex_ = id;
    focus_pending_ = true;
    bind_selection();
    return true;
}
void VertexAttributeView::set_filter(std::string filter) {
    filter_.query = std::move(filter);
    refilter();
}
void VertexAttributeView::set_changed_only(bool value) {
    changed_only_ = comparing_ && value;
    refilter();
}
bool VertexAttributeView::next_change(bool previous) {
    if (visible_.empty())
        return false;
    const auto found = std::find_if(visible_.begin(), visible_.end(),
                                    [&](auto i) { return vertex_ && rows_[i].vertex == *vertex_; });
    const auto start = found == visible_.end() ? (previous ? 0 : visible_.size() - 1)
                                               : std::size_t(found - visible_.begin());
    for (std::size_t i = 1; i <= visible_.size(); ++i) {
        const auto at =
            previous ? (start + visible_.size() - i) % visible_.size() : (start + i) % visible_.size();
        if (rows_[visible_[at]].change != ChangeKind::unchanged)
            return select_vertex(rows_[visible_[at]].vertex);
    }
    return false;
}
Json VertexAttributeView::report() const {
    std::uint64_t added = 0, removed = 0, modified = 0;
    for (const auto &r : rows_) {
        added += r.change == ChangeKind::added;
        removed += r.change == ChangeKind::removed;
        modified += r.change == ChangeKind::modified;
    }
    return {{"comparing", comparing_},
            {"channels", channels_.size()},
            {"channel", channel_},
            {"vertices", rows_.size()},
            {"visible", visible_.size()},
            {"rendered", rendered_},
            {"added", added},
            {"removed", removed},
            {"modified", modified},
            {"selected", vertex_ ? Json(*vertex_) : Json(nullptr)},
            {"selectedVisible", selected_visible_},
            {"detailRows", details_.table().rows.size()}};
}
Json VertexAttributeView::view_state() const {
    return {{"channel", channel_},
            {"vertex", vertex_ ? Json(*vertex_) : Json(nullptr)},
            {"filter", filter_.query},
            {"changedOnly", changed_only_},
            {"properties", details_.view_state()}};
}
void VertexAttributeView::restore_view_state(const Json &state) {
    view_state::validate(state, "vertex-attribute");
    const auto name = state.value("channel", channel_);
    if (!select_channel(name)) {
        channel_ = name;
        vertex_.reset();
        rebuild();
    }
    if (state.contains("filter"))
        filter_.query = state["filter"].get<std::string>();
    if (state.contains("changedOnly"))
        changed_only_ = comparing_ && state["changedOnly"].get<bool>();
    refilter();
    if (state.contains("vertex")) {
        vertex_.reset();
        if (!state["vertex"].is_null())
            select_vertex(state["vertex"].get<std::uint32_t>());
        bind_selection();
    }
    if (state.contains("properties"))
        details_.restore_view_state(state["properties"]);
}
void VertexAttributeView::draw(const char *id) {
    cyui::ScopedID scope(id);
    rendered_ = 0;
    selected_visible_ = false;
    ImGui::TextWrapped("%s",
                       cyui::tr("Stored render vertex values; no shader or deformation evaluation.").c_str());
    if (ImGui::BeginCombo("##attribute-channel", channel_.c_str())) {
        for (const auto &name : channels_)
            if (ImGui::Selectable(name.c_str(), name == channel_))
                select_channel(name);
        ImGui::EndCombo();
    }
    for (const auto &entry : std::vector<std::pair<const char *, const MeshVertexAttribute *>>{
             {"Before", channel(before_, channel_)}, {"After", channel(after_, channel_)}})
        if (entry.second)
            ImGui::Text("%s: %s | %s: %u", cyui::tr(entry.first).c_str(), entry.second->encoding().c_str(),
                        cyui::tr("Components").c_str(), entry.second->components());
    cyui::FilterOptions options;
    options.hint = "Find attribute vertices or values";
    options.width = std::min(cyui::dp(270), ImGui::GetContentRegionAvail().x);
    bool update = cyui::filterBox("##attribute-filter", filter_, options);
    if (comparing_) {
        if (ImGui::GetContentRegionAvail().x > cyui::dp(240))
            ImGui::SameLine();
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed attribute vertex").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed attribute vertex").c_str()))
            next_change();
    }
    if (update)
        refilter();
    const auto height = std::max(cyui::dp(80), ImGui::GetContentRegionAvail().y * .38f);
    ImGui::BeginChild("##attribute-vertices", {0, height}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##attribute-table", 2,
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
                ++rendered_;
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
            cyui::tr(rows_.empty() ? "No stored channel with this name" : "No matching attribute vertex")
                .c_str());
    ImGui::EndChild();
    ImGui::TextWrapped("%s", cyui::tr("Storage encoding is excluded from value changes.").c_str());
    details_.draw("##attribute-details");
}
} // namespace cyuediff
