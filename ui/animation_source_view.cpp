#include <cyuediff/animation_source_view.hpp>
#include "../src/animation_internal.hpp"
#include "../src/animation_transform.hpp"
#include "diff_style.hpp"
#include <algorithm>
#include <cmath>
namespace cyuediff {
namespace {
const char *labels[] = {"Position X", "Position Y", "Position Z", "Rotation X", "Rotation Y",
                        "Rotation Z", "Rotation W", "Scale X",    "Scale Y",    "Scale Z"};
const char *channel(int component) {
    return component < 3 ? "positions" : component < 7 ? "rotations" : "scales";
}
unsigned axis(int component) {
    return unsigned(component < 3 ? component : component < 7 ? component - 3 : component - 7);
}
std::uint32_t keys(const Json *m) {
    return m && m->contains("numberOfKeys") ? m->at("numberOfKeys").get<std::uint32_t>() : 0;
}
} // namespace
void AnimationSourceView::register_translations() {
    cyui::registerTranslations(
        "fr",
        {{"Independent source frames", "Frames source indépendantes"},
         {"Source frame before", "Frame source avant"},
         {"Source frame after", "Frame source après"},
         {"Position tolerance", "Tolérance de position"},
         {"Rotation tolerance (degrees)", "Tolérance angulaire (degrés)"},
         {"Scale tolerance", "Tolérance d’échelle"},
         {"Selected local frame comparison", "Comparaison locale de la piste sélectionnée"},
         {"Local transform within tolerance", "Transformation locale dans la tolérance"},
         {"Local transform changed", "Transformation locale modifiée"},
         {"Local transform comparison unavailable", "Comparaison de transformation locale indisponible"},
         {"Position difference: %.7g | Rotation: %.7g deg | Scale: %.7g",
          "Écart de position : %.7g | Rotation : %.7g degrés | Échelle : %.7g"},
         {"Frame metrics are independent of stored key differences.",
          "Les métriques de frame sont distinctes des différences de clés enregistrées."},
         {"Before: orange | After: green | Click to choose the before frame.",
          "Avant : orange | Après : vert | Cliquez pour choisir la frame avant."},
         {"Stored key status", "État des clés enregistrées"}});
    cyui::registerTranslations(
        "fr", {{"Animation source", "Source d’animation"},
               {"Animations", "Animations"},
               {"Stored bone-local source keys; runtime animation is not evaluated.",
                "Clés source locales aux os ; l’animation à l’exécution n’est pas évaluée."},
               {"Animation model before", "Modèle d’animation avant"},
               {"Animation model after", "Modèle d’animation après"},
               {"Animation model", "Modèle d’animation"},
               {"Source frame", "Frame source"},
               {"Source key values", "Valeurs des clés source"},
               {"Find bone tracks", "Rechercher des pistes d’os"},
               {"No stored animation model", "Aucun modèle d’animation enregistré"},
               {"Some stored fields are unavailable. Omitted defaults remain unknown.",
                "Certains champs sont indisponibles. Les valeurs omises restent inconnues."},
               {"Select a bone track to inspect its source keys.",
                "Sélectionnez une piste d’os pour consulter ses clés source."},
               {"No matching bone track", "Aucune piste d’os correspondante"},
               {"Track list is limited to 4,096 entries.", "La liste est limitée à 4 096 pistes."},
               {"Source frame details", "Détails de la frame source"},
               {"No channel values available", "Aucune valeur disponible pour ce canal"},
               {"Before: orange | After: green | Click the curve to choose a source frame.",
                "Avant : orange | Après : vert | Cliquez sur la courbe pour choisir une frame source."},
               {"Position X", "Position X"},
               {"Position Y", "Position Y"},
               {"Position Z", "Position Z"},
               {"Rotation X", "Rotation X"},
               {"Rotation Y", "Rotation Y"},
               {"Rotation Z", "Rotation Z"},
               {"Rotation W", "Rotation W"},
               {"Scale X", "Échelle X"},
               {"Scale Y", "Échelle Y"},
               {"Scale Z", "Échelle Z"},
               {"Unavailable selected model", "Modèle sélectionné indisponible"},
               {"Unknown opposite track table; absence is not a confirmed change.",
                "Table des pistes opposée inconnue ; l’absence ne confirme aucun changement."}});
}
const Json *AnimationSourceView::model(bool after) const {
    const auto &r = roots_[after ? 1 : 0];
    if (!r.is_object())
        return nullptr;
    const auto it = r.find(objects_[after ? 1 : 0]);
    return it == r.end() ? nullptr : &*it;
}
const Json *AnimationSourceView::track(bool after) const {
    const auto *m = model(after);
    if (!m)
        return nullptr;
    const auto it = m->at("tracks").find(selected_);
    return it == m->at("tracks").end() ? nullptr : &*it;
}
bool AnimationSourceView::empty() const { return roots_[0].empty() && roots_[1].empty(); }
void AnimationSourceView::set_packages(const Package *a, const Package *b, bool comparing) {
    comparing_ = comparing || (a && b);
    frame_ = after_frame_ = 0;
    independent_frames_ = false;
    position_tolerance_ = rotation_tolerance_ = scale_tolerance_ = 0;
    frame_difference_ = Json::object();
    component_ = 0;
    filter_ = {};
    changed_only_ = false;
    selected_.clear();
    for (unsigned side = 0; side < 2; ++side) {
        const auto *p = side ? b : a;
        present_[side] = p;
        input_complete_[side] = p && p->complete_input;
        if (p) {
            AnimationOptions o;
            o.source_indices = false;
            roots_[side] = animation_snapshot(*p, o).at("models");
        } else
            roots_[side] = Json::object();
        objects_[side] = roots_[side].empty() ? "" : roots_[side].begin().key();
    }
    bind_models();
}
bool AnimationSourceView::select_models(std::string a, std::string b) {
    objects_[0] = std::move(a);
    objects_[1] = std::move(b);
    bind_models();
    return (!present_[0] || model(false)) && (!present_[1] || model(true));
}
void AnimationSourceView::bind_models() {
    entries_.clear();
    truncated_ = false;
    for (bool side : {false, true})
        if (const auto *m = model(side))
            for (auto it = m->at("tracks").begin(); it != m->at("tracks").end(); ++it) {
                if (!entries_.count(it.key()) && entries_.size() >= 4096) {
                    truncated_ = true;
                    continue;
                }
                entries_[it.key()].name = it->value("name", it.key());
            }
    bool identities[2]{};
    for (bool side : {false, true})
        if (const auto *m = model(side))
            identities[side] = detail::animation_track_identities_known(*m);
    auto known = [&](bool side, const Json *t) {
        const auto *m = model(side);
        return !present_[side] || (input_complete_[side] && m && (t || identities[side]));
    };
    for (auto &e : entries_) {
        const auto key = selected_;
        selected_ = e.first;
        const auto *a = track(false), *b = track(true);
        selected_ = key;
        e.second.known = (!comparing_ || (known(false, a) && known(true, b))) &&
                         (!a || a->value("complete", false)) && (!b || b->value("complete", false));
        e.second.change = !comparing_ || !e.second.known ? ChangeKind::unchanged : classify_change(a, b);
    }
    rebuild();
    prepare();
}
void AnimationSourceView::rebuild() {
    visible_.clear();
    for (const auto &e : entries_)
        if (filter_.matches(e.second.name, e.first) &&
            (!changed_only_ || !e.second.known || e.second.change != ChangeKind::unchanged))
            visible_.push_back(e.first);
    if (std::find(visible_.begin(), visible_.end(), selected_) == visible_.end())
        selected_ = visible_.empty() ? "" : visible_[0];
}
bool AnimationSourceView::select_track(const std::string &key) {
    if (!key.empty() && std::find(visible_.begin(), visible_.end(), key) == visible_.end())
        return false;
    selected_ = key;
    prepare();
    return true;
}
void AnimationSourceView::set_filter(std::string q) {
    filter_.query = std::move(q);
    rebuild();
    prepare();
}
void AnimationSourceView::set_changed_only(bool v) {
    changed_only_ = comparing_ && v;
    rebuild();
    prepare();
}
void AnimationSourceView::set_frame(std::uint32_t frame) {
    if (frame > INT32_MAX)
        throw Error("view-state", "Source frame exceeds the supported display range");
    frame_ = frame;
    if (!independent_frames_)
        after_frame_ = frame_;
    sample();
}
void AnimationSourceView::set_after_frame(std::uint32_t frame) {
    if (frame > INT32_MAX)
        throw Error("view-state", "Source frame exceeds the supported display range");
    after_frame_ = frame;
    independent_frames_ = true;
    sample();
}
void AnimationSourceView::set_independent_frames(bool value) {
    independent_frames_ = value;
    if (!value)
        after_frame_ = frame_;
    sample();
}
void AnimationSourceView::set_tolerances(double position, double rotation_degrees, double scale) {
    detail::validate_animation_tolerances(position, rotation_degrees, scale);
    position_tolerance_ = position;
    rotation_tolerance_ = rotation_degrees;
    scale_tolerance_ = scale;
    sample();
}
void AnimationSourceView::set_component(unsigned c) {
    if (c < 10) {
        component_ = int(c);
        prepare();
    }
}
bool AnimationSourceView::selected_known() const {
    const auto it = entries_.find(selected_);
    return it != entries_.end() && it->second.known;
}
bool AnimationSourceView::next_change(bool previous) {
    std::vector<std::string> changed;
    for (const auto &key : visible_)
        if (entries_.at(key).known && entries_.at(key).change != ChangeKind::unchanged)
            changed.push_back(key);
    if (changed.empty())
        return false;
    auto it = std::find(changed.begin(), changed.end(), selected_);
    auto index =
        it == changed.end()
            ? (previous ? changed.size() - 1 : 0)
            : (std::size_t(it - changed.begin()) + changed.size() + (previous ? -1 : 1)) % changed.size();
    return select_track(changed[index]);
}
void AnimationSourceView::prepare() {
    for (bool side : {false, true}) {
        auto &out = curves_[side ? 1 : 0];
        out.clear();
        const auto *t = track(side), *m = model(side);
        const auto n = keys(m);
        if (!t || !n || !t->contains(channel(component_)))
            continue;
        const auto &array = t->at(channel(component_));
        if (array.size() != 1 && array.size() != n)
            continue;
        const auto size = std::min<std::uint32_t>(n, 2048);
        for (std::uint32_t j = 0; j < size; ++j) {
            const auto index = size <= 1 ? 0u : std::uint32_t(std::uint64_t(j) * (n - 1) / (size - 1));
            out.push_back(
                {double(index), array[array.size() == 1 ? 0 : index][axis(component_)].get<double>()});
        }
    }
    sample();
}
void AnimationSourceView::sample() {
    for (bool side : {false, true}) {
        const auto *m = model(side), *t = track(side);
        const auto count = keys(m);
        const auto frame = side && independent_frames_ ? after_frame_ : frame_;
        Json out = {{"frame", frame}, {"numberOfKeys", count}, {"ready", false}};
        if (t && count && frame < count) {
            out = detail::sample_animation_track(*t, frame, count);
            out["frame"] = frame;
            out["numberOfKeys"] = count;
            if (m->contains("frameRate")) {
                const auto &rate = m->at("frameRate");
                out["frameRate"] = rate;
                if (rate.contains("numerator") && rate.contains("denominator"))
                    out["timeSeconds"] =
                        double(frame) * rate["denominator"].get<double>() / rate["numerator"].get<double>();
            }
            for (const char *name : {"positions", "rotations", "scales"})
                if (t->contains(name))
                    out["storedKeyCounts"][name] = t->at(name).size();
            if (!input_complete_[side])
                out["ready"] = false;
        } else
            out["diagnostics"] = Json::array({"Selected source frame or track is unavailable"});
        samples_[side ? 1 : 0] = std::move(out);
    }
    frame_difference_ =
        comparing_ ? detail::compare_local_animation_values(samples_[0], samples_[1], position_tolerance_,
                                                            rotation_tolerance_, scale_tolerance_)
                   : Json::object();
    const auto *a = present_[0] ? &samples_[0] : nullptr, *b = present_[1] ? &samples_[1] : nullptr;
    if (comparing_ && !selected_known())
        details_.set_values(a && (samples_[0].value("ready", false) || !b) ? a : b, nullptr, false);
    else
        details_.set_values(a, b, comparing_);
}
Json AnimationSourceView::view_state() const {
    Json out = {{"beforeObject", objects_[0]},
                {"afterObject", objects_[1]},
                {"frame", frame_},
                {"afterFrame", independent_frames_ ? after_frame_ : frame_},
                {"independentFrames", independent_frames_},
                {"positionTolerance", position_tolerance_},
                {"rotationToleranceDegrees", rotation_tolerance_},
                {"scaleTolerance", scale_tolerance_},
                {"component", component_},
                {"inspection",
                 {{"filter", filter_.query},
                  {"changedOnly", changed_only_},
                  {"selected", selected_},
                  {"properties", details_.view_state()}}}};
    return out;
}
void AnimationSourceView::restore_view_state(const Json &v) {
    view_state::validate(v, "animation-source");
    if (v.contains("beforeObject"))
        objects_[0] = v["beforeObject"].get<std::string>();
    if (v.contains("afterObject"))
        objects_[1] = v["afterObject"].get<std::string>();
    if (v.contains("frame"))
        frame_ = v["frame"].get<std::uint32_t>();
    independent_frames_ = v.value("independentFrames", independent_frames_);
    after_frame_ = independent_frames_ ? v.value("afterFrame", frame_) : frame_;
    position_tolerance_ = v.value("positionTolerance", position_tolerance_);
    rotation_tolerance_ = v.value("rotationToleranceDegrees", rotation_tolerance_);
    scale_tolerance_ = v.value("scaleTolerance", scale_tolerance_);
    if (v.contains("component"))
        component_ = v["component"].get<int>();
    if (v.contains("inspection")) {
        const auto &i = v["inspection"];
        filter_.query = i.value("filter", std::string());
        changed_only_ = comparing_ && i.value("changedOnly", false);
        selected_ = i.value("selected", std::string());
    }
    bind_models();
    if (v.contains("inspection") && v["inspection"].contains("properties"))
        details_.restore_view_state(v["inspection"]["properties"]);
}
void AnimationSourceView::chart() {
    if (curves_[0].empty() && curves_[1].empty()) {
        cyui::textDim("%s", cyui::tr("No channel values available").c_str());
        return;
    }
    double low = 0, high = 0;
    bool first = true;
    for (const auto &curve : curves_)
        for (const auto &p : curve) {
            if (first) {
                low = high = p[1];
                first = false;
            } else {
                low = std::min(low, p[1]);
                high = std::max(high, p[1]);
            }
        }
    const auto count = std::max(keys(model(false)), keys(model(true)));
    const double end = std::max(1u, count ? count - 1 : 0),
                 scale = std::max({std::abs(low), std::abs(high), 1e-30});
    auto lo = low / scale, hi = high / scale;
    if (hi - lo < 1e-12) {
        lo -= .5;
        hi += .5;
    }
    ImGui::Text("%.6g .. %.6g", low, high);
    const auto a = ImGui::GetCursorScreenPos();
    const ImVec2 size{std::max(1.f, ImGui::GetContentRegionAvail().x), cyui::dp(120)};
    ImGui::InvisibleButton("##source-curve", size);
    auto *draw = ImGui::GetWindowDrawList();
    const auto &palette = cyui::palette();
    const ImVec2 b{a.x + size.x, a.y + size.y};
    draw->AddRect(a, b, palette.textDim);
    draw->PushClipRect(a, b, true);
    for (unsigned side = 0; side < 2; ++side) {
        const auto &curve = curves_[side];
        const auto color = side ? palette.success : palette.warning;
        auto point = [&](const std::array<double, 2> &p) {
            return ImVec2{a.x + float(p[0] / end) * size.x,
                          a.y + size.y - float((p[1] / scale - lo) / (hi - lo)) * size.y};
        };
        for (std::size_t j = 1; j < curve.size(); ++j)
            draw->AddLine(point(curve[j - 1]), point(curve[j]), color, cyui::dp(2));
        if (curve.size() == 1)
            draw->AddCircleFilled(point(curve[0]), cyui::dp(3), color);
    }
    for (unsigned side = 0; side < (comparing_ && independent_frames_ ? 2u : 1u); ++side) {
        const auto frame = side ? after_frame_ : frame_;
        if (frame <= end) {
            const auto x = a.x + float(frame / end) * size.x;
            draw->AddLine({x, a.y}, {x, b.y},
                          independent_frames_ ? (side ? palette.success : palette.warning) : palette.text);
        }
    }
    draw->PopClipRect();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const double ratio = std::clamp(double((ImGui::GetIO().MousePos.x - a.x) / size.x), 0., 1.);
        set_frame(std::uint32_t(std::llround(ratio * (count ? count - 1 : 0))));
    }
    cyui::textDim("%s",
                  cyui::tr(independent_frames_
                               ? "Before: orange | After: green | Click to choose the before frame."
                               : "Before: orange | After: green | Click the curve to choose a source frame.")
                      .c_str());
}
void AnimationSourceView::draw(const char *id) {
    cyui::ScopedID scope(id);
    if (empty()) {
        cyui::textDim("%s", cyui::tr("No stored animation model").c_str());
        return;
    }
    ImGui::TextWrapped(
        "%s", cyui::tr("Stored bone-local source keys; runtime animation is not evaluated.").c_str());
    for (unsigned side = 0; side < 2; ++side)
        if (present_[side]) {
            const char *label =
                comparing_ ? (side ? "Animation model after" : "Animation model before") : "Animation model";
            ImGui::SetNextItemWidth(std::min(cyui::dp(420), ImGui::GetContentRegionAvail().x));
            std::string choice;
            bool update = false;
            if (ImGui::BeginCombo(cyui::trLabel(label).c_str(), objects_[side].c_str())) {
                for (auto it = roots_[side].begin(); it != roots_[side].end(); ++it)
                    if (ImGui::Selectable(it.key().c_str(), objects_[side] == it.key())) {
                        choice = it.key();
                        update = true;
                    }
                ImGui::EndCombo();
            }
            if (update) {
                objects_[side] = std::move(choice);
                bind_models();
            }
            const auto *m = model(side);
            if (!m)
                cyui::textDim("%s", cyui::tr("Unavailable selected model").c_str());
            else if (!m->at("complete").get<bool>())
                cyui::textDim(
                    "%s",
                    cyui::tr("Some stored fields are unavailable. Omitted defaults remain unknown.").c_str());
        }
    cyui::FilterOptions options;
    options.hint = "Find bone tracks";
    options.width = std::min(cyui::dp(260), ImGui::GetContentRegionAvail().x);
    bool update = cyui::filterBox("##source-filter", filter_, options);
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
        prepare();
    }
    if (truncated_)
        cyui::textDim("%s", cyui::tr("Track list is limited to 4,096 entries.").c_str());
    const auto height =
        selected_.empty() ? std::max(cyui::dp(120), ImGui::GetContentRegionAvail().y) : cyui::dp(120);
    ImGui::BeginChild("##source-tracks", {0, height}, ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##tracks", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                          {0, ImGui::GetContentRegionAvail().y})) {
        ImGui::TableSetupColumn(cyui::tr("Bone").c_str(), ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn(cyui::tr("Stored key status").c_str(), ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(int(visible_.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const auto &key = visible_[i], &name = entries_.at(key).name;
                const auto &e = entries_.at(key);
                cyui::ScopedID row(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      e.known ? detail::change_color(e.change) : cyui::palette().textDim);
                if (ImGui::Selectable("##track", selected_ == key, ImGuiSelectableFlags_SpanAllColumns))
                    select_track(key);
                ImGui::SameLine();
                ImGui::TextUnformatted(name.c_str());
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cyui::tr(!e.known     ? "Unavailable"
                                                : comparing_ ? change_name(e.change)
                                                             : "Bone")
                                           .c_str());
            }
        ImGui::EndTable();
    }
    if (visible_.empty())
        cyui::textDim("%s", cyui::tr("No matching bone track").c_str());
    ImGui::EndChild();
    if (selected_.empty())
        return;
    ImGui::Text("%s: %s", cyui::tr("Bone").c_str(), entries_.at(selected_).name.c_str());
    int frame = int(std::min<std::uint32_t>(frame_, INT32_MAX));
    const auto n = independent_frames_ ? keys(model(false)) : std::max(keys(model(false)), keys(model(true)));
    ImGui::SetNextItemWidth(std::max(cyui::dp(80), ImGui::GetContentRegionAvail().x * .65f));
    if (ImGui::SliderInt(
            cyui::trLabel(comparing_ && independent_frames_ ? "Source frame before" : "Source frame").c_str(),
            &frame, 0, int(n ? n - 1 : 0)))
        set_frame(std::uint32_t(frame));
    if (comparing_) {
        bool independent = independent_frames_;
        if (ImGui::Checkbox(cyui::trLabel("Independent source frames").c_str(), &independent))
            set_independent_frames(independent);
        if (independent_frames_) {
            int after = int(after_frame_);
            const auto count = keys(model(true));
            ImGui::SetNextItemWidth(std::max(cyui::dp(80), ImGui::GetContentRegionAvail().x * .65f));
            if (ImGui::SliderInt(cyui::trLabel("Source frame after").c_str(), &after, 0,
                                 int(count ? count - 1 : 0)))
                set_after_frame(std::uint32_t(after));
        }
        double pt = position_tolerance_, rt = rotation_tolerance_, st = scale_tolerance_;
        bool changed = false;
        ImGui::SetNextItemWidth(std::min(cyui::dp(140), ImGui::GetContentRegionAvail().x));
        changed =
            ImGui::InputDouble(cyui::trLabel("Position tolerance").c_str(), &pt, 0, 0, "%.6g") || changed;
        if (ImGui::GetContentRegionAvail().x > cyui::dp(900))
            ImGui::SameLine();
        ImGui::SetNextItemWidth(std::min(cyui::dp(140), ImGui::GetContentRegionAvail().x));
        changed =
            ImGui::InputDouble(cyui::trLabel("Rotation tolerance (degrees)").c_str(), &rt, 0, 0, "%.6g") ||
            changed;
        if (ImGui::GetContentRegionAvail().x > cyui::dp(900))
            ImGui::SameLine();
        ImGui::SetNextItemWidth(std::min(cyui::dp(140), ImGui::GetContentRegionAvail().x));
        changed = ImGui::InputDouble(cyui::trLabel("Scale tolerance").c_str(), &st, 0, 0, "%.6g") || changed;
        if (changed && std::isfinite(pt) && pt >= 0 && std::isfinite(rt) && rt >= 0 && rt <= 180 &&
            std::isfinite(st) && st >= 0)
            set_tolerances(pt, rt, st);
    }
    ImGui::SetNextItemWidth(std::min(cyui::dp(220), ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo(cyui::trLabel("Source key values").c_str(), cyui::tr(labels[component_]).c_str())) {
        for (int c = 0; c < 10; ++c)
            if (ImGui::Selectable(cyui::tr(labels[c]).c_str(), component_ == c))
                set_component(unsigned(c));
        ImGui::EndCombo();
    }
    chart();
    if (comparing_) {
        ImGui::TextUnformatted(cyui::tr("Selected local frame comparison").c_str());
        if (frame_difference_.value("ready", false)) {
            ImGui::TextUnformatted(cyui::tr(frame_difference_["changed"].get<bool>()
                                                ? "Local transform changed"
                                                : "Local transform within tolerance")
                                       .c_str());
            ImGui::TextWrapped(
                cyui::tr("Position difference: %.7g | Rotation: %.7g deg | Scale: %.7g").c_str(),
                frame_difference_["positionDistance"].get<double>(),
                frame_difference_["rotationDeltaDegrees"].get<double>(),
                frame_difference_["scaleMaxDelta"].get<double>());
        } else
            cyui::textDim("%s", cyui::tr("Local transform comparison unavailable").c_str());
        ImGui::TextWrapped("%s",
                           cyui::tr("Frame metrics are independent of stored key differences.").c_str());
    }
    if (comparing_ && !selected_known())
        cyui::textDim("%s",
                      cyui::tr("Unknown opposite track table; absence is not a confirmed change.").c_str());
    ImGui::BeginChild("##source-details", {0, std::max(cyui::dp(100), ImGui::GetContentRegionAvail().y)},
                      ImGuiChildFlags_Borders);
    details_.draw("Source frame details");
    ImGui::EndChild();
}
} // namespace cyuediff
