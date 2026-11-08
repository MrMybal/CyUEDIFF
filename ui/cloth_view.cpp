#include <cyuediff/cloth_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
namespace {
const Json *section(const Json &j, std::uint32_t id) {
    const auto key = std::to_string(id);
    return j.is_object() && j.contains("sections") && j["sections"].contains(key) ? &j["sections"][key]
                                                                                  : nullptr;
}
Json binding(const Json *s) {
    if (!s)
        return nullptr;
    Json out = *s;
    out.erase("lodBiases");
    return out;
}
const Json *vertices(const Json *s, std::uint32_t bias) {
    const auto key = std::to_string(bias);
    return s && s->contains("lodBiases") && (*s)["lodBiases"].contains(key) ? &(*s)["lodBiases"][key]
                                                                            : nullptr;
}
const Json *vertex(const Json *v, std::uint32_t id) {
    const auto key = std::to_string(id);
    return v && v->contains(key) ? &(*v)[key] : nullptr;
}
} // namespace
void ClothView::register_translations() {
    cyui::registerTranslations(
        "fr", {{"Cloth mappings", "Correspondances de tissu"},
               {"Section", "Section"},
               {"LOD bias", "Décalage de LOD"},
               {"Stored render-to-simulation mappings; no cloth simulation.",
                "Correspondances du rendu vers le maillage de simulation ; sans simulation de tissu."},
               {"Padding and inactive coefficients are excluded from changes.",
                "Les octets de remplissage et les coefficients inactifs sont exclus des différences."},
               {"Find cloth vertices or values", "Rechercher un sommet de tissu ou une valeur"},
               {"Previous changed cloth vertex", "Sommet de tissu modifié précédent"},
               {"Next changed cloth vertex", "Sommet de tissu modifié suivant"},
               {"No stored mappings for this section and LOD bias",
                "Aucune correspondance enregistrée pour cette section et ce décalage de LOD"},
               {"No matching cloth vertex", "Aucun sommet de tissu correspondant"},
               {"Render vertex", "Sommet de rendu"}});
}
void ClothView::set_cloths(std::shared_ptr<const MeshCloth> a, std::shared_ptr<const MeshCloth> b,
                           bool comparing) {
    before_ = a ? a->semantic_snapshot() : Json::object();
    after_ = b ? b->semantic_snapshot() : Json::object();
    comparing_ = comparing || bool(a && b);
    mappings_.clear();
    std::set<std::pair<std::uint32_t, std::uint32_t>> modes;
    for (const auto *model : {a.get(), b.get()})
        if (model)
            for (const auto &s : model->sections)
                if (s->has_stored_data())
                    for (std::uint32_t bi = 0; bi < std::max<std::size_t>(1, s->lod_biases.size()); ++bi)
                        modes.insert({s->section, bi});
    mappings_.assign(modes.begin(), modes.end());
    section_ = bias_ = 0;
    if (!mappings_.empty()) {
        section_ = mappings_[0].first;
        bias_ = mappings_[0].second;
    }
    vertex_.reset();
    filter_ = {};
    changed_only_ = false;
    rendered_ = 0;
    selected_visible_ = false;
    rebuild();
}
void ClothView::rebuild() {
    rows_.clear();
    visible_.clear();
    const auto *sa = section(before_, section_), *sb = section(after_, section_);
    const auto *a = vertices(sa, bias_), *b = vertices(sb, bias_);
    const bool changed_binding = binding(sa) != binding(sb);
    std::set<std::uint32_t> ids;
    for (const auto *v : {a, b})
        if (v)
            for (auto i = v->begin(); i != v->end(); ++i)
                ids.insert(static_cast<std::uint32_t>(std::stoul(i.key())));
    for (auto id : ids) {
        const auto *av = vertex(a, id), *bv = vertex(b, id);
        const auto change = !comparing_                     ? ChangeKind::unchanged
                            : !av                           ? ChangeKind::added
                            : !bv                           ? ChangeKind::removed
                            : changed_binding || *av != *bv ? ChangeKind::modified
                                                            : ChangeKind::unchanged;
        rows_.push_back(
            {id, change, std::to_string(id) + " " + (av ? av->dump() : "") + " " + (bv ? bv->dump() : "")});
        if ((!changed_only_ || change != ChangeKind::unchanged) && filter_.matches(rows_.back().search))
            visible_.push_back(rows_.size() - 1);
    }
    if (vertex_ &&
        std::none_of(visible_.begin(), visible_.end(), [&](auto i) { return rows_[i].vertex == *vertex_; }))
        vertex_.reset();
    bind_selection();
}
void ClothView::bind_selection() {
    const auto detail = [&](const Json &j) {
        const auto *s = section(j, section_);
        if (!s)
            return Json(nullptr);
        Json out = {{"binding", binding(s)}};
        if (vertex_) {
            const auto *v = vertex(vertices(s, bias_), *vertex_);
            out["influences"] = v ? *v : Json(nullptr);
        }
        return out;
    };
    const auto a = detail(before_), b = detail(after_);
    details_.set_values(a.is_null() ? nullptr : &a, b.is_null() ? nullptr : &b, comparing_);
}
bool ClothView::select_mapping(std::uint32_t s, std::uint32_t b) {
    if (std::find(mappings_.begin(), mappings_.end(), std::make_pair(s, b)) == mappings_.end())
        return false;
    section_ = s;
    bias_ = b;
    vertex_.reset();
    rebuild();
    return true;
}
bool ClothView::select_vertex(std::uint32_t v) {
    if (std::none_of(visible_.begin(), visible_.end(), [&](auto i) { return rows_[i].vertex == v; }))
        return false;
    vertex_ = v;
    focus_pending_ = true;
    bind_selection();
    return true;
}
void ClothView::set_filter(std::string s) {
    filter_.query = std::move(s);
    rebuild();
}
void ClothView::set_changed_only(bool v) {
    changed_only_ = comparing_ && v;
    rebuild();
}
bool ClothView::next_change(bool previous) {
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
Json ClothView::report() const {
    std::uint64_t modified = 0, added = 0, removed = 0;
    for (const auto &r : rows_) {
        modified += r.change == ChangeKind::modified;
        added += r.change == ChangeKind::added;
        removed += r.change == ChangeKind::removed;
    }
    return {{"comparing", comparing_},
            {"section", section_},
            {"lodBias", bias_},
            {"mappings", mappings_.size()},
            {"vertices", rows_.size()},
            {"visible", visible_.size()},
            {"rendered", rendered_},
            {"modified", modified},
            {"added", added},
            {"removed", removed},
            {"selected", vertex_ ? Json(*vertex_) : Json(nullptr)},
            {"selectedVisible", selected_visible_},
            {"detailRows", details_.table().rows.size()}};
}
Json ClothView::view_state() const {
    return {{"section", section_},
            {"lodBias", bias_},
            {"vertex", vertex_ ? Json(*vertex_) : Json(nullptr)},
            {"filter", filter_.query},
            {"changedOnly", changed_only_},
            {"properties", details_.view_state()}};
}
void ClothView::restore_view_state(const Json &j) {
    view_state::validate(j, "cloth");
    const auto selected_section = j.value("section", section_), selected_bias = j.value("lodBias", bias_);
    if (!select_mapping(selected_section, selected_bias)) {
        section_ = selected_section;
        bias_ = selected_bias;
        vertex_.reset();
    }
    if (j.contains("filter"))
        filter_.query = j["filter"].get<std::string>();
    if (j.contains("changedOnly"))
        changed_only_ = comparing_ && j["changedOnly"].get<bool>();
    rebuild();
    if (j.contains("vertex")) {
        vertex_.reset();
        if (!j["vertex"].is_null())
            select_vertex(j["vertex"].get<std::uint32_t>());
        else
            bind_selection();
    }
    if (j.contains("properties"))
        details_.restore_view_state(j["properties"]);
}
void ClothView::draw(const char *id) {
    cyui::ScopedID scope(id);
    rendered_ = 0;
    selected_visible_ = false;
    ImGui::TextWrapped("%s", cyui::tr("Stored render-to-simulation mappings; no cloth simulation.").c_str());
    const auto label = [&](const auto &m) {
        return cyui::tr("Section") + " " + std::to_string(m.first) + " | " + cyui::tr("LOD bias") + " " +
               std::to_string(m.second);
    };
    if (ImGui::BeginCombo("##cloth-mapping", label(std::make_pair(section_, bias_)).c_str())) {
        for (const auto &m : mappings_)
            if (ImGui::Selectable(label(m).c_str(), m.first == section_ && m.second == bias_))
                select_mapping(m.first, m.second);
        ImGui::EndCombo();
    }
    cyui::FilterOptions options;
    options.hint = "Find cloth vertices or values";
    options.width = std::min(cyui::dp(270), ImGui::GetContentRegionAvail().x);
    bool update = cyui::filterBox("##cloth-filter", filter_, options);
    if (comparing_) {
        if (ImGui::GetContentRegionAvail().x > cyui::dp(240))
            ImGui::SameLine();
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed cloth vertex").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed cloth vertex").c_str()))
            next_change();
    }
    if (update)
        rebuild();
    const auto height = std::max(cyui::dp(80), ImGui::GetContentRegionAvail().y * .38f);
    ImGui::BeginChild("##cloth-vertices", {0, height}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##cloth-table", 2,
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
                const auto name = std::to_string(r.vertex);
                cyui::ScopedID item(name.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(name.c_str(), vertex_ && *vertex_ == r.vertex,
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
        cyui::textDim(cyui::tr(rows_.empty() ? "No stored mappings for this section and LOD bias"
                                             : "No matching cloth vertex")
                          .c_str());
    ImGui::EndChild();
    ImGui::TextWrapped("%s",
                       cyui::tr("Padding and inactive coefficients are excluded from changes.").c_str());
    details_.draw("##cloth-details");
}
} // namespace cyuediff
