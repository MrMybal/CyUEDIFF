#include "json_text.hpp"
#include <cyuediff/mesh_skin.hpp>
#include <cyuediff/mesh_morphs.hpp>
#include <cyuediff/asset_view.hpp>
#include <cyuediff/animation_pose_view.hpp>
#include "diff_style.hpp"
#include <iomanip>
#include <ostream>
#include <streambuf>
namespace cyuediff::detail {
namespace {
struct PresentationTextLimit {};
class PresentationTextBuffer final : public std::streambuf {
    std::string text_;
    std::size_t lines_ = 1;
    void append(const char *data, std::size_t size) {
        if (size > presentation_json_bytes - text_.size())
            throw PresentationTextLimit{};
        const auto lines = static_cast<std::size_t>(std::count(data, data + size, '\n'));
        if (lines > presentation_json_lines - lines_)
            throw PresentationTextLimit{};
        lines_ += lines;
        text_.append(data, size);
    }
    std::streamsize xsputn(const char *data, std::streamsize size) override {
        if (size > 0)
            append(data, static_cast<std::size_t>(size));
        return size;
    }
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            const auto value = traits_type::to_char_type(c);
            append(&value, 1);
        }
        return traits_type::not_eof(c);
    }

  public:
    std::string take() {
        return std::move(text_);
    }
};
} // namespace
std::string presentation_json(const Json &value) {
    PresentationTextBuffer buffer;
    std::ostream output(&buffer);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    try {
        output << std::setw(2) << value;
        return buffer.take();
    } catch (const PresentationTextLimit &) {
        return {};
    }
}
} // namespace cyuediff::detail
namespace cyuediff {
void AssetView::set_animation_poses(const AnimationPose *before, const AnimationPose *after, bool comparing) {
    animation_pose_.set_poses(before,after,comparing);
    pose_bound_=before || after;pose_preview_=pose_bound_;
}
void AssetView::set_animation_socket_poses(const AnimationSocketPose *a, const AnimationSocketPose *b, bool comparing) {
    animation_sockets_.set_socket_poses(a,b,comparing);sockets_bound_=a||b;sockets_preview_=false;
}
void AssetView::register_translations() {
    PropertyBrowserView::register_translations();
    cyui::registerTranslations("fr",
                               {{"Browse by level", "Parcourir par niveau"},
                                {"Stored properties", "Propriétés enregistrées"},
                                {"Native fields", "Champs natifs"},
                                {"Opaque metadata", "Métadonnées opaques"},
                                {"Some values are unknown; inspecting one snapshot.",
                                 "Certaines valeurs sont inconnues ; inspection d'un seul instantané."}});
    AnimationMetadataView::register_translations();
    AttachmentsView::register_translations();
    AnimationSourceView::register_translations();
    AnimationPoseView::register_translations();
    cyui::registerTranslations("fr", {{"Show source socket origins", "Afficher les origines des sockets animés"}, {"Show source hierarchy pose", "Afficher la pose hiérarchique source"}});
    CurveView::register_translations();
    cyui::registerTranslations(
        "fr", {{"Collisions", "Collisions"},
               {"Click a wire: inspect shape | Alt + drag: orbit | Middle drag: pan | Wheel: zoom",
                "Clic sur une ar\u00eate : inspecter | Alt + glisser : orbite | Glisser central : "
                "d\u00e9placer | Molette : zoom"},
               {"Collision preview unavailable; stored properties remain inspectable.",
                "Aper\u00e7u des collisions indisponible ; les propri\u00e9t\u00e9s restent consultables."},
               {"Collision geometry", "G\u00e9om\u00e9trie de collision"},
               {"Changed shapes only", "Formes modifi\u00e9es uniquement"},
               {"Fit collision geometry", "Cadrer les collisions"},
               {"Some shapes have no supported stored geometry.",
                "Certaines formes ne disposent pas de g\u00e9om\u00e9trie enregistr\u00e9e prise en charge."},
               {"No visible collision geometry", "Aucune g\u00e9om\u00e9trie de collision visible"},
               {"Select a body to preview its local geometry.",
                "S\u00e9lectionnez un corps pour afficher sa g\u00e9om\u00e9trie locale."},
               {"Cooker-coordinate preview; bone and component placement is unavailable.",
                "Aperçu dans les coordonnées de cuisson ; placement des os et composants indisponible."},
               {"Body-local preview; bone and component placement is unavailable.",
                "Aper\u00e7u local au corps ; placement des os et composants indisponible."},
               {"Collision body", "Corps de collision"},
               {"Collision shape", "Forme de collision"},
               {"All bodies and references", "Tous les corps et r\u00e9f\u00e9rences"},
               {"All stored shapes", "Toutes les formes enregistr\u00e9es"},
               {"No stored collision bodies", "Aucun corps de collision enregistr\u00e9"},
               {"This package has no supported BodySetup projection.",
                "Ce package ne contient aucun BodySetup pris en charge."},
               {"Stored collision shapes; native physics data is not decoded.",
                "Formes de collision enregistrées ; les données physiques natives ne sont pas décodées."},
               {"Decoded native Chaos geometry in cooker coordinates; no simulation.",
                "Géométrie Chaos native décodée dans les coordonnées de cuisson ; aucune simulation."},
               {"Stored collision properties; physics data is not evaluated.",
                "Propri\u00e9t\u00e9s de collision enregistr\u00e9es ; les donn\u00e9es physiques ne sont "
                "pas \u00e9valu\u00e9es."},
               {"Collision data is incomplete. Missing fields and external bodies remain explicit.",
                "Donn\u00e9es de collision incompl\u00e8tes. Les champs manquants et les corps externes "
                "restent explicites."},
               {"Previous changed shape", "Forme modifi\u00e9e pr\u00e9c\u00e9dente"},
               {"Next changed shape", "Forme modifi\u00e9e suivante"}});
    cyui::registerTranslations(
        "fr",
        {{"Reference displacement: %.6g | delta (%.6g, %.6g, %.6g)", "Déplacement de référence : %.6g | delta (%.6g, %.6g, %.6g)"},
         {"Retarget source before","Source de retargeting avant"},
         {"Retarget source after","Source de retargeting après"},
         {"Retarget source","Source de retargeting"},
         {"Stored raw reference","Référence brute enregistrée"},
         {"Hierarchy", "Hiérarchie"},
         {"Reference pose", "Pose de référence"},
         {"Bone", "Os"},
         {"No reference positions available", "Aucune position de référence disponible"},
         {"Some reference joints are unavailable.", "Certaines articulations de référence sont indisponibles."},
         {"Pose preview is limited to 4,096 joints.", "L'aperçu est limité à 4 096 articulations."},
         {"Moved joints only", "Articulations déplacées uniquement"},
         {"Fit reference pose", "Cadrer la pose de référence"},
         {"Affine reference positions; colors show position or parent changes.",
          "Positions de référence affines ; les couleurs indiquent les changements de position ou de parent."},
         {"Click a joint: inspect | Alt + drag: orbit | Middle drag: pan | Wheel: zoom",
          "Clic sur une articulation : inspecter | Alt + glisser : orbite | Glisser central : déplacer | Molette : zoom"},
         {"Skeleton", "Squelette"},
         {"Skeleton object", "Objet squelette"},
         {"Parent", "Parent"},
         {"Children", "Enfants"},
         {"No stored reference skeleton", "Aucun squelette de r\u00e9f\u00e9rence enregistr\u00e9"},
         {"This package has no supported Skeleton or SkeletalMesh object.",
          "Ce package ne contient aucun objet Skeleton ou SkeletalMesh pris en charge."},
         {"Raw reference hierarchy and local pose; animations and skinning are not evaluated.",
          "Hi\u00e9rarchie de r\u00e9f\u00e9rence et pose locale ; animations et skinning non "
          "\u00e9valu\u00e9s."},
         {"Skeleton data is incomplete. Unknown native data remains opaque.",
          "Donn\u00e9es du squelette incompl\u00e8tes. Les donn\u00e9es natives inconnues restent opaques."},
         {"Hierarchy diagram is limited to 4,096 bones. Complete stored data remains in Properties.",
          "Le diagramme est limit\u00e9 \u00e0 4 096 os. Les donn\u00e9es compl\u00e8tes restent dans "
          "Propri\u00e9t\u00e9s."},
         {"Skeleton properties", "Propri\u00e9t\u00e9s du squelette"},
         {"Connections include both stored parent relationships.",
          "Les connexions incluent les deux parent\u00e9s enregistr\u00e9es."}});
    GraphInspector::register_translations();
    cyui::registerTranslations("fr",
                               {{"Actual pixels", "Pixels r\u00e9els"},
                                {"Fit changed pixels", "Cadrer les pixels modifi\u00e9s"},
                                {"%.4f%% changed | mean absolute error: %.8g",
                                 "%.4f%% modifi\u00e9s | erreur absolue moyenne : %.8g"},
                                {"Changed edges only", "Ar\u00eates modifi\u00e9es uniquement"},
                                {"Fit changed geometry", "Cadrer la g\u00e9om\u00e9trie modifi\u00e9e"},
                                {"Camera view", "Vue de la cam\u00e9ra"},
                                {"Choose view", "Choisir une vue"},
                                {"Perspective", "Perspective"},
                                {"Front", "Face"},
                                {"Right", "Droite"},
                                {"Top", "Dessus"},
                                {"No changed edges", "Aucune ar\u00eate modifi\u00e9e"},
                                {"Show unchanged edges to inspect the complete mesh.",
                                 "Affichez les ar\u00eates inchang\u00e9es pour examiner le mesh complet."},
                                {"No matching parameter", "Aucun param\u00e8tre correspondant"},
                                {"Change the filter or show unchanged parameters.",
                                 "Modifiez le filtre ou affichez les param\u00e8tres inchang\u00e9s."}});
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
         {"Inspect morph targets", "Inspecter les morph targets"},
         {"Stored position and split-normal deltas in source space",
          "Deltas de position et de normales par coin dans l’espace source"},
         {"Inspect source skinning", "Inspecter le skinning source"},
         {"Stored bone poses and raw weights (0..65535)", "Poses des os et poids bruts stockés (0..65535)"},
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
         {"Whole-document text exceeds the display limits. Use scoped properties or export JSON with the "
          "CLI.",
          "Le texte complet dépasse les limites d’affichage. Utilisez les propriétés par niveau ou exportez "
          "le JSON avec la CLI."},
         {"Raw JSON exceeds the display limits. Use scoped properties or export JSON with the CLI.",
          "Le JSON brut dépasse les limites d’affichage. Utilisez les propriétés par niveau ou exportez le "
          "JSON avec la CLI."},
         {"Empty package", "Package vide"},
         {"This package has no exported objects.", "Ce package ne contient aucun objet exporté."}});
}
void AssetView::set_packages(std::shared_ptr<const Package> before, std::shared_ptr<const Package> after,
                             bool compare_missing_side) {
    if (before && after && (before->source_read_limit_failed || after->source_read_limit_failed))
        throw Error("source-read-limits",
                    "A source read limit/count guard failed; semantic review is unavailable.");

    layout_.restore(Json::object(), "asset-layout");
    before_ = std::move(before);
    after_ = std::move(after);
    selected_ = 0;
    property_browser_ = PropertyBrowserView{};
    browse_properties_ = false;
    browser_object_.clear();
    browser_root_ = 0;
    bound_browser_root_ = -1;
    comparison_ = (before_ && after_) || (compare_missing_side && (before_ || after_));
    properties_after_ = !before_;
    auto prepare_inspection = [](const std::shared_ptr<const Package> &package) {
        std::vector<std::string> result;
        if (package)
            for (const auto &entry : package->exports) {
                if (detail::presentation_json(entry.properties).empty() ||
                    detail::presentation_json(entry.native_data).empty()) {
                    result.emplace_back();
                    continue;
                }
                Json data = {{"properties", entry.properties}, {"opaque", entry.opaque}};
                if (!entry.native_data.empty())
                    data["native"] = entry.native_data;
                result.push_back(detail::presentation_json(data));
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
                const auto &raw = after ? after_properties_ : before_properties_;
                const auto &peer_raw = after ? before_properties_ : after_properties_;
                const auto local_index = static_cast<std::size_t>(&entry - local->exports.data());
                const auto peer_index =
                    it == objects.end() ? 0 : static_cast<std::size_t>(it->second - peer->exports.data());
                if (raw[local_index].empty() || (it != objects.end() && peer_raw[peer_index].empty())) {
                    const auto *a = &entry.properties;
                    const auto *b = it == objects.end() ? nullptr : &it->second->properties;
                    if (after)
                        std::swap(a, b);
                    if (!comparison_)
                        a = b = &entry.properties;
                    PropertyTableOptions options;
                    options.max_rows = 1;
                    options.escape_non_bmp = sizeof(ImWchar) == 2;
                    auto table = property_table(a, b, options);
                    if (comparison_ && it != objects.end() && !table.rows.empty() &&
                        (entry.native_data != it->second->native_data || entry.opaque != it->second->opaque))
                        table.rows[0].change = ChangeKind::modified;
                    PropertyView view;
                    view.set_table(std::move(table), comparison_);
                    views.push_back(std::move(view));
                    browse_properties_ = true;
                    continue;
                }
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

    bind_property_browser();
    collisions_.set_packages(before_.get(), after_.get(), comparison_);
    skeleton_.set_packages(before_.get(), after_.get(), comparison_);
    animations_.set_packages(before_.get(),after_.get(),comparison_);
    set_animation_poses(nullptr);
    set_animation_socket_poses(nullptr);
    curves_.set_packages(before_.get(),after_.get(),comparison_);
    before_geometry_.reset();
    after_geometry_.reset();
    graph_after_ = texture_after_ = mesh_after_ = material_after_ = !before_;
    before_mesh_.set_mesh({});
    skin_.set_values(nullptr, nullptr, false);
    skin_inspection_ = false;
    morphs_.set_values(nullptr, nullptr, false);
    morph_inspection_ = false;
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
    text_comparison_available_ = false;
    if (comparison_) {
        auto small = [](const auto &raw) {
            std::size_t size = 0;
            for (const auto &text : raw) {
                if (text.empty() || text.size() > detail::presentation_json_bytes - size)
                    return false;
                size += text.size();
            }
            return true;
        };
        if (small(before_properties_) && small(after_properties_)) {
            auto snapshot = [](const auto &package) {
                return detail::presentation_json(package ? package->semantic_snapshot()
                                                         : Json{{"absent", true}});
            };
            const auto before = snapshot(before_), after = snapshot(after_);
            if (!before.empty() && !after.empty()) {
                document_.setTexts(before, after, cyui::CodeLanguage::json);
                text_comparison_available_ = true;
                if (!document_.hunks().empty()) {
                    diff_state_.hunk = 0;
                    diff_state_.focus = document_.hunks().front();
                }
            }
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
    const auto before_skin = before_geometry_ && before_geometry_->skin ? before_geometry_->skin->to_json() : Json(nullptr);
    const auto after_skin = after_geometry_ && after_geometry_->skin ? after_geometry_->skin->to_json() : Json(nullptr);
    skin_.set_values(before_skin.is_null() ? nullptr : &before_skin,
                     after_skin.is_null() ? nullptr : &after_skin, comparison_);
    const auto before_morphs =
        before_geometry_ && before_geometry_->morphs ? before_geometry_->morphs->to_json() : Json(nullptr);
    const auto after_morphs =
        after_geometry_ && after_geometry_->morphs ? after_geometry_->morphs->to_json() : Json(nullptr);
    morphs_.set_values(before_morphs.is_null() ? nullptr : &before_morphs,
                       after_morphs.is_null() ? nullptr : &after_morphs, comparison_);
}
AssetView::BrowserInputs AssetView::property_browser_inputs() const {
    const auto &selected = properties_after_ ? after_ : before_;
    if (!selected || selected_ >= selected->exports.size())
        return {};
    const auto &path = selected->exports[selected_].path;
    auto find = [&](const auto &package) -> const ObjectExport * {
        if (package)
            for (const auto &entry : package->exports)
                if (entry.path == path)
                    return &entry;
        return nullptr;
    };
    const auto a = find(before_), b = find(after_);
    auto value = [&](const ObjectExport *entry) -> const Json * {
        if (!entry)
            return nullptr;
        return browser_root_ == 0   ? &entry->properties
               : browser_root_ == 1 ? &entry->native_data
                                    : &entry->opaque;
    };
    auto known = [&](const ObjectExport *entry) {
        if (!entry || browser_root_ == 2)
            return true;
        if (browser_root_ == 1)
            return !entry->native_data.empty();
        return entry->coverage == "properties" || entry->coverage == "empty";
    };
    BrowserInputs result{value(a), value(b), comparison_ && known(a) && known(b),
                         comparison_ && (!known(a) || !known(b))};
    if (!result.comparing) {
        const auto local = properties_after_ ? result.after : result.before;
        result.before = local ? local : result.before ? result.before : result.after;
        result.after = nullptr;
    }
    return result;
}
void AssetView::bind_property_browser() {
    const auto &package = properties_after_ ? after_ : before_;
    const auto path =
        package && selected_ < package->exports.size() ? package->exports[selected_].path : std::string{};
    if (path == browser_object_ && browser_after_ == properties_after_ &&
        bound_browser_root_ == browser_root_)
        return;
    browser_object_ = path;
    browser_after_ = properties_after_;
    bound_browser_root_ = browser_root_;
    const auto input = property_browser_inputs();
    property_browser_.set_values(input.before, input.after, input.comparing);
    const auto &views = properties_after_ ? after_inspection_ : before_inspection_;
    if (selected_ < views.size() && views[selected_].table().truncated)
        browse_properties_ = true;
}
bool AssetView::set_property_scope(const std::string &scope, std::size_t first, int root) {
    if (root < 0 || root > 2)
        return false;
    browser_root_ = root;
    bind_property_browser();
    const auto input = property_browser_inputs();
    if (!property_browser_.navigate(input.before, input.after, scope, first))
        return false;
    browse_properties_ = true;
    return true;
}
Json AssetView::view_state() const {
    const auto &p = properties_after_ ? after_ : before_;
    const auto &t = texture_after_ ? after_textures_ : before_textures_;
    Json result = {
        {"filter", filter_.query},
        {"selected", p && selected_ < p->exports.size() ? Json(p->exports[selected_].path) : Json(nullptr)},
        {"propertiesAfter", properties_after_},
        {"rawProperties", raw_properties_},
        {"browseProperties", browse_properties_},
        {"browserRoot", browser_root_},
        {"browserObject", browser_object_},
        {"propertyBrowser", property_browser_.view_state()},
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
    if (!layout_.snapshot().empty())
        result["layout"] = layout_.snapshot();
    if (!collisions_.empty())
        result["collisions"] = collisions_.view_state();
    if (!skeleton_.empty())
        result["skeleton"] = skeleton_.view_state();
    if(!animations_.empty())result["animations"]=animations_.view_state();
    if(sockets_bound_){result["animationSockets"]=animation_sockets_.view_state();result["animationSocketsPreview"]=sockets_preview_;}
    if(pose_bound_){result["animationPose"]=animation_pose_.view_state();result["animationPosePreview"]=pose_preview_;}
    if(!curves_.empty())result["curves"]=curves_.view_state();
    if ((before_geometry_ && before_geometry_->skin) || (after_geometry_ && after_geometry_->skin)) {
        result["meshSkin"] = skin_.view_state();
        result["meshSkinInspection"] = skin_inspection_;
    }
    if ((before_geometry_ && before_geometry_->morphs) || (after_geometry_ && after_geometry_->morphs)) {
        result["meshMorphs"] = morphs_.view_state();
        result["meshMorphInspection"] = morph_inspection_;
    }
    result["before"] = side(false);
    result["after"] = side(true);
    return result;
}
void AssetView::restore_view_state(const Json &state) {
    cyuediff::view_state::validate(state, "asset");
    if (state.contains("meshMorphs"))
        morphs_.restore_view_state(state["meshMorphs"]);
    if (state.contains("meshMorphInspection"))
        morph_inspection_ = state["meshMorphInspection"].get<bool>();
    if (state.contains("meshSkin")) skin_.restore_view_state(state["meshSkin"]);
    if (state.contains("meshSkinInspection")) skin_inspection_ = state["meshSkinInspection"].get<bool>();
    if (morph_inspection_)
        skin_inspection_ = false;
    if (state.contains("layout"))
        layout_.restore(state["layout"], "asset-layout");
    if (state.contains("collisions"))
        collisions_.restore_view_state(state["collisions"]);
    if (state.contains("skeleton"))
        skeleton_.restore_view_state(state["skeleton"]);
    if(state.contains("animations"))animations_.restore_view_state(state["animations"]);
    if(state.contains("animationSockets"))animation_sockets_.restore_view_state(state["animationSockets"]);
    if(state.contains("animationSocketsPreview"))sockets_preview_=sockets_bound_ && state["animationSocketsPreview"].get<bool>();
    if(state.contains("animationPose"))animation_pose_.restore_view_state(state["animationPose"]);
    if(state.contains("animationPosePreview"))pose_preview_=pose_bound_ && state["animationPosePreview"].get<bool>();
    if(state.contains("curves"))curves_.restore_view_state(state["curves"]);
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
    const auto &selected_package = properties_after_ ? after_ : before_;
    if (!selected_package || selected_ >= selected_package->exports.size())
        selected_ = 0;
    const auto &selected_textures = texture_after_ ? after_textures_ : before_textures_;
    if (texture_selected_ >= selected_textures.size())
        texture_selected_ = 0;
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
    if (state.contains("browserRoot"))
        browser_root_ = state.at("browserRoot").get<int>();
    bind_property_browser();
    if (state.contains("browseProperties"))
        browse_properties_ = state.at("browseProperties").get<bool>();
    if (state.contains("browserObject") && state.at("browserObject") == browser_object_ &&
        state.contains("propertyBrowser")) {
        const auto input = property_browser_inputs();
        property_browser_.restore_view_state(input.before, input.after, state.at("propertyBrowser"));
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
            else if (layout_.begin_table("##inspection", "objectColumns", 2)) {
                layout_.column("##objects", 0, 1);
                layout_.column("##properties", 1, 2);
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
                                          ImGuiWindowFlags_HorizontalScrollbar)) {
                        if (inspection[selected_].empty())
                            ImGui::TextWrapped("%s",
                                               cyui::tr("Raw JSON exceeds the display limits. Use scoped "
                                                        "properties or export JSON with the CLI.")
                                                   .c_str());
                        else
                            ImGui::TextUnformatted(inspection[selected_].c_str());
                    }
                    ImGui::EndChild();
                } else {
                    bind_property_browser();
                    ImGui::Checkbox(cyui::trLabel("Browse by level").c_str(), &browse_properties_);
                    if (browse_properties_) {
                        const auto stored = cyui::tr("Stored properties"), native = cyui::tr("Native fields"),
                                   opaque = cyui::tr("Opaque metadata");
                        const char *roots[] = {stored.c_str(), native.c_str(), opaque.c_str()};
                        if (ImGui::Combo("##property-browser-root", &browser_root_, roots, 3))
                            bind_property_browser();
                        const auto input = property_browser_inputs();
                        if (input.unknown)
                            cyui::callout(
                                cyui::Tone::info,
                                cyui::tr("Some values are unknown; inspecting one snapshot.").c_str());
                        property_browser_.draw(input.before, input.after);
                    } else
                        (properties_after_ ? after_inspection_ : before_inspection_)[selected_].draw();
                }
                layout_.end_table();
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
            const bool has_skin = (before_geometry_ && before_geometry_->skin) || (after_geometry_ && after_geometry_->skin);
            const bool has_morphs = (before_geometry_ && before_geometry_->morphs) ||
                                    (after_geometry_ && after_geometry_->morphs);
            if (has_skin &&
                ImGui::Checkbox(cyui::trLabel("Inspect source skinning").c_str(), &skin_inspection_) &&
                skin_inspection_)
                morph_inspection_ = false;
            if (has_morphs &&
                ImGui::Checkbox(cyui::trLabel("Inspect morph targets").c_str(), &morph_inspection_) &&
                morph_inspection_)
                skin_inspection_ = false;
            if (has_morphs && morph_inspection_) {
                cyui::textDim("%s",
                              cyui::tr("Stored position and split-normal deltas in source space").c_str());
                morphs_.draw("##mesh-morph-inspection");
            } else if (has_skin && skin_inspection_) {
                cyui::textDim("%s", cyui::tr("Stored bone poses and raw weights (0..65535)").c_str());
                skin_.draw("##mesh-skin-inspection");
            } else
                (mesh_after_ ? after_mesh_ : before_mesh_).draw();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(cyui::trLabel("Curves").c_str(),nullptr,flags(AssetTab::curves))){
            active_tab_=AssetTab::curves;curves_.draw();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(cyui::trLabel("Animations").c_str(),nullptr,flags(AssetTab::animations))){
            active_tab_=AssetTab::animations;
            if(pose_bound_)ImGui::Checkbox(cyui::trLabel("Show source hierarchy pose").c_str(),&pose_preview_);
            if(pose_bound_ && pose_preview_) {
                if(sockets_bound_)ImGui::Checkbox(cyui::trLabel("Show source socket origins").c_str(),&sockets_preview_);
                if(sockets_bound_ && sockets_preview_)animation_sockets_.draw();else animation_pose_.draw();
            }else animations_.draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(cyui::trLabel("Skeleton").c_str(), nullptr, flags(AssetTab::skeleton))) {
            active_tab_ = AssetTab::skeleton;
            skeleton_.draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(cyui::trLabel("Collisions").c_str(), nullptr, flags(AssetTab::collisions))) {
            active_tab_ = AssetTab::collisions;
            collisions_.draw();
            ImGui::EndTabItem();
        }
        if (comparison_ &&
            ImGui::BeginTabItem(cyui::trLabel("Comparison").c_str(), nullptr, flags(AssetTab::comparison))) {
            active_tab_ = AssetTab::comparison;
            if (text_comparison_available_)
                cyui::diffViewer("##semantic-diff", diff_state_, document_, ImGui::GetContentRegionAvail());
            else
                ImGui::TextWrapped("%s", cyui::tr("Whole-document text exceeds the display limits. Use "
                                                  "scoped properties or export JSON with the CLI.")
                                             .c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
} // namespace cyuediff
