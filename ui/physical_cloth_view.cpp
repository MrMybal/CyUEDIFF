#include <cyuediff/physical_cloth_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
namespace cyuediff {
namespace {
constexpr std::size_t max_work = 1'000'000;
void intersect(Json &a, Json &b) {
    if (!a.is_object() || !b.is_object())
        return;
    for (auto it = a.begin(); it != a.end();)
        if (!b.contains(it.key()))
            it = a.erase(it);
        else
            ++it;
    for (auto it = b.begin(); it != b.end();)
        if (!a.contains(it.key()))
            it = b.erase(it);
        else
            ++it;
}
void known_fields(Json &a, Json &b, bool complete_maps) {
    if (!a.is_object() || !b.is_object())
        return;
    if (!complete_maps && a.contains("weightMaps") && b.contains("weightMaps"))
        intersect(a["weightMaps"], b["weightMaps"]);
    // Missing optional data is unknown. This never mutates the stored inspection rows.
    intersect(a, b);
}
} // namespace
void PhysicalClothView::register_translations() {
    cyui::registerTranslations(
        "fr",
        {{"Physical cloth", "Tissu physique"},
         {"Physical cloth controls", "Réglages du tissu physique"},
         {"Before physical LOD", "LOD physique avant"},
         {"After physical LOD", "LOD physique après"},
         {"Stored physical mesh", "Maillage physique stocké"},
         {"Transition from higher detail", "Transition depuis le LOD plus détaillé"},
         {"Transition from lower detail", "Transition depuis le LOD moins détaillé"},
         {"Stored geometry and reference-pose correspondences; no cloth simulation.",
          "Géométrie stockée et correspondances de pose de référence ; sans simulation de tissu."},
         {"Unknown defaults and unavailable peers do not prove removals.",
          "Les valeurs par défaut inconnues et les entrées indisponibles ne prouvent pas de suppressions."},
         {"Physical vertex", "Sommet physique"},
         {"Find physical vertex", "Rechercher un sommet physique"},
         {"Physical cloth details", "Détails du tissu physique"},
         {"Physical cloth geometry", "Géométrie du tissu physique"},
         {"Stored cloth configuration", "Configuration de tissu stockée"},
         {"Physical scope unavailable", "Données physiques sélectionnées indisponibles"},
         {"Physical comparison unavailable", "Comparaison physique indisponible"},
         {"No matching physical vertex", "Aucun sommet physique correspondant"},
         {"Previous changed physical vertex", "Sommet physique modifié précédent"},
         {"Next changed physical vertex", "Sommet physique modifié suivant"}});
}
void PhysicalClothView::set_assets(std::shared_ptr<const PhysicalClothAsset> a,
                                   std::shared_ptr<const PhysicalClothAsset> b, bool comparing,
                                   bool explicit_pair) {
    before_ = {};
    after_ = {};
    before_.asset = std::move(a);
    after_.asset = std::move(b);
    comparing_ = comparing || bool(before_.asset && after_.asset);
    explicit_pair_ = explicit_pair;
    for (auto *s : {&before_, &after_})
        if (s->asset && s->asset->available() && !s->asset->lods().empty())
            s->lod = 0;
    selected_.reset();
    filter_ = {};
    changed_only_ = false;
    mode_ = panel_ = 0;
    controls_open_ = false;
    after_side_ = !before_.lod && bool(after_.lod);
    rebuild();
}
void PhysicalClothView::prepare(Side &s) {
    s.vertices.clear();
    s.geometry.reset();
    s.wrapped.clear();
    s.summary = nullptr;
    if (!s.asset || !s.asset->available())
        return;
    s.summary = s.asset->to_json(false);
    if (!s.lod || *s.lod >= s.asset->lods().size())
        return;
    const auto &l = s.asset->lods()[*s.lod];
    std::size_t work = 0;
    const auto consume = [&](std::size_t n) {
        if (n > max_work - work)
            throw Error("physical-cloth-view-limit", "Physical view preparation exceeds its record budget");
        work += n;
    };
    consume(l.vertices.size());
    consume(l.up.records.size());
    consume(l.down.records.size());
    for (const auto &m : l.weight_maps)
        consume(m.second.size());
    for (const auto *batches : {&l.euclidean_tethers, &l.geodesic_tethers})
        for (const auto &batch : *batches)
            consume(batch.size());
    const auto &t = mode_ == 1 ? l.up : l.down;
    if (mode_ && !t.records.empty() && t.wrap_available)
        s.wrapped = s.asset->wrap_transition(*s.lod, mode_ == 1);
    const auto count = mode_ ? t.records.size() : l.vertices.size();
    s.vertices.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Json row;
        if (!mode_) {
            row = s.asset->vertex(*s.lod, i);
            row["euclideanTethers"] = Json::object();
            row["geodesicTethers"] = Json::object();
        } else {
            row = s.asset->transition(*s.lod, mode_ == 1, i);
            for (const char *key : {"sourceLOD", "sourceBoundsVerified", "wrapAvailable", "padding"})
                row.erase(key);
        }
        s.vertices.push_back(std::move(row));
    }
    if (!mode_)
        for (const auto &type : {std::make_pair("euclideanTethers", &l.euclidean_tethers),
                                 std::make_pair("geodesicTethers", &l.geodesic_tethers)})
            for (std::size_t b = 0; b < type.second->size(); ++b)
                for (const auto &tether : (*type.second)[b])
                    s.vertices.at(tether.particle)[type.first][std::to_string(b)] = {
                        {"anchor", tether.anchor}, {"length", tether.length}};
    s.geometry = l.geometry;
    if (mode_) {
        s.geometry.reset();
        if (!s.wrapped.empty() && l.geometry && s.wrapped.size() == l.vertices.size()) {
            auto g = std::make_shared<MeshGeometry>(*l.geometry);
            for (std::size_t i = 0; i < g->vertices.size(); ++i)
                for (unsigned k = 0; k < 3; ++k)
                    g->vertices[i].position[k] = s.wrapped[i][k];
            // This preview transports positions only; target normals/bounds are not the wrapped result.
            g->bounds = Json::object();
            g->attributes["scope"] = "reference-pose-physical-cloth-transition";
            g->attributes["normalsAvailable"] = false;
            for (auto &instance : g->instances)
                instance.normal = {};
            s.geometry = std::move(g);
        }
    }
}
void PhysicalClothView::rebuild() {
    diagnostics_.clear();
    for (auto *side : {&before_, &after_}) {
        try {
            prepare(*side);
        } catch (const Error &e) {
            side->vertices.clear();
            side->geometry.reset();
            side->wrapped.clear();
            side->summary = nullptr;
            diagnostics_.push_back(std::string(side == &before_ ? "Before: " : "After: ") + e.code + ": " +
                                   e.what());
        }
    }
    rows_.clear();
    const auto valid = [](const Side &s) {
        return s.asset && s.asset->available() && s.lod && *s.lod < s.asset->lods().size();
    };
    known_ = diagnostics_.empty() && comparing_ && valid(before_) && valid(after_) &&
             (explicit_pair_ || before_.asset->object_path() == after_.asset->object_path());
    if (mode_ && (before_.vertices.empty() || after_.vertices.empty()))
        known_ = false;
    complete_maps_ = known_ && before_.asset->lods()[*before_.lod].weight_maps_replaced &&
                     after_.asset->lods()[*after_.lod].weight_maps_replaced;
    for (std::size_t i = 0; i < std::max(before_.vertices.size(), after_.vertices.size()); ++i) {
        const bool a = i < before_.vertices.size(), b = i < after_.vertices.size();
        auto c = ChangeKind::unchanged;
        if (known_) {
            if (!a)
                c = ChangeKind::added;
            else if (!b)
                c = ChangeKind::removed;
            else {
                auto x = before_.vertices[i], y = after_.vertices[i];
                known_fields(x, y, complete_maps_);
                if (x != y)
                    c = ChangeKind::modified;
            }
        }
        rows_.push_back({static_cast<std::uint32_t>(i), c});
    }
    filter();
    bind_mesh();
}
void PhysicalClothView::filter() {
    visible_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if ((!changed_only_ || !known_ || rows_[i].change != ChangeKind::unchanged) &&
            filter_.matches(std::to_string(rows_[i].vertex)))
            visible_.push_back(i);
    if (selected_ &&
        std::none_of(visible_.begin(), visible_.end(), [&](auto i) { return rows_[i].vertex == *selected_; }))
        selected_.reset();
    bind_selection();
}
void PhysicalClothView::bind_selection() {
    const auto value = [&](const Side &s) -> Json {
        return selected_ && *selected_ < s.vertices.size() ? s.vertices[*selected_] : Json(nullptr);
    };
    auto a = value(before_), b = value(after_);
    if (known_) {
        known_fields(a, b, complete_maps_);
        details_.set_values(a.is_null() ? nullptr : &a, b.is_null() ? nullptr : &b, true);
    } else {
        auto &v = after_side_ ? b : a;
        details_.set_values(v.is_null() ? nullptr : &v, nullptr, false);
    }
    const auto &s = after_side_ ? after_ : before_;
    summary_.set_values(s.summary.is_null() ? nullptr : &s.summary, nullptr, false);
}
void PhysicalClothView::bind_mesh() {
    const auto &own = after_side_ ? after_ : before_, &peer = after_side_ ? before_ : after_;
    mesh_.set_mesh(own.geometry, known_ ? peer.geometry : nullptr, after_side_, known_);
}
bool PhysicalClothView::select_lod(bool after, std::uint32_t lod) {
    auto &s = after ? after_ : before_;
    if (!s.asset || !s.asset->available() || lod >= s.asset->lods().size())
        return false;
    s.lod = lod;
    selected_.reset();
    rebuild();
    return true;
}
bool PhysicalClothView::select_mode(int m) {
    if (m < 0 || m > 2)
        return false;
    mode_ = m;
    selected_.reset();
    rebuild();
    return true;
}
bool PhysicalClothView::select_vertex(std::uint32_t id) {
    if (std::none_of(visible_.begin(), visible_.end(), [&](auto i) { return rows_[i].vertex == id; }))
        return false;
    selected_ = id;
    focus_ = true;
    bind_selection();
    return true;
}
void PhysicalClothView::set_filter(std::string s) {
    if (s.size() > 4096)
        throw Error("physical-cloth-view-limit", "Physical filter exceeds text budget");
    filter_.query = std::move(s);
    filter();
}
void PhysicalClothView::set_changed_only(bool b) {
    changed_only_ = comparing_ && b;
    filter();
}
void PhysicalClothView::set_after_side(bool b) {
    after_side_ = b;
    bind_selection();
    bind_mesh();
}
bool PhysicalClothView::next_change(bool previous) {
    if (!known_ || visible_.empty())
        return false;
    auto it = std::find_if(visible_.begin(), visible_.end(),
                           [&](auto i) { return selected_ && rows_[i].vertex == *selected_; });
    const auto at =
        it == visible_.end() ? (previous ? 0 : visible_.size() - 1) : std::size_t(it - visible_.begin());
    for (std::size_t i = 1; i <= visible_.size(); ++i) {
        auto n = previous ? (at + visible_.size() - i) % visible_.size() : (at + i) % visible_.size();
        if (rows_[visible_[n]].change != ChangeKind::unchanged)
            return select_vertex(rows_[visible_[n]].vertex);
    }
    return false;
}
Json PhysicalClothView::report() const {
    std::size_t modified = 0, added = 0, removed = 0;
    for (const auto &r : rows_) {
        modified += r.change == ChangeKind::modified;
        added += r.change == ChangeKind::added;
        removed += r.change == ChangeKind::removed;
    }
    const auto wrapped = [&](const Side &s) -> Json {
        return selected_ && *selected_ < s.wrapped.size() ? Json(s.wrapped[*selected_]) : Json(nullptr);
    };
    return {{"schema", "cyuediff.physical-cloth-view/1"},
            {"comparing", comparing_},
            {"knownComparison", known_},
            {"completeWeightMapInventory", complete_maps_},
            {"explicitObjectPair", explicit_pair_},
            {"beforeObject", before_.asset ? Json(before_.asset->object_path()) : Json(nullptr)},
            {"afterObject", after_.asset ? Json(after_.asset->object_path()) : Json(nullptr)},
            {"beforeLOD", before_.lod ? Json(*before_.lod) : Json(nullptr)},
            {"afterLOD", after_.lod ? Json(*after_.lod) : Json(nullptr)},
            {"mode", mode_},
            {"vertices", rows_.size()},
            {"visible", visible_.size()},
            {"rendered", rendered_},
            {"compact", compact_},
            {"detailsHeight", details_height_},
            {"selected", selected_ ? Json(*selected_) : Json(nullptr)},
            {"selectedVisible", selected_visible_},
            {"modified", modified},
            {"added", added},
            {"removed", removed},
            {"detailRows", details_.table().rows.size()},
            {"beforeWrappedPosition", wrapped(before_)},
            {"afterWrappedPosition", wrapped(after_)},
            {"geometryAvailable", bool((after_side_ ? after_ : before_).geometry)},
            {"diagnostics", diagnostics_},
            {"defaultsResolved", false},
            {"simulationEvaluated", false}};
}
Json PhysicalClothView::view_state() const {
    return {{"beforeLOD", before_.lod ? Json(*before_.lod) : Json(nullptr)},
            {"afterLOD", after_.lod ? Json(*after_.lod) : Json(nullptr)},
            {"mode", mode_},
            {"panel", panel_},
            {"controlsOpen", controls_open_},
            {"after", after_side_},
            {"filter", filter_.query},
            {"changedOnly", changed_only_},
            {"selected", selected_ ? Json(*selected_) : Json(nullptr)},
            {"details", details_.view_state()},
            {"mesh", mesh_.view_state()}};
}
void PhysicalClothView::restore_view_state(const Json &s) {
    if (!s.is_object())
        return;
    const auto lod = [&](Side &side, const char *key) {
        if (s.contains(key) && s[key].is_number_integer() && s[key] >= 0 && s[key] <= UINT32_MAX)
            side.lod = s[key].get<std::uint32_t>();
        else if (s.contains(key) && s[key].is_null())
            side.lod.reset();
    };
    if (s.contains("controlsOpen") && s["controlsOpen"].is_boolean())
        controls_open_ = s["controlsOpen"].get<bool>();
    lod(before_, "beforeLOD");
    lod(after_, "afterLOD");
    if (s.contains("mode") && s["mode"].is_number_integer() && s["mode"] >= 0 && s["mode"] <= 2)
        mode_ = s["mode"].get<int>();
    if (s.contains("panel") && s["panel"].is_number_integer() && s["panel"] >= 0 && s["panel"] <= 2)
        panel_ = s["panel"].get<int>();
    if (s.contains("after") && s["after"].is_boolean())
        after_side_ = s["after"].get<bool>();
    if (s.contains("filter") && s["filter"].is_string() &&
        s["filter"].get_ref<const std::string &>().size() <= 4096)
        filter_.query = s["filter"].get<std::string>();
    if (s.contains("changedOnly") && s["changedOnly"].is_boolean())
        changed_only_ = comparing_ && s["changedOnly"].get<bool>();
    selected_.reset();
    rebuild();
    if (s.contains("selected") && s["selected"].is_number_integer() && s["selected"] >= 0 &&
        s["selected"] <= UINT32_MAX)
        select_vertex(s["selected"].get<std::uint32_t>());
    if (s.contains("details"))
        details_.restore_view_state(s["details"]);
    if (s.contains("mesh"))
        mesh_.restore_view_state(s["mesh"]);
}
void PhysicalClothView::draw(const char *id) {
    cyui::ScopedID scope(id);
    rendered_ = 0;
    selected_visible_ = false;
    details_height_ = 0;
    compact_ =
        ImGui::GetContentRegionAvail().x < cyui::dp(650) || ImGui::GetContentRegionAvail().y < cyui::dp(480);
    for (const auto &d : diagnostics_)
        ImGui::TextWrapped("%s", d.c_str());
    bool show_controls = true;
    if (compact_) {
        ImGui::SetNextItemOpen(controls_open_, ImGuiCond_Always);
        controls_open_ = ImGui::CollapsingHeader(cyui::trLabel("Physical cloth controls").c_str());
        show_controls = controls_open_;
    }
    if (show_controls) {
        ImGui::TextWrapped(
            "%s",
            cyui::tr("Stored geometry and reference-pose correspondences; no cloth simulation.").c_str());
        const auto choose = [&](Side &side, bool after) {
            if (!side.asset || !side.asset->available())
                return;
            const auto label = cyui::trLabel(after ? "After physical LOD" : "Before physical LOD");
            const auto selected = side.lod ? std::to_string(*side.lod) : cyui::tr("Unavailable");
            ImGui::SetNextItemWidth(cyui::dp(90));
            if (ImGui::BeginCombo(label.c_str(), selected.c_str())) {
                for (std::uint32_t i = 0; i < side.asset->lods().size(); ++i)
                    if (ImGui::Selectable(std::to_string(i).c_str(), side.lod && *side.lod == i))
                        select_lod(after, i);
                ImGui::EndCombo();
            }
        };
        choose(before_, false);
        choose(after_, true);
        const char *modes[] = {"Stored physical mesh", "Transition from higher detail",
                               "Transition from lower detail"};
        if (ImGui::BeginCombo("##physical-mode", cyui::tr(modes[mode_]).c_str())) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(cyui::tr(modes[i]).c_str(), mode_ == i))
                    select_mode(i);
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s",
                           cyui::tr("Unknown defaults and unavailable peers do not prove removals.").c_str());
        if (ImGui::Checkbox(cyui::trLabel("After").c_str(), &after_side_)) {
            bind_selection();
            bind_mesh();
        }
    }
    if (comparing_ && !known_)
        cyui::textDim(cyui::tr("Physical comparison unavailable").c_str());
    const char *panels[] = {"Physical cloth details", "Physical cloth geometry",
                            "Stored cloth configuration"};
    if (compact_) {
        if (ImGui::BeginCombo("##physical-panel", cyui::tr(panels[panel_]).c_str())) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(cyui::tr(panels[i]).c_str(), panel_ == i))
                    panel_ = i;
            ImGui::EndCombo();
        }
    } else
        for (int i = 0; i < 3; ++i) {
            if (i && ImGui::GetContentRegionAvail().x > cyui::dp(720))
                ImGui::SameLine();
            if (ImGui::RadioButton(cyui::trLabel(panels[i]).c_str(), panel_ == i))
                panel_ = i;
        }
    if (panel_ == 1) {
        if ((after_side_ ? after_ : before_).geometry)
            mesh_.draw("##physical-wire");
        else
            cyui::textDim(cyui::tr("Physical scope unavailable").c_str());
        return;
    }
    if (panel_ == 2) {
        summary_.draw("##physical-config");
        return;
    }
    cyui::FilterOptions options;
    options.hint = "Find physical vertex";
    options.width = std::min(cyui::dp(270), ImGui::GetContentRegionAvail().x);
    if (cyui::filterBox("##physical-filter", filter_, options))
        filter();
    if (known_) {
        if (ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_))
            filter();
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed physical vertex").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed physical vertex").c_str()))
            next_change();
    }
    const auto height = std::max(cyui::dp(65), ImGui::GetContentRegionAvail().y * (compact_ ? .25f : .34f));
    if (ImGui::BeginTable("##physical-list", 2,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                          {0, height})) {
        ImGui::TableSetupColumn(cyui::tr("Physical vertex").c_str());
        ImGui::TableSetupColumn(cyui::tr("Change").c_str());
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_.size()));
        if (focus_ && selected_)
            for (std::size_t i = 0; i < visible_.size(); ++i)
                if (rows_[visible_[i]].vertex == *selected_) {
                    clipper.IncludeItemByIndex(static_cast<int>(i));
                    break;
                }
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto &row = rows_[visible_[i]];
                const auto name = std::to_string(row.vertex);
                cyui::ScopedID item(name.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(name.c_str(), selected_ && *selected_ == row.vertex,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    select_vertex(row.vertex);
                ++rendered_;
                if (selected_ && *selected_ == row.vertex) {
                    selected_visible_ = ImGui::IsItemVisible();
                    if (focus_) {
                        ImGui::SetScrollHereY(.4f);
                        focus_ = false;
                    }
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::PushStyleColor(ImGuiCol_Text, detail::change_color(row.change));
                ImGui::TextUnformatted(cyui::tr(change_name(row.change)).c_str());
                ImGui::PopStyleColor();
            }
        ImGui::EndTable();
    }
    if (visible_.empty())
        cyui::textDim(cyui::tr("No matching physical vertex").c_str());
    details_height_ = std::max(0.f, ImGui::GetContentRegionAvail().y);
    details_.draw("##physical-details");
}
} // namespace cyuediff
