#include <cyuediff/curve_view.hpp>
#include "../src/curves_internal.hpp"
#include "diff_style.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace cyuediff {
void CurveView::register_translations() {
    cyui::registerTranslations(
        "fr",
        {{"Curves", "Courbes"},
         {"Stored curves", "Courbes enregistrées"},
         {"Curve owner before", "Objet courbe avant"},
         {"Curve owner after", "Objet courbe après"},
         {"Curve owner", "Objet courbe"},
         {"Curve lane", "Canal de courbe"},
         {"Curve time", "Temps de la courbe"},
         {"Find source keys", "Rechercher des clés source"},
         {"Stored curve keys", "Clés de courbe enregistrées"},
         {"No stored rich curve", "Aucune courbe riche enregistrée"},
         {"Unknown curve fields remain explicit. Weighted segments and extrapolation are unavailable.",
          "Les champs inconnus restent explicites. Les segments pondérés et l’extrapolation sont "
          "indisponibles."},
         {"Stored tangents; supported segments use double-precision source evaluation.",
          "Tangentes enregistrées ; les segments pris en charge sont évalués en double précision."},
         {"Unknown opposite keys; absence is not a confirmed change.",
          "Clés opposées inconnues ; l’absence ne confirme aucun changement."},
         {"Curve key details", "Détails de la clé"},
         {"Time", "Temps"},
         {"Evaluated source value", "Valeur source évaluée"},
         {"Curve values unavailable", "Valeurs de courbe indisponibles"},
         {"Before %.8g | After %.8g", "Avant %.8g | Après %.8g"},
         {"Key list is limited to 4,096 entries.", "La liste est limitée à 4 096 clés."},
         {"Orange: before | Green: after | Dots: stored keys | Click to choose time.",
          "Orange : avant | Vert : après | Points : clés enregistrées | Cliquez pour choisir le temps."}});
}
const Json *CurveView::owner(bool side) const {
    const auto &r = owners_[side];
    auto it = r.find(objects_[side]);
    return it == r.end() ? nullptr : &*it;
}
const Json *CurveView::lane(bool side) const {
    const auto *o = owner(side);
    if (!o)
        return nullptr;
    auto it = o->at("lanes").find(lane_);
    return it == o->at("lanes").end() ? nullptr : &*it;
}
const Json *CurveView::key(bool side, const std::string &id) const {
    const auto *l = lane(side);
    if (!l)
        return nullptr;
    auto it = l->at("keys").find(id);
    return it == l->at("keys").end() ? nullptr : &*it;
}
bool CurveView::empty() const { return owners_[0].empty() && owners_[1].empty(); }
void CurveView::set_packages(const Package *a, const Package *b, bool comparing) {
    comparing_ = comparing || (a && b);
    filter_ = {};
    changed_only_ = false;
    time_ = 0;
    selected_.clear();
    lane_.clear();
    for (unsigned side = 0; side < 2; ++side) {
        const auto *p = side ? b : a;
        present_[side] = p;
        input_[side] = p && p->complete_input;
        if (p) {
            CurveOptions o;
            o.source_indices = false;
            owners_[side] = curve_snapshot(*p, o).at("owners");
        } else
            owners_[side] = Json::object();
        objects_[side] = owners_[side].empty() ? "" : owners_[side].begin().key();
    }
    bind();
}
bool CurveView::select_objects(std::string a, std::string b) {
    objects_[0] = std::move(a);
    objects_[1] = std::move(b);
    bind();
    return (!present_[0] || owner(false)) && (!present_[1] || owner(true));
}
bool CurveView::select_lane(const std::string &id) {
    if (std::find(lanes_.begin(), lanes_.end(), id) == lanes_.end())
        return false;
    lane_ = id;
    selected_.clear();
    bind();
    return true;
}
void CurveView::bind() {
    std::set<std::string> common;
    truncated_ = false;
    for (bool side : {false, true})
        if (const auto *o = owner(side))
            for (auto it = o->at("lanes").begin(); it != o->at("lanes").end(); ++it)
                if (common.size() < 4096 || common.count(it.key()))
                    common.insert(it.key());
                else
                    truncated_ = true;
    lanes_ = {common.begin(), common.end()};
    if (lane_.empty() && !lanes_.empty())
        lane_ = lanes_[0];
    entries_.clear();
    for (bool side : {false, true})
        if (const auto *l = lane(side))
            for (auto it = l->at("keys").begin(); it != l->at("keys").end(); ++it) {
                if (entries_.size() >= 4096 && !entries_.count(it.key())) {
                    truncated_ = true;
                    continue;
                }
                if (it->contains("time"))
                    entries_[it.key()].time = it->at("time").get<double>();
                else
                    entries_[it.key()] = {};
            }
    auto known = [&](bool side, const Json *k) {
        const auto *o = owner(side), *l = lane(side);
        return !present_[side] || (input_[side] && o &&
                                   (k || (l && l->at("keysCoverage") == "stored") ||
                                    (!l && o->at("lanesCoverage") == "stored")));
    };
    for (auto &e : entries_) {
        const auto *a = key(false, e.first), *b = key(true, e.first);
        e.second.known = (!comparing_ || (known(false, a) && known(true, b))) &&
                         (!a || a->value("complete", false)) && (!b || b->value("complete", false));
        e.second.change = !comparing_ || !e.second.known ? ChangeKind::unchanged : classify_change(a, b);
    }
    rebuild();
    prepare_plot();
    sample();
    bind_details();
}
void CurveView::rebuild() {
    visible_.clear();
    for (const auto &e : entries_)
        if (filter_.matches(e.first, std::to_string(e.second.time)) &&
            (!changed_only_ || !e.second.known || e.second.change != ChangeKind::unchanged))
            visible_.push_back(e.first);
    std::sort(visible_.begin(), visible_.end(), [&](const std::string &a, const std::string &b) {
        return entries_.at(a).time == entries_.at(b).time ? a < b : entries_.at(a).time < entries_.at(b).time;
    });
    if (std::find(visible_.begin(), visible_.end(), selected_) == visible_.end())
        selected_ = visible_.empty() ? "" : visible_[0];
}
bool CurveView::select_key(const std::string &id) {
    if (!id.empty() && std::find(visible_.begin(), visible_.end(), id) == visible_.end())
        return false;
    selected_ = id;
    const auto *k = key(false, id);
    if (!k)
        k = key(true, id);
    if (k && k->contains("time"))
        set_time(k->at("time").get<double>());
    bind_details();
    return true;
}
void CurveView::set_time(double time) {
    if (!std::isfinite(time))
        throw Error("view-state", "Curve display time must be finite");
    time_ = time;
    sample();
}
void CurveView::set_filter(std::string q) {
    filter_.query = std::move(q);
    rebuild();
    bind_details();
}
void CurveView::set_changed_only(bool v) {
    changed_only_ = comparing_ && v;
    rebuild();
    bind_details();
}
bool CurveView::selected_known() const {
    auto it = entries_.find(selected_);
    return it != entries_.end() && it->second.known;
}
bool CurveView::next_change(bool previous) {
    std::vector<std::string> changed;
    for (const auto &id : visible_)
        if (entries_.at(id).known && entries_.at(id).change != ChangeKind::unchanged)
            changed.push_back(id);
    if (changed.empty())
        return false;
    auto it = std::find(changed.begin(), changed.end(), selected_);
    const auto i =
        it == changed.end()
            ? (previous ? changed.size() - 1 : 0)
            : (std::size_t(it - changed.begin()) + changed.size() + (previous ? -1 : 1)) % changed.size();
    return select_key(changed[i]);
}
void CurveView::bind_details() {
    const auto *a = key(false, selected_), *b = key(true, selected_);
    if (comparing_ && !selected_known())
        details_.set_values(a ? a : b, nullptr, false);
    else
        details_.set_values(a, b, comparing_);
}
void CurveView::sample() {
    for (bool side : {false, true}) {
        const auto *l = lane(side);
        values_[side] = l ? detail::evaluate_curve_lane(*l, time_, input_[side])
                          : Json{{"time", time_}, {"ready", false}, {"value", nullptr}};
    }
}
void CurveView::prepare_plot() {
    bool first = true;
    low_ = high_ = 0;
    for (bool side : {false, true})
        if (const auto *l = lane(side))
            for (const auto &k : l->at("keys"))
                if (k.contains("time")) {
                    const auto t = k.at("time").get<double>();
                    if (first) {
                        low_ = high_ = t;
                        first = false;
                    } else {
                        low_ = std::min(low_, t);
                        high_ = std::max(high_, t);
                    }
                }
    for (bool side : {false, true}) {
        plot_[side].clear();
        markers_[side].clear();
        const auto *l = lane(side);
        if (!l || first)
            continue;
        for (const auto &id : l->at("keyOrder")) {
            if (markers_[side].size() >= 4096)
                break;
            const auto &k = l->at("keys").at(id.get<std::string>());
            if (k.contains("time") && k.contains("value"))
                markers_[side].push_back({k.at("time").get<double>(), k.at("value").get<double>()});
        }
        const unsigned count = low_ == high_ ? 1 : 512;
        for (unsigned i = 0; i < count; ++i) {
            const auto t =
                count == 1 ? low_ : (1. - double(i) / (count - 1)) * low_ + double(i) / (count - 1) * high_;
            auto v = detail::evaluate_curve_lane(*l, t, input_[side]);
            plot_[side].push_back({t, v["ready"].get<bool>() ? v["value"].get<double>() : 0.,
                                   v["ready"].get<bool>() ? 1. : 0.});
        }
    }
}
Json CurveView::view_state() const {
    return {{"beforeObject", objects_[0]},
            {"afterObject", objects_[1]},
            {"lane", lane_},
            {"time", time_},
            {"inspection",
             {{"selected", selected_},
              {"filter", filter_.query},
              {"changedOnly", changed_only_},
              {"properties", details_.view_state()}}}};
}
void CurveView::restore_view_state(const Json &v) {
    view_state::validate(v, "curves");
    if (v.contains("beforeObject"))
        objects_[0] = v["beforeObject"].get<std::string>();
    if (v.contains("afterObject"))
        objects_[1] = v["afterObject"].get<std::string>();
    if (v.contains("lane"))
        lane_ = v["lane"].get<std::string>();
    if (v.contains("time"))
        time_ = v["time"].get<double>();
    if (v.contains("inspection")) {
        const auto &i = v["inspection"];
        selected_ = i.value("selected", std::string());
        filter_.query = i.value("filter", std::string());
        changed_only_ = comparing_ && i.value("changedOnly", false);
    }
    bind();
    if (v.contains("inspection") && v["inspection"].contains("properties"))
        details_.restore_view_state(v["inspection"]["properties"]);
}
void CurveView::draw_plot() {
    const auto pos = ImGui::GetCursorScreenPos();
    const ImVec2 size{std::max(1.f, ImGui::GetContentRegionAvail().x), cyui::dp(140)};
    ImGui::InvisibleButton("##curve-chart", size);
    auto *draw = ImGui::GetWindowDrawList();
    const auto &p = cyui::palette();
    double lo = 0, hi = 0;
    bool first = true;
    auto include = [&](double v) {
        if (first) {
            lo = hi = v;
            first = false;
        } else {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    };
    for (const auto &side : plot_)
        for (const auto &point : side)
            if (point[2])
                include(point[1]);
    for (const auto &side : markers_)
        for (const auto &p : side)
            include(p[1]);
    const auto xs = std::max({std::abs(low_), std::abs(high_), 1e-30}),
               ys = std::max({std::abs(lo), std::abs(hi), 1e-30});
    auto left = low_ / xs, right = high_ / xs, bottom = lo / ys, top = hi / ys;
    if (right - left < 1e-12) {
        left -= .5;
        right += .5;
    }
    if (top - bottom < 1e-12) {
        bottom -= .5;
        top += .5;
    }
    auto point = [&](double x, double y) {
        return ImVec2{pos.x + float((x / xs - left) / (right - left)) * size.x,
                      pos.y + size.y - float((y / ys - bottom) / (top - bottom)) * size.y};
    };
    const ImVec2 end{pos.x + size.x, pos.y + size.y};
    draw->AddRect(pos, end, p.textDim);
    draw->PushClipRect(pos, end, true);
    for (unsigned side = 0; side < 2; ++side) {
        const auto color = side ? p.success : p.warning;
        for (std::size_t i = 1; i < plot_[side].size(); ++i) {
            const auto &a = plot_[side][i - 1], &b = plot_[side][i];
            if (a[2] && b[2])
                draw->AddLine(point(a[0], a[1]), point(b[0], b[1]), color, cyui::dp(2));
        }
        for (const auto &k : markers_[side])
            draw->AddCircleFilled(point(k[0], k[1]), cyui::dp(3), color);
    }
    const auto cursor = point(time_, 0).x;
    if (cursor >= pos.x && cursor <= end.x)
        draw->AddLine({cursor, pos.y}, {cursor, end.y}, p.text);
    draw->PopClipRect();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto ratio = std::clamp(double((ImGui::GetIO().MousePos.x - pos.x) / size.x), 0., 1.);
        set_time((1 - ratio) * low_ + ratio * high_);
    }
    cyui::textDim(
        "%s", cyui::tr("Orange: before | Green: after | Dots: stored keys | Click to choose time.").c_str());
}
void CurveView::draw(const char *id) {
    cyui::ScopedID scope(id);
    if (empty()) {
        cyui::textDim("%s", cyui::tr("No stored rich curve").c_str());
        return;
    }
    ImGui::TextWrapped(
        "%s",
        cyui::tr("Stored tangents; supported segments use double-precision source evaluation.").c_str());
    for (unsigned side = 0; side < 2; ++side)
        if (present_[side]) {
            ImGui::SetNextItemWidth(std::min(cyui::dp(360), ImGui::GetContentRegionAvail().x));
            const char *label =
                comparing_ ? (side ? "Curve owner after" : "Curve owner before") : "Curve owner";
            std::string choice;
            bool changed = false;
            if (ImGui::BeginCombo(cyui::trLabel(label).c_str(), objects_[side].c_str())) {
                for (auto it = owners_[side].begin(); it != owners_[side].end(); ++it)
                    if (ImGui::Selectable(it.key().c_str(), objects_[side] == it.key())) {
                        choice = it.key();
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            if (changed) {
                objects_[side] = std::move(choice);
                bind();
            }
        }
    std::string chosen;
    ImGui::SetNextItemWidth(std::min(cyui::dp(280), ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo(cyui::trLabel("Curve lane").c_str(), lane_.c_str())) {
        for (const auto &l : lanes_)
            if (ImGui::Selectable(l.c_str(), lane_ == l))
                chosen = l;
        ImGui::EndCombo();
    }
    if (!chosen.empty())
        select_lane(chosen);
    cyui::FilterOptions filter;
    filter.hint = "Find source keys";
    filter.width = std::min(cyui::dp(240), ImGui::GetContentRegionAvail().x);
    bool update = cyui::filterBox("##curve-filter", filter_, filter);
    if (comparing_) {
        if (ImGui::GetContentRegionAvail().x > cyui::dp(420))
            ImGui::SameLine();
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed entry").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed entry").c_str()))
            next_change(false);
    }
    if (update) {
        rebuild();
        bind_details();
    }
    if (truncated_)
        cyui::textDim("%s", cyui::tr("Key list is limited to 4,096 entries.").c_str());
    ImGui::BeginChild("##curve-keys", {0, cyui::dp(110)}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##curve-table", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                          {0, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupColumn(cyui::tr("Time").c_str());
        ImGui::TableSetupColumn(cyui::tr("Status").c_str());
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(int(visible_.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const auto &key = visible_[i];
                const auto &e = entries_.at(key);
                cyui::ScopedID row(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      e.known ? detail::change_color(e.change) : cyui::palette().textDim);
                if (ImGui::Selectable("##source-key", selected_ == key, ImGuiSelectableFlags_SpanAllColumns))
                    select_key(key);
                ImGui::SameLine();
                ImGui::Text("%.9g", e.time);
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cyui::tr(!e.known     ? "Unavailable"
                                                : comparing_ ? change_name(e.change)
                                                             : "Value")
                                           .c_str());
            }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    double time = time_;
    ImGui::SetNextItemWidth(std::min(cyui::dp(240), ImGui::GetContentRegionAvail().x));
    if (ImGui::InputDouble(cyui::trLabel("Curve time").c_str(), &time, 0, 0, "%.9g") && std::isfinite(time))
        set_time(time);
    draw_plot();
    if (values_[0].value("ready", false) && values_[1].value("ready", false))
        ImGui::Text(cyui::tr("Before %.8g | After %.8g").c_str(), values_[0]["value"].get<double>(),
                    values_[1]["value"].get<double>());
    else if (values_[0].value("ready", false) || values_[1].value("ready", false))
        ImGui::Text("%s: %.8g", cyui::tr("Evaluated source value").c_str(),
                    values_[values_[0].value("ready", false) ? 0 : 1]["value"].get<double>());
    else
        cyui::textDim("%s", cyui::tr("Curve values unavailable").c_str());
    if ((present_[0] && !values_[0].value("ready", false)) ||
        (present_[1] && !values_[1].value("ready", false)))
        ImGui::TextWrapped(
            "%s",
            cyui::tr(
                "Unknown curve fields remain explicit. Weighted segments and extrapolation are unavailable.")
                .c_str());
    if (!selected_.empty()) {
        if (comparing_ && !selected_known())
            cyui::textDim("%s",
                          cyui::tr("Unknown opposite keys; absence is not a confirmed change.").c_str());
        ImGui::BeginChild("##curve-detail", {0, std::max(cyui::dp(100), ImGui::GetContentRegionAvail().y)},
                          ImGuiChildFlags_Borders);
        details_.draw("Curve key details");
        ImGui::EndChild();
    }
}
} // namespace cyuediff
