#include <cyuediff/animation_metadata_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
namespace {
std::string token(const std::string &s) {
    std::string out;
    for (char c : s)
        out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
std::string curve_key(const std::string &container, const std::string &name) {
    return "/curves/" + token(container) + "/" + token(name);
}
std::string marker_key(const std::string &name) { return "/markers/" + token(name); }
} // namespace
void AnimationMetadataView::register_translations() {
    cyui::registerTranslations(
        "fr",
        {{"Animation metadata", "Métadonnées d’animation"},
         {"Stored curve flags, bone links, LOD limits and sync marker names.",
          "Indicateurs de courbes, liens vers les os, limites de LOD et noms de marqueurs enregistrés."},
         {"Some animation metadata is unavailable. Missing entries may be unknown.",
          "Certaines métadonnées sont indisponibles. Les entrées manquantes peuvent être inconnues."},
         {"Animation list is limited to 4,096 entries.", "La liste est limitée à 4 096 entrées."},
         {"Find curves or markers", "Rechercher des courbes ou marqueurs"},
         {"Previous changed entry", "Entrée modifiée précédente"},
         {"Next changed entry", "Entrée modifiée suivante"},
         {"Curve", "Courbe"},
         {"Sync marker", "Marqueur de synchronisation"},
         {"Container", "Conteneur"},
         {"Entry details", "Détails de l’entrée"},
         {"Select an entry to inspect its stored metadata.",
          "Sélectionnez une entrée pour consulter ses métadonnées."},
         {"No matching animation metadata", "Aucune métadonnée d’animation correspondante"},
         {"Unknown opposite table; absence is not a confirmed removal or addition.",
          "Table opposée inconnue ; l’absence ne confirme aucun ajout ni retrait."},
         {"Unavailable", "Indisponible"}});
}
void AnimationMetadataView::set_models(const Json *a, const Json *b, bool comparing) {
    before_ = Json::object();
    after_ = Json::object();
    entries_.clear();
    visible_.clear();
    selected_.clear();
    filter_ = {};
    changed_only_ = false;
    truncated_ = false;
    comparing_ = comparing || (a && b);
    complete_ = (!a || a->at("complete").get<bool>()) && (!b || b->at("complete").get<bool>()) && (a || b);
    auto flatten = [&](const Json *body, Json &out) {
        if (!body)
            return;
        for (auto container = body->at("curveMetadata").at("containers").begin();
             container != body->at("curveMetadata").at("containers").end(); ++container)
            for (auto curve = container->begin(); curve != container->end(); ++curve) {
                const auto key = curve_key(container.key(), curve.key());
                // Retain a bounded common union; no extra drawing or copies for overflow rows.
                if (!entries_.count(key) && entries_.size() >= 4096) {
                    truncated_ = true;
                    continue;
                }
                entries_[key] = {curve.key(), container.key(), false};
                out[key] = *curve;
            }
        for (auto marker = body->at("syncMarkers").at("names").begin();
             marker != body->at("syncMarkers").at("names").end(); ++marker) {
            const auto key = marker_key(marker.key());
            if (!entries_.count(key) && entries_.size() >= 4096) {
                truncated_ = true;
                continue;
            }
            entries_[key] = {marker.key(), "", true};
            out[key] = *marker;
        }
    };
    flatten(a, before_);
    flatten(b, after_);
    auto known = [](const Json *body, bool marker) {
        if (!body)
            return true; // Known absent object side.
        const auto c = body->at(marker ? "syncMarkers" : "curveMetadata").at("coverage");
        return c == "stored" || (marker && c == "not-stored");
    };
    for (auto &item : entries_) {
        auto &entry = item.second;
        const auto x = before_.find(item.first), y = after_.find(item.first);
        entry.known = (!comparing_ || ((x != before_.end() || known(a, entry.marker)) &&
                                       (y != after_.end() || known(b, entry.marker))));
        entry.change = !comparing_ || !entry.known ? ChangeKind::unchanged
                                                   : classify_change(x == before_.end() ? nullptr : &*x,
                                                                     y == after_.end() ? nullptr : &*y);
    }
    rebuild();
    bind_selection();
}
void AnimationMetadataView::rebuild() {
    visible_.clear();
    for (const auto &item : entries_)
        if ((!changed_only_ || !item.second.known || item.second.change != ChangeKind::unchanged) &&
            filter_.matches(item.second.name, item.second.container))
            visible_.push_back(item.first);
    if (!selected_.empty() && std::find(visible_.begin(), visible_.end(), selected_) == visible_.end()) {
        selected_.clear();
        bind_selection();
    }
}
void AnimationMetadataView::bind_selection() {
    const auto a = before_.find(selected_), b = after_.find(selected_);
    const auto *x = a == before_.end() ? nullptr : &*a, *y = b == after_.end() ? nullptr : &*b;
    if (!selected_.empty() && !selected_known())
        details_.set_values(x ? x : y, nullptr, false);
    else
        details_.set_values(x, y, comparing_);
}
bool AnimationMetadataView::select(const std::string &key) {
    if (!key.empty() && std::find(visible_.begin(), visible_.end(), key) == visible_.end())
        return false;
    if (key != selected_) {
        selected_ = key;
        bind_selection();
    }
    return true;
}
void AnimationMetadataView::set_filter(std::string q) {
    filter_.query = std::move(q);
    rebuild();
}
void AnimationMetadataView::set_changed_only(bool v) {
    changed_only_ = comparing_ && v;
    rebuild();
}
bool AnimationMetadataView::select_curve(const std::string &c, const std::string &n) {
    return select(curve_key(c, n));
}
bool AnimationMetadataView::select_marker(const std::string &n) { return select(marker_key(n)); }
bool AnimationMetadataView::selected_known() const {
    const auto it = entries_.find(selected_);
    return it != entries_.end() && it->second.known;
}
bool AnimationMetadataView::next_change(bool previous) {
    std::vector<std::string> changed;
    for (const auto &key : visible_)
        if (entries_.at(key).known && entries_.at(key).change != ChangeKind::unchanged)
            changed.push_back(key);
    if (changed.empty())
        return false;
    const auto it = std::find(changed.begin(), changed.end(), selected_);
    std::size_t i =
        it == changed.end()
            ? (previous ? changed.size() - 1 : 0)
            : (std::size_t(it - changed.begin()) + changed.size() + (previous ? -1 : 1)) % changed.size();
    return select(changed[i]);
}
Json AnimationMetadataView::view_state() const {
    Json out = Json::object();
    if (!filter_.query.empty())
        out["filter"] = filter_.query;
    if (changed_only_)
        out["changedOnly"] = true;
    if (!selected_.empty()) {
        out["selected"] = selected_;
        out["properties"] = details_.view_state();
    }
    return out;
}
void AnimationMetadataView::restore_view_state(const Json &state) {
    view_state::validate(state, "graph-inspection");
    if (state.contains("filter"))
        filter_.query = state.at("filter").get<std::string>();
    if (state.contains("changedOnly"))
        changed_only_ = comparing_ && state.at("changedOnly").get<bool>();
    rebuild();
    if (state.contains("selected")) {
        select("");
        if (state.at("selected").is_string())
            select(state.at("selected").get<std::string>());
    }
    if (!selected_.empty() && state.contains("properties"))
        details_.restore_view_state(state.at("properties"));
}
void AnimationMetadataView::draw(const char *id) {
    cyui::ScopedID scope(id);
    ImGui::TextWrapped("%s",
                       cyui::tr("Stored curve flags, bone links, LOD limits and sync marker names.").c_str());
    if (!complete_)
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("Some animation metadata is unavailable. Missing entries may be unknown.").c_str());
    if (truncated_)
        cyui::callout(cyui::Tone::warning, cyui::tr("Animation list is limited to 4,096 entries.").c_str());
    cyui::FilterOptions filter;
    filter.hint = "Find curves or markers";
    filter.width = std::min(cyui::dp(260), ImGui::GetContentRegionAvail().x);
    bool update = cyui::filterBox("##animation-filter", filter_, filter);
    if (comparing_) {
        if (ImGui::GetContentRegionAvail().x > cyui::dp(250))
            ImGui::SameLine();
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed entry").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed entry").c_str()))
            next_change();
    }
    if (update)
        rebuild();
    const float height =
        selected_.empty() ? 0 : std::max(cyui::dp(100), ImGui::GetContentRegionAvail().y * .4f);
    ImGui::BeginChild("##animation-entries", {0, height}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##animation-table", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                          {0, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupColumn(cyui::tr("Name").c_str(), ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn(cyui::tr("Container").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn(cyui::tr("Status").c_str(), ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(int(visible_.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto &key = visible_[std::size_t(i)];
                const auto &entry = entries_.at(key);
                cyui::ScopedID row(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, entry.known ? detail::change_color(entry.change)
                                                                 : cyui::palette().textDim);
                if (ImGui::Selectable("##entry", selected_ == key, ImGuiSelectableFlags_SpanAllColumns))
                    select(key);
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::PopStyleColor();
                cyui::tooltip(key.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.marker ? cyui::tr("Sync marker").c_str()
                                                    : entry.container.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cyui::tr(!entry.known   ? "Unavailable"
                                                : comparing_   ? change_name(entry.change)
                                                : entry.marker ? "Sync marker"
                                                               : "Curve")
                                           .c_str());
            }
        ImGui::EndTable();
    }
    if (visible_.empty())
        cyui::textDim("%s", cyui::tr("No matching animation metadata").c_str());
    ImGui::EndChild();
    if (!selected_.empty()) {
        if (!selected_known())
            cyui::textDim(
                "%s",
                cyui::tr("Unknown opposite table; absence is not a confirmed removal or addition.").c_str());
        ImGui::BeginChild("##animation-detail", {0, 0}, ImGuiChildFlags_Borders);
        details_.draw("Entry details");
        ImGui::EndChild();
    }
}
} // namespace cyuediff
