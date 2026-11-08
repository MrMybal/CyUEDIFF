#include <cyuediff/mesh_skin_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
void MeshSkinView::register_translations() {
    cyui::registerTranslations(
        "fr", {{"Bones and weights", "Os et poids"},
               {"Find bones or vertices", "Rechercher un os ou un sommet"},
               {"Stored bones and raw weights; no skin evaluation.",
                "Os et poids bruts enregistrés ; sans calcul de skinning."},
               {"Bone", "Os"},
               {"Vertex weights", "Poids du sommet"},
               {"Previous changed skin entry", "Entrée de skin modifiée précédente"},
               {"Next changed skin entry", "Entrée de skin modifiée suivante"},
               {"No matching skin entry", "Aucune entrée de skin correspondante"},
               {"Select a bone or vertex to inspect its stored values.",
                "Sélectionnez un os ou un sommet pour inspecter ses valeurs enregistrées."}});
}
void MeshSkinView::set_skins(std::shared_ptr<const MeshSkin> before, std::shared_ptr<const MeshSkin> after,
                             bool comparing) {
    before_ = before ? before->to_json() : Json::object();
    after_ = after ? after->to_json() : Json::object();
    comparing_ = comparing || bool(before && after);
    changed_only_ = false;
    filter_ = {};
    selected_.clear();
    focus_pending_ = selected_visible_ = false;
    rendered_rows_ = 0;
    entries_.clear();
    auto add = [&](const std::string &table, const std::string &name, bool vertex) {
        const auto a = before_.contains(table) ? before_[table].find(name) : before_.end();
        const auto b = after_.contains(table) ? after_[table].find(name) : after_.end();
        const Json *av = before_.contains(table) && a != before_[table].end() ? &*a : nullptr;
        const Json *bv = after_.contains(table) && b != after_[table].end() ? &*b : nullptr;
        Entry e;
        e.vertex = vertex;
        e.key = (vertex ? "vertex/" : "bone/") + name;
        e.label = name;
        e.change = !comparing_ ? ChangeKind::unchanged
                               : (!av ? ChangeKind::added
                                      : (!bv ? ChangeKind::removed
                                             : (*av == *bv ? ChangeKind::unchanged : ChangeKind::modified)));
        e.search = e.key + " " + (av ? av->dump() : std::string{}) + " " + (bv ? bv->dump() : std::string{});
        entries_.push_back(std::move(e));
    };
    std::set<std::string> bones;
    std::set<std::uint32_t> vertices;
    for (const auto *skin : {before.get(), after.get()})
        if (skin) {
            for (const auto &bone : skin->bones)
                bones.insert(bone.name);
            for (const auto &profile : skin->profiles)
                for (const auto &v : profile.second)
                    vertices.insert(v.first);
        }
    for (const auto &bone : bones)
        add("bones", bone, false);
    for (const auto vertex : vertices)
        add("vertices", std::to_string(vertex), true);
    rebuild();
    if (!visible_.empty())
        select(entries_[visible_[0]].key);
    bind_selection();
}
void MeshSkinView::bind_selection() {
    const auto slash = selected_.find('/');
    const auto table = selected_.compare(0, 7, "vertex/") == 0 ? "vertices" : "bones";
    const auto name = slash == std::string::npos ? std::string{} : selected_.substr(slash + 1);
    auto value = [&](const Json &j) -> const Json * {
        return j.contains(table) && j[table].contains(name) ? &j[table][name] : nullptr;
    };
    details_.set_values(value(before_), value(after_), comparing_);
    details_.set_changed_only(false);
}
void MeshSkinView::rebuild() {
    visible_.clear();
    bool found = false;
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if ((!changed_only_ || entries_[i].change != ChangeKind::unchanged) &&
            filter_.matches(entries_[i].label, entries_[i].search)) {
            visible_.push_back(i);
            found = found || entries_[i].key == selected_;
        }
    if (!found) {
        selected_.clear();
        focus_pending_ = false;
        bind_selection();
    }
}
void MeshSkinView::set_filter(std::string query) {
    filter_.query = std::move(query);
    rebuild();
}
void MeshSkinView::set_changed_only(bool value) {
    changed_only_ = comparing_ && value;
    rebuild();
}
bool MeshSkinView::select(const std::string &key) {
    if (key.empty()) {
        selected_.clear();
        focus_pending_ = false;
        bind_selection();
        return true;
    }
    const auto found =
        std::find_if(visible_.begin(), visible_.end(), [&](auto i) { return entries_[i].key == key; });
    if (found == visible_.end())
        return false;
    selected_ = key;
    focus_pending_ = true;
    bind_selection();
    return true;
}
bool MeshSkinView::select_bone(const std::string &name) {
    return select("bone/" + name);
}
bool MeshSkinView::select_vertex(std::uint32_t id) {
    return select("vertex/" + std::to_string(id));
}
bool MeshSkinView::next_change(bool previous) {
    std::vector<std::size_t> changed;
    for (auto i : visible_)
        if (entries_[i].change != ChangeKind::unchanged)
            changed.push_back(i);
    if (changed.empty())
        return false;
    const auto it =
        std::find_if(changed.begin(), changed.end(), [&](auto i) { return entries_[i].key == selected_; });
    const auto n = changed.size();
    const auto index = it == changed.end()
                           ? (previous ? n - 1 : 0)
                           : (std::size_t(it - changed.begin()) + n + (previous ? -1 : 1)) % n;
    return select(entries_[changed[index]].key);
}
Json MeshSkinView::report() const {
    std::uint64_t modified = 0, added = 0, removed = 0, bones = 0, vertices = 0;
    for (const auto &e : entries_) {
        bones += !e.vertex;
        vertices += e.vertex;
        modified += e.change == ChangeKind::modified;
        added += e.change == ChangeKind::added;
        removed += e.change == ChangeKind::removed;
    }
    return {{"renderedRows", rendered_rows_},
            {"selectedVisible", selected_visible_},
            {"bones", bones},
            {"vertices", vertices},
            {"visible", visible_.size()},
            {"selected", selected_},
            {"modified", modified},
            {"added", added},
            {"removed", removed},
            {"comparing", comparing_},
            {"detailRows", details_.table().rows.size()}};
}
Json MeshSkinView::view_state() const {
    return {{"filter", filter_.query},
            {"changedOnly", changed_only_},
            {"selected", selected_.empty() ? Json(nullptr) : Json(selected_)},
            {"properties", details_.view_state()}};
}
void MeshSkinView::restore_view_state(const Json &state) {
    view_state::validate(state, "graph-inspection");
    if (state.contains("filter"))
        filter_.query = state["filter"].get<std::string>();
    if (state.contains("changedOnly"))
        changed_only_ = comparing_ && state["changedOnly"].get<bool>();
    rebuild();
    if (state.contains("selected")) {
        select("");
        if (state["selected"].is_string())
            select(state["selected"].get<std::string>());
    }
    if (!selected_.empty() && state.contains("properties"))
        details_.restore_view_state(state["properties"]);
}
void MeshSkinView::draw(const char *id) {
    cyui::ScopedID scope(id);
    rendered_rows_ = 0;
    selected_visible_ = false;
    ImGui::TextWrapped("%s", cyui::tr("Stored bones and raw weights; no skin evaluation.").c_str());
    cyui::FilterOptions options;
    options.hint = "Find bones or vertices";
    options.width = std::min(cyui::dp(270), ImGui::GetContentRegionAvail().x);
    bool update = cyui::filterBox("##skin-filter", filter_, options);
    if (comparing_) {
        if (ImGui::GetContentRegionAvail().x > cyui::dp(250))
            ImGui::SameLine();
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed skin entry").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed skin entry").c_str()))
            next_change();
    }
    if (update)
        rebuild();
    const auto height =
        selected_.empty() ? 0 : std::max(cyui::dp(100), ImGui::GetContentRegionAvail().y * .38f);
    ImGui::BeginChild("##skin-entries", {0, height}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##skin-table", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                          {0, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupColumn(cyui::tr("Name").c_str());
        ImGui::TableSetupColumn(cyui::tr("Type").c_str());
        ImGui::TableSetupColumn(cyui::tr("Change").c_str());
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_.size()));
        if (focus_pending_)
            for (std::size_t i = 0; i < visible_.size(); ++i)
                if (entries_[visible_[i]].key == selected_) {
                    clipper.IncludeItemByIndex(static_cast<int>(i));
                    break;
                }

        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto &e = entries_[visible_[row]];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                cyui::ScopedID item(e.key.c_str());
                if (ImGui::Selectable(e.label.c_str(), selected_ == e.key,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    select(e.key);
                ++rendered_rows_;
                if (selected_ == e.key) {
                    selected_visible_ = ImGui::IsItemVisible();
                    if (focus_pending_) {
                        ImGui::SetScrollHereY(.4f);
                        focus_pending_ = false;
                    }
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(cyui::tr(e.vertex ? "Vertex weights" : "Bone").c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::PushStyleColor(ImGuiCol_Text, detail::change_color(e.change));
                ImGui::TextUnformatted(cyui::tr(change_name(e.change)).c_str());
                ImGui::PopStyleColor();
            }
        ImGui::EndTable();
    }
    if (visible_.empty())
        cyui::textDim(cyui::tr("No matching skin entry").c_str());
    ImGui::EndChild();
    if (!selected_.empty()) {
        const bool vertex = selected_.compare(0, 7, "vertex/") == 0;
        ImGui::TextWrapped("%s | %s", cyui::tr(vertex ? "Vertex weights" : "Bone").c_str(),
                           selected_.substr(vertex ? 7 : 5).c_str());
        details_.draw("##skin-details");
    } else
        cyui::textDim(cyui::tr("Select a bone or vertex to inspect its stored values.").c_str());
}
} // namespace cyuediff
