#include <cyuediff/asset_view.hpp>
#include "diff_style.hpp"
namespace cyuediff {
void AssetView::register_translations() {
    PropertyView::register_translations();
    cyui::registerTranslations(
        "fr",
        {{"Source diagnostics", "Diagnostics de la source"},
         {"HDR preview: Reinhard / sRGB at 0 EV. Differences use source floats.",
          "Aperçu HDR : Reinhard / sRGB à 0 EV. Les écarts utilisent les flottants sources."},
         {"HDR: %llu changed pixels | maximum sample delta: %.8g",
          "HDR : %llu pixels modifiés | écart maximal source : %.8g"},
         {"HDR and LDR sources require an explicit common sample space.",
          "Les sources HDR et LDR nécessitent un espace d'échantillonnage commun explicite."},
         {"Pixel difference mask", "Masque des pixels modifiés"},
         {"%llu changed pixels | maximum delta: %u", "%llu pixels modifiés | écart maximal : %u"},
         {"Images have different dimensions.", "Les images ont des dimensions différentes."},
         {"Added", "Ajouté"},
         {"Removed", "Supprimé"},
         {"Modified", "Modifié"},
         {"Unchanged", "Inchangé"},
         {"Change", "Changement"},
         {"Changes", "Changements"},
         {"Type", "Type"},
         {"Property", "Propriété"},
         {"Changed only", "Modifiés uniquement"},
         {"Raw JSON", "JSON brut"},
         {"Property table reached its display limit.",
          "Le tableau des propriétés a atteint sa limite d'affichage."},
         {"Added package", "Package ajouté"},
         {"Removed package", "Package supprimé"},
         {"Asset inspection", "Inspection des assets"},
         {"Objects", "Objets"},
         {"Properties", "Propriétés"},
         {"Comparison", "Comparaison"},
         {"No asset loaded", "Aucun asset chargé"},
         {"Open an Unreal package in the host application.",
          "Ouvrez un fichier Unreal depuis l'application hôte."},
         {"Some payloads are opaque. Their hashes remain available for comparison.",
          "Certains blocs restent opaques. Leurs empreintes permettent de les comparer."},
         {"Language", "Langue"},
         {"Blueprint graphs", "Graphes Blueprint"},
         {"Graph", "Graphe"},
         {"Textures", "Textures"},
         {"Materials", "Matériaux"},
         {"Material", "Matériau"},
         {"No material", "Aucun matériau"},
         {"This package has no material graph or instance.",
          "Ce package ne contient aucun graphe ni instance de matériau."},
         {"Some material data could not be resolved.",
          "Certaines données du matériau n'ont pas pu être résolues."},
         {"Parent: %s", "Parent : %s"},
         {"Stored overrides only. Parent values are not evaluated.",
          "Surcharges enregistrées uniquement. Les valeurs du parent ne sont pas évaluées."},
         {"Parameter", "Paramètre"},
         {"Kind", "Type"},
         {"Value", "Valeur"},
         {"Override", "Surcharge"},
         {"Unavailable", "Indisponible"},
         {"scalar", "scalaire"},
         {"vector", "vecteur"},
         {"double-vector", "vecteur double"},
         {"texture", "texture"},
         {"virtual-texture", "texture virtuelle"},
         {"volume-texture", "texture volumique"},
         {"font", "police"},
         {"static-switch", "commutateur statique"},
         {"static-mask", "masque statique"},
         {"Some output ports are inferred from saved connections.",
          "Certains ports de sortie sont déduits des connexions enregistrées."},
         {"Meshes", "Meshes"},
         {"Mesh preview unavailable", "Aperçu du mesh indisponible"},
         {"Load a supported source mesh in the application.",
          "Chargez un mesh source pris en charge dans l'application."},
         {"%zu vertices / %zu triangles / %zu material groups",
          "%zu sommets / %zu triangles / %zu groupes de matériaux"},
         {"Fit mesh", "Cadrer le mesh"},
         {"Alt + drag: orbit | Middle drag: pan | Wheel: zoom",
          "Alt + glisser : orbite | Bouton central : déplacement | Molette : zoom"},
         {"Preview is limited to 100,000 edges.", "L'aperçu est limité à 100 000 arêtes."},
         {"No texture source", "Aucune source de texture"},
         {"This package has no decoded Texture2D source.",
          "Ce package ne contient aucune source Texture2D décodée."},
         {"Preview unavailable", "Aperçu indisponible"},
         {"Load a supported source preview in the application.",
          "Chargez un aperçu source pris en charge dans l'application."},
         {"Fit image", "Cadrer l'image"},
         {"Pixel: %d, %d", "Pixel : %d, %d"},
         {"Fit graph", "Cadrer le graphe"},
         {"Before", "Avant"},
         {"After", "Après"},
         {"No Blueprint graph", "Aucun graphe Blueprint"},
         {"This package has no decoded editor graph.", "Ce package ne contient aucun graphe éditeur décodé."},
         {"%zu nodes / %zu pins / %zu links", "%zu nœuds / %zu pins / %zu connexions"},
         {"This graph is incomplete. Some pins or connections could not be resolved.",
          "Ce graphe est incomplet. Certains pins ou connexions n'ont pas pu être résolus."},
         {"Graph diagnostics", "Diagnostics du graphe"},
         {"Empty package", "Package vide"},
         {"This package has no exported objects.", "Ce package ne contient aucun objet exporté."}});
}
void AssetView::set_packages(std::shared_ptr<const Package> before, std::shared_ptr<const Package> after,
                             bool compare_missing_side) {
    before_ = std::move(before);
    after_ = std::move(after);
    selected_ = 0;
    comparison_ = (before_ && after_) || (compare_missing_side && (before_ || after_));
    properties_after_ = !before_;
    auto prepare_inspection = [](const std::shared_ptr<const Package> &package) {
        std::vector<std::string> result;
        if (package)
            for (const auto &entry : package->exports) {
                Json data = {{"properties", entry.properties}, {"opaque", entry.opaque}};
                if (!entry.native_data.empty())
                    data["native"] = entry.native_data;
                result.push_back(data.dump(2));
            }
        return result;
    };
    before_properties_ = prepare_inspection(before_);
    after_properties_ = prepare_inspection(after_);
    auto inspections = [&](const auto &local, const auto &peer, bool after) {
        std::vector<PropertyView> views;
        std::map<std::string, const ObjectExport *> objects;
        if (peer)
            for (const auto &entry : peer->exports)
                objects[entry.path] = &entry;
        auto data = [](const ObjectExport &entry) {
            return Json{
                {"properties", entry.properties}, {"opaque", entry.opaque}, {"native", entry.native_data}};
        };
        if (local)
            for (const auto &entry : local->exports) {
                auto it = objects.find(entry.path);
                auto a = data(entry), b = it == objects.end() ? Json{} : data(*it->second);
                const Json *match = it == objects.end() ? nullptr : &b;
                PropertyView view;
                view.set_values(after ? match : &a, after ? &a : match, comparison_);
                views.push_back(std::move(view));
            }
        return views;
    };
    before_inspection_ = inspections(before_, after_, false);
    after_inspection_ = inspections(after_, before_, true);
    const auto &initial = properties_after_ ? after_inspection_ : before_inspection_;
    if (comparison_)
        for (std::size_t i = 0; i < initial.size(); ++i)
            if (!initial[i].table().rows.empty() &&
                initial[i].table().rows.front().change != ChangeKind::unchanged) {
                selected_ = i;
                break;
            }

    before_geometry_.reset();
    after_geometry_.reset();
    graph_after_ = texture_after_ = mesh_after_ = material_after_ = !before_;
    before_mesh_.set_mesh({});
    after_mesh_.set_mesh({});
    texture_selected_ = 0;
    before_textures_.clear();
    after_textures_.clear();
    auto load = [](const std::shared_ptr<const Package> &package, std::vector<TextureView> &views) {
        if (package)
            for (const auto &texture : package->textures) {
                TextureView view;
                view.set_source(texture);
                views.push_back(std::move(view));
            }
    };
    load(before_, before_textures_);
    load(after_, after_textures_);
    const Package empty;
    const auto *before_peer = comparison_ ? (after_ ? after_.get() : &empty) : nullptr;
    const auto *after_peer = comparison_ ? (before_ ? before_.get() : &empty) : nullptr;
    before_material_.set_package(before_ ? *before_ : empty, before_peer, false);
    after_material_.set_package(after_ ? *after_ : empty, after_peer, true);
    before_graph_.set_package(before_ ? *before_ : empty, before_peer, false);
    after_graph_.set_package(after_ ? *after_ : empty, after_peer, true);
    document_ = {};
    diff_state_ = {};
    if (comparison_) {
        auto snapshot = [](const auto &package) {
            return package ? package->semantic_snapshot().dump(2) : Json{{"absent", true}}.dump(2);
        };
        document_.setTexts(snapshot(before_), snapshot(after_), cyui::CodeLanguage::json);
        if (!document_.hunks().empty()) {
            diff_state_.hunk = 0;
            diff_state_.focus = document_.hunks().front();
        }
    }
}
bool AssetView::set_texture_preview(const std::string &path, const cyui::ImageData &image, bool after) {
    auto &views = after ? after_textures_ : before_textures_;
    for (auto &view : views)
        if (view.object_path() == path)
            return view.set_preview(image);
    return false;
}
bool AssetView::set_texture_difference(const std::string &before_path, const std::string &after_path,
                                       const cyui::ImageData &image, const ImageComparison &comparison) {
    bool found = false;
    for (auto &view : before_textures_)
        if (view.object_path() == before_path)
            found = view.set_difference(image, comparison) || found;
    for (auto &view : after_textures_)
        if (view.object_path() == after_path)
            found = view.set_difference(image, comparison) || found;
    return found;
}
bool AssetView::set_texture_difference(const std::string &before_path, const std::string &after_path,
                                       const cyui::ImageData &image, const HdrImageComparison &comparison) {
    bool found = false;
    for (auto &view : before_textures_)
        if (view.object_path() == before_path)
            found = view.set_difference(image, comparison) || found;
    for (auto &view : after_textures_)
        if (view.object_path() == after_path)
            found = view.set_difference(image, comparison) || found;
    return found;
}
bool AssetView::clear_texture_preview(const std::string &path, bool after) {
    auto &views = after ? after_textures_ : before_textures_;
    for (auto &view : views)
        if (view.object_path() == path) {
            view.clear_preview();
            return true;
        }
    return false;
}
void AssetView::set_mesh_geometry(std::shared_ptr<const MeshGeometry> geometry, bool after) {
    (after ? after_geometry_ : before_geometry_) = std::move(geometry);
    before_mesh_.set_mesh(before_geometry_, after_geometry_, false, comparison_);
    after_mesh_.set_mesh(after_geometry_, before_geometry_, true, comparison_);
}
Json AssetView::view_state() const {
    const auto &p = properties_after_ ? after_ : before_;
    const auto &t = texture_after_ ? after_textures_ : before_textures_;
    Json result = {
        {"filter", filter_.query},
        {"selected", p && selected_ < p->exports.size() ? Json(p->exports[selected_].path) : Json(nullptr)},
        {"propertiesAfter", properties_after_},
        {"rawProperties", raw_properties_},
        {"graphAfter", graph_after_},
        {"materialAfter", material_after_},
        {"textureAfter", texture_after_},
        {"meshAfter", mesh_after_},
        {"textureSelected",
         texture_selected_ < t.size() ? Json(t[texture_selected_].object_path()) : Json(nullptr)},
        {"comparison",
         {{"split", diff_state_.split},
          {"query", diff_state_.query},
          {"scroll", std::max(0.f, diff_state_.scroll)}}}};
    auto side = [&](bool after) {
        const auto &package = after ? after_ : before_;
        const auto &views = after ? after_inspection_ : before_inspection_;
        Json properties = Json::object();
        if (package && selected_ < views.size())
            properties[package->exports[selected_].path] = views[selected_].view_state();
        Json textures = Json::object();
        const auto &tv = after ? after_textures_ : before_textures_;
        if (texture_selected_ < tv.size())
            textures[tv[texture_selected_].object_path()] = tv[texture_selected_].view_state();
        for (const auto &v : tv)
            if (textures.size() < 128)
                textures[v.object_path()] = v.view_state();
        return Json{{"properties", properties},
                    {"blueprint", (after ? after_graph_ : before_graph_).view_state()},
                    {"material", (after ? after_material_ : before_material_).view_state()},
                    {"textures", textures},
                    {"mesh", (after ? after_mesh_ : before_mesh_).view_state()}};
    };
    result["before"] = side(false);
    result["after"] = side(true);
    return result;
}
void AssetView::restore_view_state(const Json &state) {
    cyuediff::view_state::validate(state, "asset");
    auto flag = [&](const char *key, bool &out) {
        if (state.contains(key))
            out = state[key].get<bool>();
        if (!before_)
            out = bool(after_);
        if (!after_)
            out = false;
    };
    flag("propertiesAfter", properties_after_);
    flag("graphAfter", graph_after_);
    flag("materialAfter", material_after_);
    flag("textureAfter", texture_after_);
    flag("meshAfter", mesh_after_);
    if (state.contains("rawProperties"))
        raw_properties_ = state["rawProperties"].get<bool>();
    if (state.contains("filter"))
        filter_.query = state["filter"].get<std::string>();
    if (state.contains("selected")) {
        selected_ = 0;
        const auto &p = properties_after_ ? after_ : before_;
        if (p)
            for (std::size_t i = 0; i < p->exports.size(); ++i)
                if (state["selected"] == p->exports[i].path) {
                    selected_ = i;
                    break;
                }
    }
    if (state.contains("textureSelected")) {
        texture_selected_ = 0;
        const auto &views = texture_after_ ? after_textures_ : before_textures_;
        for (std::size_t i = 0; i < views.size(); ++i)
            if (state["textureSelected"] == views[i].object_path()) {
                texture_selected_ = i;
                break;
            }
    }
    for (bool after : {false, true}) {
        const char *key = after ? "after" : "before";
        if (!state.contains(key))
            continue;
        const auto &v = state[key];
        if (v.contains("blueprint"))
            (after ? after_graph_ : before_graph_).restore_view_state(v["blueprint"]);
        if (v.contains("material"))
            (after ? after_material_ : before_material_).restore_view_state(v["material"]);
        if (v.contains("mesh"))
            (after ? after_mesh_ : before_mesh_).restore_view_state(v["mesh"]);
        if (v.contains("textures"))
            for (auto &t : after ? after_textures_ : before_textures_) {
                auto it = v["textures"].find(t.object_path());
                if (it != v["textures"].end())
                    t.restore_view_state(*it);
            }
        const auto &p = after ? after_ : before_;
        auto &views = after ? after_inspection_ : before_inspection_;
        if (p && v.contains("properties"))
            for (std::size_t i = 0; i < views.size(); ++i) {
                auto it = v["properties"].find(p->exports[i].path);
                if (it != v["properties"].end())
                    views[i].restore_view_state(*it);
            }
    }
    if (state.contains("comparison")) {
        const auto &v = state["comparison"];
        if (v.contains("split"))
            diff_state_.split = v["split"].get<bool>();
        if (v.contains("query"))
            diff_state_.query = v["query"].get<std::string>();
        if (v.contains("scroll")) {
            diff_state_.scroll = v["scroll"].get<float>();
            diff_state_.focus = -1;
            diff_state_.hunk = -1;
        }
    }
}
void AssetView::draw(const char *id, bool show_language) {
    cyui::ScopedID scope(id);
    if (show_language && ImGui::BeginCombo(cyui::trLabel("Language").c_str(), cyui::locale().c_str())) {
        for (const auto &locale : cyui::availableLocales())
            if (ImGui::Selectable(locale.name.c_str(), cyui::locale() == locale.code))
                cyui::setLocale(locale.code.c_str());
        ImGui::EndCombo();
    }
    if (!before_ && !after_) {
        cyui::emptyState(cyui::tr("No asset loaded").c_str(),
                         cyui::tr("Open an Unreal package in the host application.").c_str(),
                         cyui::icon::document);
        return;
    }
    const auto &present = before_ ? before_ : after_;
    cyui::panelHeader(cyui::tr("Asset inspection").c_str(), present->source.filename().u8string().c_str());
    if (comparison_ && (!before_ || !after_))
        cyui::badge(cyui::tr(before_ ? "Removed package" : "Added package").c_str());
    cyui::callout(
        cyui::Tone::warning,
        cyui::tr("Some payloads are opaque. Their hashes remain available for comparison.").c_str());
    if (ImGui::BeginTabBar("##asset-tabs")) {
        auto requested = requested_tab_;
        requested_tab_.reset();
        auto flags = [&](AssetTab tab) {
            return requested == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };
        if (ImGui::BeginTabItem(cyui::trLabel("Properties").c_str(), nullptr, flags(AssetTab::properties))) {
            active_tab_ = AssetTab::properties;
            if (before_ && after_) {
                if (ImGui::RadioButton(cyui::trLabel("Before").c_str(), !properties_after_)) {
                    properties_after_ = false;
                    selected_ = 0;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(cyui::trLabel("After").c_str(), properties_after_)) {
                    properties_after_ = true;
                    selected_ = 0;
                }
            }
            const auto &package = properties_after_ ? after_ : before_;
            const auto &inspection = properties_after_ ? after_properties_ : before_properties_;
            cyui::filterBox("##objects-filter", filter_);
            if (package->exports.empty())
                cyui::emptyState(cyui::tr("Empty package").c_str(),
                                 cyui::tr("This package has no exported objects.").c_str(),
                                 cyui::icon::document);
            else if (ImGui::BeginTable("##inspection", 2, ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("##objects", ImGuiTableColumnFlags_WidthFixed, cyui::dp(300));
                ImGui::TableSetupColumn("##properties");
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::BeginChild("##objects"))
                    for (std::size_t i = 0; i < package->exports.size(); ++i) {
                        const auto &entry = package->exports[i];
                        if (!filter_.matches(entry.path.c_str(), entry.class_name.c_str()))
                            continue;
                        const auto &table =
                            (properties_after_ ? after_inspection_ : before_inspection_)[i].table();
                        bool changed = comparison_ && !table.rows.empty() &&
                                       table.rows.front().change != ChangeKind::unchanged;
                        if (changed)
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                                  detail::change_color(table.rows.front().change));
                        if (ImGui::Selectable(entry.path.c_str(), selected_ == i))
                            selected_ = i;
                        if (changed)
                            ImGui::PopStyleColor();
                    }
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(1);
                const auto &entry = package->exports[selected_];
                cyui::textDim("%s", entry.class_name.c_str());
                cyui::textDim("%s", entry.coverage.c_str());
                ImGui::Checkbox(cyui::trLabel("Raw JSON").c_str(), &raw_properties_);
                if (raw_properties_) {
                    if (ImGui::BeginChild("##properties", {}, ImGuiChildFlags_None,
                                          ImGuiWindowFlags_HorizontalScrollbar))
                        ImGui::TextUnformatted(inspection[selected_].c_str());
                    ImGui::EndChild();
                } else
                    (properties_after_ ? after_inspection_ : before_inspection_)[selected_].draw();
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(cyui::trLabel("Blueprint graphs").c_str(), nullptr,
                                flags(AssetTab::blueprint))) {
            active_tab_ = AssetTab::blueprint;
            if (before_ && after_) {
                if (ImGui::RadioButton(cyui::trLabel("Before").c_str(), !graph_after_))
                    graph_after_ = false;
                ImGui::SameLine();
                if (ImGui::RadioButton(cyui::trLabel("After").c_str(), graph_after_))
                    graph_after_ = true;
            }
            (graph_after_ ? after_graph_ : before_graph_).draw("##blueprint");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(cyui::trLabel("Materials").c_str(), nullptr, flags(AssetTab::materials))) {
            active_tab_ = AssetTab::materials;
            if (before_ && after_) {
                if (ImGui::RadioButton(cyui::trLabel("Before").c_str(), !material_after_))
                    material_after_ = false;
                ImGui::SameLine();
                if (ImGui::RadioButton(cyui::trLabel("After").c_str(), material_after_))
                    material_after_ = true;
            }
            (material_after_ ? after_material_ : before_material_).draw("##material");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(cyui::trLabel("Textures").c_str(), nullptr, flags(AssetTab::textures))) {
            active_tab_ = AssetTab::textures;
            if (before_ && after_) {
                if (ImGui::RadioButton(cyui::trLabel("Before").c_str(), !texture_after_)) {
                    texture_after_ = false;
                    texture_selected_ = 0;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(cyui::trLabel("After").c_str(), texture_after_)) {
                    texture_after_ = true;
                    texture_selected_ = 0;
                }
            }
            auto &views = texture_after_ ? after_textures_ : before_textures_;
            if (views.empty())
                TextureView{}.draw();
            else {
                if (ImGui::BeginCombo("##texture-selection",
                                      views[texture_selected_].object_path().c_str())) {
                    for (std::size_t i = 0; i < views.size(); ++i)
                        if (ImGui::Selectable(views[i].object_path().c_str(), i == texture_selected_))
                            texture_selected_ = i;
                    ImGui::EndCombo();
                }
                views[texture_selected_].draw();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(cyui::trLabel("Meshes").c_str(), nullptr, flags(AssetTab::meshes))) {
            active_tab_ = AssetTab::meshes;
            if (before_ && after_) {
                if (ImGui::RadioButton(cyui::trLabel("Before").c_str(), !mesh_after_))
                    mesh_after_ = false;
                ImGui::SameLine();
                if (ImGui::RadioButton(cyui::trLabel("After").c_str(), mesh_after_))
                    mesh_after_ = true;
            }
            (mesh_after_ ? after_mesh_ : before_mesh_).draw();
            ImGui::EndTabItem();
        }
        if (comparison_ &&
            ImGui::BeginTabItem(cyui::trLabel("Comparison").c_str(), nullptr, flags(AssetTab::comparison))) {
            active_tab_ = AssetTab::comparison;
            cyui::diffViewer("##semantic-diff", diff_state_, document_, ImGui::GetContentRegionAvail());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
} // namespace cyuediff
