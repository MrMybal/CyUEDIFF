#include "container_view.hpp"
#include <algorithm>
#include <map>
namespace cyuediff::desktop {
namespace {
const char *state_label(const std::string &state) {
    if (state == "added")
        return "Added";
    if (state == "removed")
        return "Removed";
    if (state == "modified")
        return "Modified";
    if (state == "unchanged")
        return "Unchanged";
    return "Unchecked";
}
cyui::Tone state_tone(const std::string &state) {
    if (state == "added")
        return cyui::Tone::success;
    if (state == "removed")
        return cyui::Tone::error;
    if (state == "modified")
        return cyui::Tone::warning;
    return cyui::Tone::neutral;
}
} // namespace
void ContainerView::register_translations() {
    CookedAnimationView::register_translations();
    NaniteView::register_translations();
    MeshSkinView::register_translations();
    RenderMorphView::register_translations();
    ClothView::register_translations();
    PhysicalClothView::register_translations();
    cyui::registerTranslations(
        "fr", {{"Physical cloth settings", "Réglages du tissu physique"},
               {"Before physical cloth profile", "Profil du tissu physique avant"},
               {"After physical cloth profile", "Profil du tissu physique après"},
               {"Before physical cloth object", "Objet de tissu physique avant"},
               {"After physical cloth object", "Objet de tissu physique après"},
               {"Compare selected physical objects", "Comparer les objets physiques sélectionnés"}});
    VertexAttributeView::register_translations();
    PropertyBrowserView::register_translations();
    cyui::registerTranslations("fr", {{"Browse by level", "Parcourir par niveau"}});
    cyui::registerTranslations(
        "fr",
        {{"Nanite resource unavailable", "Ressource Nanite indisponible"},
         {"Before Nanite resource", "Ressource Nanite avant"},
         {"After Nanite resource", "Ressource Nanite après"},
         {"Cooked Nanite settings", "Réglages Nanite cooked"},
         {"Before Nanite profile", "Profil Nanite avant"},
         {"After Nanite profile", "Profil Nanite après"},
         {"Decode Nanite attributes", "Décoder les attributs Nanite"},
         {"Decode after Nanite attributes", "Décoder les attributs Nanite après"},
         {"Curve settings TOC before", "TOC des réglages de courbes avant"},
         {"Curve settings TOC after", "TOC des réglages de courbes après"},
         {"Curve settings member before", "Membre des réglages de courbes avant"},
         {"Curve settings member after", "Membre des réglages de courbes après"},
         {"Cooked animation settings", "Réglages des animations cooked"},
         {"Before animation", "Animation avant"},
         {"After animation", "Animation après"},
         {"Before interpolation", "Interpolation avant"},
         {"After interpolation", "Interpolation après"},
         {"Linear", "Linéaire"},
         {"Step", "Palier"},
         {"Before settings TOC", "TOC des réglages avant"},
         {"After settings TOC", "TOC des réglages après"},
         {"Before settings member", "Membre des réglages avant"},
         {"After settings member", "Membre des réglages après"},
         {"Before Skeleton TOC", "TOC du Skeleton avant"},
         {"After Skeleton TOC", "TOC du Skeleton après"},
         {"Before Skeleton member", "Membre du Skeleton avant"},
         {"After Skeleton member", "Membre du Skeleton après"},
         {"Before member ID or path", "ID ou chemin du membre avant"},
         {"After member override", "Membre après distinct"},
         {"Dependency member IDs are explicit. An empty dependency TOC uses that side's main container. "
          "Interpolation is required only when omitted from stored properties.",
          "Les ID des dépendances sont explicites. Un TOC de dépendance vide utilise le conteneur "
          "principal "
          "du même côté. L’interpolation est requise uniquement si les propriétés enregistrées "
          "l’omettent."},
         {"Cooked animations", "Animations cooked"},
         {"Before animation object", "Objet animation avant"},
         {"After animation object", "Objet animation après"},
         {"Animation object unavailable", "Objet animation indisponible"},
         {"Peer mesh unavailable; changes cannot be classified.",
          "Mesh opposé indisponible ; les changements ne peuvent pas être classés."},
         {"cooked-skeletal-lod", "Lecture des LOD squelettiques cooked"},
         {"cooked-skeletal-vertices", "Lecture des sommets squelettiques"},
         {"cooked-skeletal-weights", "Lecture des poids squelettiques"},
         {"Cooked skeletal settings", "Réglages squelettiques cooked"},
         {"Before skeletal", "Squelettique avant"},
         {"After skeletal", "Squelettique après"},
         {"Vertex colors", "Couleurs des sommets"},
         {"After vertex colors", "Couleurs des sommets après"},
         {"Use the skeletal producer mobile-LOD setting; omitted colors need an explicit value.",
          "Utilisez le réglage de LOD mobile du producteur squelettique ; les couleurs omises "
          "nécessitent une valeur explicite."},
         {"Cooked meshes", "Meshes cooked"},
         {"Native physics", "Physique native"},
         {"Cooked physics settings", "Réglages de physique cooked"},
         {"Before physics", "Physique avant"},
         {"After physics", "Physique après"},
         {"Decoded native Chaos geometry in cooker coordinates; no simulation.",
          "Géométrie Chaos native décodée dans les coordonnées de cuisson ; aucune simulation."},
         {"Preparing native physics geometry", "Préparation de la géométrie physique native"},
         {"cooked-physics-body", "Lecture des corps physiques cooked"},
         {"Cooked collisions", "Collisions cooked"},
         {"Stored collision shapes; native physics data is not decoded.",
          "Formes de collision enregistrées ; les données physiques natives ne sont pas décodées."},
         {"Projecting cooked collision shapes", "Projection des formes de collision cooked"},
         {"Projecting stored collision shapes", "Projection des formes de collision enregistrées"},
         {"collision-wireframe", "Préparation des contours de collision"},
         {"Cooked mesh settings", "Réglages des meshes cooked"},
         {"Before mesh", "Mesh avant"},
         {"After mesh", "Mesh après"},
         {"Mobile min LOD", "LOD mobile minimal"},
         {"After mobile min LOD", "LOD mobile minimal après"},
         {"Unknown", "Inconnu"},
         {"Off", "Désactivé"},
         {"On", "Activé"},
         {"Use the producer r.StaticMesh.KeepMobileMinLODSettingOnDesktop value.",
          "Utilisez la valeur r.StaticMesh.KeepMobileMinLODSettingOnDesktop du producteur."},
         {"Stored render LOD geometry.", "Géométrie des LOD de rendu enregistrés."},
         {"Cooked pixels", "Pixels cooked"},
         {"Stored channels; no color transform. HDR previews use Reinhard / sRGB at 0 EV.",
          "Canaux stockés ; aucune transformation de couleur. Aperçus HDR : Reinhard / sRGB à 0 EV."},
         {"Only explicitly supported native profiles are decoded.",
          "Seuls les profils natifs explicitement pris en charge sont décodés."},
         {"Schema context", "Contexte de schéma"},
         {"Schema contexts (optional)", "Contextes de schéma (facultatifs)"},
         {"Before schema", "Schéma avant"},
         {"After schema", "Schéma après"},
         {"Choose before schema...", "Choisir le schéma avant..."},
         {"Choose after schema...", "Choisir le schéma après..."},
         {"Cooked schema context (*.json)", "Contexte de schéma cooked (*.json)"},
         {"Choose schema context", "Choisir un contexte de schéma"},
         {"Object details", "Détails de l'objet"},
         {"Decoded properties", "Propriétés décodées"},
         {"No decoded properties", "Aucune propriété décodée"},
         {"Supply a matching schema context and inspect the member.",
          "Fournissez un contexte de schéma compatible et inspectez le membre."},
         {"Before properties only; after values are unknown.",
          "Propriétés avant uniquement ; valeurs après inconnues."},
         {"After properties only; before values are unknown.",
          "Propriétés après uniquement ; valeurs avant inconnues."},
         {"Explicit mappings do not verify cooked build compatibility or resolve defaults.",
          "Les mappings explicites ne valident pas la compatibilité du build cooked ni les valeurs par "
          "défaut."},
         {"Cooked interpretation context differs.", "Le contexte d'interprétation cooked diffère."},
         {"Object comparison requires explicit profiles on both present members.",
          "La comparaison d'objets requiert un profil explicite pour chaque membre présent."},
         {"schema-context", "Lecture du contexte de schéma"}});

    cyui::registerTranslations("en", {{"schema-context", "Reading schema context"},
                                      {"container-diff", "Comparing decoded members"},
                                      {"container-index", "Reading container index"},
                                      {"container-read", "Reading container member"},
                                      {"cooked-physics-body", "Reading cooked physics bodies"},
                                      {"cooked-script-objects", "Reading script objects"},
                                      {"cooked-package", "Reading cooked objects"}});
    cyui::registerTranslations(
        "fr",
        {{"After container", "Conteneur après"},
         {"After cooked profile", "Profil cooked après"},
         {"After global script TOC", "TOC script global après"},
         {"Compare all decoded members", "Comparer tous les membres décodés"},
         {"Up to 512 MiB total; 64 MiB per member.", "Jusqu'à 512 Mio au total ; 64 Mio par membre."},
         {"Index comparison", "Indexer la comparaison"},
         {"Unchecked", "À vérifier"},
         {"Added", "Ajouté"},
         {"Removed", "Supprimé"},
         {"Modified", "Modifié"},
         {"Unchanged", "Inchangé"},
         {"Hide unchanged", "Masquer les inchangés"},
         {"Common members remain unchecked until decoded.", "Les membres communs restent à vérifier jusqu'au "
                                                            "décodage."},
         {"Decoded comparison complete.", "Comparaison des octets décodés terminée."},
         {"Before container", "Conteneur avant"},
         {"Comparison", "Comparaison"},
         {"container-diff", "Comparaison des membres décodés"},
         {"Containers", "Conteneurs"},
         {"Open container...", "Ouvrir un conteneur..."},
         {"Unreal container (*.pak;*.utoc)", "Conteneur Unreal (*.pak;*.utoc)"},
         {"Choose container", "Choisir un conteneur"},
         {"Container file", "Fichier conteneur"},
         {"Cooked profile", "Profil cooked"},
         {"Byte inspection", "Inspection des octets"},
         {"Global script TOC", "TOC script global"},
         {"Codec module", "Module codec"},
         {"Index container", "Indexer le conteneur"},
         {"Container contents", "Contenu du conteneur"},
         {"Indexing container", "Indexation du conteneur"},
         {"container-read", "Lecture du membre"},
         {"container-index", "Lecture de l'index du conteneur"},
         {"cooked-script-objects", "Lecture des objets script"},
         {"cooked-package", "Lecture des objets cooked"},
         {"Members", "Membres"},
         {"Inspection", "Inspection"},
         {"Cooked objects", "Objets cooked"},
         {"Virtual path / chunk ID", "Chemin virtuel / ID de chunk"},
         {"Decoded bytes", "Octets décodés"},
         {"Chunk type", "Type de chunk"},
         {"State", "État"},
         {"Live", "Présent"},
         {"Deleted", "Supprimé"},
         {"Encrypted", "Chiffré"},
         {"Zen packages only", "Packages Zen uniquement"},
         {"Inspect selected member", "Inspecter le membre choisi"},
         {"Copy member ID", "Copier l'ID du membre"},
         {"%zu / %zu members", "%zu / %zu membres"},
         {"No matching member", "Aucun membre correspondant"},
         {"Change the search or filters, or show unchanged members.",
          "Modifiez la recherche ou les filtres, ou affichez les membres inchangés."},
         {"Select a member and inspect its decoded bytes.", "Choisissez un membre pour inspecter ses octets "
                                                            "décodés."},
         {"Choose an explicit UE profile for Zen object inspection.",
          "Choisissez un profil UE explicite pour inspecter les objets Zen."},
         {"Only explicitly supported native profiles are decoded.",
          "Seuls les profils natifs explicitement pris en charge sont décodés."},
         {"Select a cooked object.", "Choisissez un objet cooked."},
         {"Container diagnostics", "Diagnostics du conteneur"},
         {"Decoded bytes; package data is not interpreted for this member.", "Octets décodés "
                                                                             "; les données de package ne "
                                                                             "sont pas "
                                                                             "interprétées "
                                                                             "pour ce membre."},
         {"Choose a Pak or IoStore TOC, then index it.",
          "Choisissez un Pak ou un TOC IoStore, puis indexez-le."}});
}
void ContainerView::set_filter(std::string value) {
    filter_.query = std::move(value);
    rebuild_members();
}
void ContainerView::set_object_filter(std::string value) {
    object_filter_.query = std::move(value);
    rebuild_objects();
}
void ContainerView::set_packages_only(bool value) {
    packages_only_ = value;
    rebuild_members();
}
void ContainerView::set_changed_only(bool value) {
    changed_only_ = value;
    rebuild_members();
    rebuild_objects();
}
void ContainerView::rebuild_members() {
    members_.clear();
    for (std::size_t i = 0; i < member_rows_.size(); ++i) {
        const auto &row = member_rows_[i];
        const auto *entry = row.after ? row.after : row.before;
        if ((!packages_only_ || row.zen) && (!changed_only_ || row.state != "unchanged") &&
            filter_.matches(entry->path, entry->id + " " + row.state + (row.zen ? " Zen" : "")))
            members_.push_back(static_cast<int>(i));
    }
    if (std::find(members_.begin(), members_.end(), member_) == members_.end())
        member_ = -1;
}
void ContainerView::rebuild_objects() {
    objects_.clear();
    for (std::size_t i = 0; i < object_rows_.size(); ++i) {
        const auto &row = object_rows_[i];
        if ((!changed_only_ || row.state != "unchanged") && object_filter_.matches(row.path, row.search))
            objects_.push_back(static_cast<int>(i));
    }
    if (std::find(objects_.begin(), objects_.end(), object_) == objects_.end())
        object_ = -1;
}
bool ContainerView::select_member(const std::string &key) {
    for (auto i : members_) {
        const auto &row = member_rows_[i];
        auto matches = [&](const ContainerEntry *entry) {
            return entry && (entry->id == key || (!entry->path.empty() && entry->path == key));
        };
        if (row.key == key || matches(row.before) || matches(row.after)) {
            member_ = i;
            return true;
        }
    }
    return false;
}
const ContainerEntry *ContainerView::selected_member() const {
    if (member_ < 0)
        return nullptr;
    const auto &row = member_rows_[member_];
    return row.after ? row.after : row.before;
}
std::string ContainerView::selected_key() const {
    return member_ < 0 ? std::string{} : member_rows_[member_].key;
}
bool ContainerView::select_object(std::size_t index) {
    if (index >= object_rows_.size() ||
        std::find(objects_.begin(), objects_.end(), static_cast<int>(index)) == objects_.end())
        return false;
    object_ = static_cast<int>(index);
    const auto &row = object_rows_[index];
    object_data_ = row.before;
    after_object_data_ = row.after;
    if (document_->after && !compare_objects_)
        properties_.set_values(object_data_.is_null() ? &after_object_data_ : &object_data_, nullptr, false);
    else
        properties_.set_values(object_data_.is_null() ? nullptr : &object_data_,
                               after_object_data_.is_null() ? nullptr : &after_object_data_,
                               compare_objects_);
    property_available_ = !row.before_values.is_null() || !row.after_values.is_null();
    properties_comparing_ = compare_objects_ && (row.before.is_null() || !row.before_values.is_null()) &&
                            (row.after.is_null() || !row.after_values.is_null());
    const auto *before_read = row.before.is_null() ? nullptr : &row.before_read;
    const auto *after_read = row.after.is_null() ? nullptr : &row.after_read;
    const auto *single_read = !row.before_values.is_null() ? before_read : after_read;
    property_coverage_ = properties_comparing_ ? cooked_property_coverage(before_read, after_read)
                                               : cooked_property_coverage(single_read, single_read);
    if (properties_comparing_)
        values_.set_values(row.before_values.is_null() ? nullptr : &row.before_values,
                           row.after_values.is_null() ? nullptr : &row.after_values, true,
                           property_coverage_);
    else
        values_.set_values(!row.before_values.is_null()
                               ? &row.before_values
                               : (!row.after_values.is_null() ? &row.after_values : nullptr),
                           nullptr, false, property_coverage_);
    const auto snapshots = property_snapshots();
    property_browser_.set_values(snapshots.first, snapshots.second, properties_comparing_,
                                 property_coverage_);
    browse_properties_ = values_.table().truncated;
    property_notice_.clear();
    if (document_->after && property_available_ && !properties_comparing_)
        property_notice_ = !row.before_values.is_null() ? "Before properties only; after values are unknown."
                                                        : "After properties only; before values are unknown.";
    property_mode_ = property_available_;
    auto read_metadata = [](Json read) {
        if (read.is_null())
            return Json{{"status", "unbound"}};
        if (read.contains("properties")) {
            read["propertyCount"] = read.at("properties").size();
            read.erase("properties");
        }
        return read;
    };
    schema_details_["selectedObject"] = {{"path", row.path},
                                         {"before", read_metadata(row.before_read)},
                                         {"after", read_metadata(row.after_read)},
                                         {"propertiesComparable", properties_comparing_}};
    schema_.set_values(&schema_details_, nullptr, false);
    return true;
}
void ContainerView::apply_object_selection() {
    if (pending_object_ >= 0)
        select_object(static_cast<std::size_t>(pending_object_));
    pending_object_ = -1;
}
void ContainerView::set_document(std::shared_ptr<const ContainerDocument> value) {
    layout_.restore(Json::object(), "container-layout");
    document_ = std::move(value);
    member_ = object_ = pending_object_ = -1;
    compare_objects_ = false;
    property_available_ = properties_comparing_ = property_mode_ = false;
    property_notice_.clear();
    object_data_ = after_object_data_ = schema_details_ = nullptr;
    values_ = PropertyView{};
    property_browser_ = PropertyBrowserView{};
    browse_properties_ = false;
    properties_ = PropertyView{};
    schema_ = PropertyView{};
    member_rows_.clear();
    object_rows_.clear();
    physics_present_ = false;
    physics_.set_models(nullptr, nullptr, {}, {});
    collisions_present_ = false;
    collisions_.set_models(nullptr, nullptr, {}, {});
    physical_cloth_.set_assets(nullptr);
    physical_cloth_present_ = physical_pair_explicit_ = false;
    before_physical_cloth_ = after_physical_cloth_ = -1;
    nanite_.set_resources(nullptr);
    nanite_present_ = false;
    before_nanite_ = after_nanite_ = -1;
    animation_.set_animations(nullptr);
    animation_present_ = false;
    before_animation_ = after_animation_ = -1;
    mesh_rows_.clear();
    mesh_ = 0;
    mesh_skin_ = false;
    mesh_morphs_ = false;
    mesh_cloth_ = false;
    mesh_attributes_ = false;
    pixel_rows_.clear();
    pixel_ = 0;
    diagnostics_.clear();
    if (document_) {
        auto is_zen = [](const ContainerDocument &side, const ContainerEntry *entry) {
            return entry && side.metadata["kind"] == "iostore" && entry->chunk_type == 1;
        };
        if (document_->after) {
            for (std::size_t i = 0; i < document_->members.size(); ++i) {
                const auto &pair = document_->members[i];
                const auto *b = pair.before ? &document_->input->entries()[*pair.before] : nullptr;
                const auto *a = pair.after ? &document_->after->input->entries()[*pair.after] : nullptr;
                member_rows_.push_back({b, a, pair.key, document_->member_states[i],
                                        is_zen(*document_, b) || is_zen(*document_->after, a)});
            }
        } else {
            for (const auto &entry : document_->input->entries())
                member_rows_.push_back({&entry, nullptr, entry.id, "", is_zen(*document_, &entry)});
        }
        auto diagnostics = [&](const ContainerDocument &side) {
            for (const auto &text : side.metadata.at("diagnostics"))
                diagnostics_.push_back(text.get<std::string>());
            if (side.zen)
                diagnostics_.insert(diagnostics_.end(), side.zen->diagnostics.begin(),
                                    side.zen->diagnostics.end());
        };
        diagnostics(*document_);
        auto add_pixels = [&](const ContainerDocument &side, bool after) {
            for (const auto &preview : side.previews) {
                PixelRow row;
                row.key = preview.key;
                row.after = after;
                TextureSource metadata;
                metadata.object_path = preview.object_path;
                metadata.width = preview.image.width;
                metadata.height = preview.image.height;
                metadata.format = preview.format;
                metadata.compression = "mip " + std::to_string(preview.mip);
                row.view.set_source(std::move(metadata));
                pixel_rows_.push_back(std::move(row));
            }
            diagnostics_.insert(diagnostics_.end(), side.texture_diagnostics.begin(),
                                side.texture_diagnostics.end());
        };
        auto add_meshes = [&](const ContainerDocument &side, bool after) {
            const auto peer = after ? document_.get() : document_->after.get();
            for (const auto &preview : side.mesh_previews) {
                std::shared_ptr<const MeshGeometry> other;
                if (const auto *paired = paired_mesh_preview(*document_, preview, after))
                    other = paired->geometry;
                MeshRow row;
                row.key = preview.key;
                row.after = after;
                row.comparison_known = !peer || other || peer->inspection.is_null();
                row.view.set_mesh(preview.geometry, other, after, peer != nullptr && row.comparison_known);
                const auto own_skin = preview.geometry->skin, peer_skin = other ? other->skin : nullptr;
                row.skin_available = own_skin || peer_skin;
                if (row.skin_available)
                    row.skin.set_skins(after ? peer_skin : own_skin, after ? own_skin : peer_skin,
                                       peer != nullptr && row.comparison_known);

                const auto own_morphs = preview.geometry->render_morphs,
                           peer_morphs = other ? other->render_morphs : nullptr;
                row.morphs_available = own_morphs || peer_morphs;
                if (row.morphs_available)
                    row.morphs.set_models(after ? peer_morphs : own_morphs, after ? own_morphs : peer_morphs,
                                          peer != nullptr && row.comparison_known);
                const auto own_cloth = preview.geometry->cloth, peer_cloth = other ? other->cloth : nullptr;
                row.cloth_available = own_cloth || peer_cloth;
                if (row.cloth_available)
                    row.cloth.set_cloths(after ? peer_cloth : own_cloth, after ? own_cloth : peer_cloth,
                                         peer != nullptr && row.comparison_known);
                const auto own_attributes = preview.geometry->vertex_attributes,
                           peer_attributes = other ? other->vertex_attributes : nullptr;
                row.attributes_available = (own_attributes && !own_attributes->channels.empty()) ||
                                           (peer_attributes && !peer_attributes->channels.empty());
                if (row.attributes_available)
                    row.attributes.set_attributes(after ? peer_attributes : own_attributes,
                                                  after ? own_attributes : peer_attributes,
                                                  peer != nullptr && row.comparison_known);
                mesh_rows_.push_back(std::move(row));
            }
            diagnostics_.insert(diagnostics_.end(), side.mesh_diagnostics.begin(),
                                side.mesh_diagnostics.end());
        };
        physics_present_ =
            (!document_->physics_model.is_null() && !document_->physics_model.at("bodies").empty()) ||
            (document_->after && !document_->after->physics_model.is_null() &&
             !document_->after->physics_model.at("bodies").empty());
        if (physics_present_) {
            auto model = [](const ContainerDocument &side) -> Json {
                if (!side.physics_model.is_null())
                    return side.physics_model;
                if (side.inspection.is_null())
                    return nullptr; // The selected member is absent.
                return {{"schema", "cyuediff.collision/1"}, {"complete", false},
                        {"bodyTableKnown", false},          {"bodies", Json::object()},
                        {"owners", Json::object()},         {"provenance", {{"nativePhysicsDecoded", true}}}};
            };
            physics_.set_models(model(*document_),
                                document_->after ? model(*document_->after) : Json(nullptr),
                                document_->physics_wire,
                                document_->after ? document_->after->physics_wire : CollisionWireframe{},
                                document_->after != nullptr);
        }
        diagnostics_.insert(diagnostics_.end(), document_->physics_diagnostics.begin(),
                            document_->physics_diagnostics.end());
        if (document_->after)
            diagnostics_.insert(diagnostics_.end(), document_->after->physics_diagnostics.begin(),
                                document_->after->physics_diagnostics.end());
        collisions_present_ =
            !document_->collisions.is_null() || (document_->after && !document_->after->collisions.is_null());
        if (collisions_present_) {
            auto known = [](const ContainerDocument &side) {
                return side.inspection.is_null() || (side.zen && !side.zen->script_objects_sha256.empty());
            };
            const bool comparing = document_->after && known(*document_) && known(*document_->after);
            collisions_.set_models(
                document_->collisions, document_->after ? document_->after->collisions : Json(nullptr),
                document_->collision_wire,
                document_->after ? document_->after->collision_wire : CollisionWireframe{}, comparing);
        }
        diagnostics_.insert(diagnostics_.end(), document_->collision_diagnostics.begin(),
                            document_->collision_diagnostics.end());
        if (document_->after)
            diagnostics_.insert(diagnostics_.end(), document_->after->collision_diagnostics.begin(),
                                document_->after->collision_diagnostics.end());
        add_meshes(*document_, false);
        if (document_->after)
            add_meshes(*document_->after, true);
        add_pixels(*document_, false);
        if (document_->after)
            add_pixels(*document_->after, true);
        if (document_->after)
            diagnostics(*document_->after);
        if (document_->after) {
            std::map<std::string, ObjectRow> pairs;
            auto add = [&](const std::optional<ZenPackage> &package, bool right) {
                if (!package)
                    return;
                const auto objects = package->snapshot().at("objects");
                for (const auto &entry : package->exports) {
                    auto &row = pairs[entry.path];
                    row.path = entry.path;
                    row.name = entry.name;
                    row.search += " " + entry.class_name;
                    (right ? row.after : row.before) = objects.at(entry.path);
                    (right ? row.after_read : row.before_read) = entry.property_read;
                }
            };
            add(document_->zen, false);
            add(document_->after->zen, true);
            // A non-decoded peer is not evidence that an export was removed or added.
            const bool complete = (document_->inspection.is_null() || document_->zen) &&
                                  (document_->after->inspection.is_null() || document_->after->zen);
            compare_objects_ = complete;
            for (auto &item : pairs) {
                auto &row = item.second;
                row.state = !complete ? "unchecked"
                                      : (row.before.is_null()
                                             ? "added"
                                             : (row.after.is_null()
                                                    ? "removed"
                                                    : (row.before == row.after ? "unchanged" : "modified")));
                if (!row.before_read.is_null() && row.before_read.at("status") == "decoded")
                    row.before_values = row.before_read.at("properties");
                if (!row.after_read.is_null() && row.after_read.at("status") == "decoded")
                    row.after_values = row.after_read.at("properties");
                // Do not display an undecoded peer as removed named properties.
                if (!row.before.is_null()) {
                    row.before.erase("properties");
                    row.before.erase("propertyRead");
                }
                if (!row.after.is_null()) {
                    row.after.erase("properties");
                    row.after.erase("propertyRead");
                }
                if (complete && !row.before.is_null() && !row.after.is_null()) {
                    std::string base = "/objects/";
                    for (char c : row.path)
                        base += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
                    bool changed = row.before != row.after, unknown = false;
                    const auto report = document_->selected_comparison.find("zen");
                    if (report != document_->selected_comparison.end() && report->is_object()) {
                        auto belongs = [&](const Json &entry) {
                            const auto &path = entry.at("path").get_ref<const std::string &>();
                            return path == base ||
                                   (path.size() > base.size() && path.compare(0, base.size(), base) == 0 &&
                                    path[base.size()] == '/');
                        };
                        for (const auto &entry : report->at("changes"))
                            changed = belongs(entry) || changed;
                        if (report->contains("unavailableScopes"))
                            for (const auto &entry : report->at("unavailableScopes"))
                                unknown = belongs(entry) || unknown;
                    }
                    row.state = unknown ? "unchecked" : changed ? "modified" : "unchanged";
                }
                row.search += " " + row.state;
                object_rows_.push_back(std::move(row));
            }
        } else if (document_->zen) {
            for (const auto &entry : document_->zen->exports) {
                ObjectRow row;
                row.name = entry.name;
                row.path = entry.path;
                row.search = entry.class_name;
                row.before = entry.to_json();
                row.before.erase("propertyRead");
                row.before_read = entry.property_read;
                if (!row.before_read.is_null() && row.before_read.at("status") == "decoded")
                    row.before_values = row.before_read.at("properties");
                object_rows_.push_back(std::move(row));
            }
        }
        auto schema_source = [](const ContainerDocument &side) -> Json {
            if (!side.schema_manifest)
                return nullptr;
            auto result = side.schema_manifest->to_json();
            result["mappingLoaded"] = side.schema_loaded;
            result["contextSha256"] = side.zen ? side.zen->schema_context_sha256 : std::string{};
            return result;
        };
        schema_details_ = {{"before", schema_source(*document_)},
                           {"after", document_->after ? schema_source(*document_->after) : Json(nullptr)}};
        schema_.set_values(&schema_details_, nullptr, false);
        rebuild_members();
        rebuild_objects();
        if (!select_member(document_->source.entry) && !members_.empty())
            member_ = members_.front();
        const auto *before = document_->inspection.is_null() ? &document_->metadata : &document_->inspection;
        const Json *after = nullptr;
        if (document_->after) {
            before =
                document_->inspection.is_null() && !document_->after->inspection.is_null() ? nullptr : before;
            after = document_->after->inspection.is_null()
                        ? (document_->inspection.is_null() ? &document_->after->metadata : nullptr)
                        : &document_->after->inspection;
        }
        inspection_.set_values(before, after, bool(document_->after));
        const auto *comparison = document_->selected_comparison.is_null() ? &document_->comparison
                                                                          : &document_->selected_comparison;
        comparison_.set_values(comparison, nullptr, false);
        if (!objects_.empty())
            select_object(static_cast<std::size_t>(objects_.front()));
        initial_tab_ = !object_rows_.empty()
                           ? 2
                           : ((!document_->inspection.is_null() ||
                               (document_->after && !document_->after->inspection.is_null()))
                                  ? 1
                                  : 0);
        if (!pixel_rows_.empty())
            initial_tab_ = 5;
        if (!mesh_rows_.empty())
            initial_tab_ = 6;
        before_animation_ = document_->animation_previews.empty() ? -1 : 0;
        after_animation_ = document_->after && !document_->after->animation_previews.empty() ? 0 : -1;
        animation_present_ = before_animation_ >= 0 || after_animation_ >= 0 ||
                             !document_->animation_diagnostics.empty() ||
                             (document_->after && !document_->after->animation_diagnostics.empty());
        bind_animation();
        if (animation_present_)
            initial_tab_ = 9;
        before_nanite_ = document_->nanite_previews.empty() ? -1 : 0;
        after_nanite_ = document_->after && !document_->after->nanite_previews.empty() ? 0 : -1;
        nanite_present_ =
            before_nanite_ >= 0 || after_nanite_ >= 0 ||
            document_->source.nanite_profile != NaniteProfile::unspecified ||
            (document_->after && document_->after->source.nanite_profile != NaniteProfile::unspecified);
        bind_nanite();
        if (nanite_present_)
            initial_tab_ = 10;
        before_physical_cloth_ = document_->physical_cloth_previews.size() == 1 ? 0 : -1;
        after_physical_cloth_ =
            document_->after && document_->after->physical_cloth_previews.size() == 1 ? 0 : -1;
        physical_cloth_present_ =
            document_->source.physical_cloth_profile != PhysicalClothProfile::unspecified ||
            (document_->after &&
             document_->after->source.physical_cloth_profile != PhysicalClothProfile::unspecified);
        bind_physical_cloth();
        if (physical_cloth_present_)
            initial_tab_ = 11;
        select_tab_ = true;
    } else {
        members_.clear();
        objects_.clear();
    }
}

bool ContainerView::select_tab(int tab) {
    if (!document_ || tab < 0 || tab > 11 || (tab == 11 && !physical_cloth_present_) ||
        (tab == 10 && !nanite_present_) || (tab == 9 && !animation_present_) ||
        (tab == 8 && !physics_present_) || (tab == 7 && !collisions_present_) ||
        (tab == 6 && mesh_rows_.empty()) || (tab == 5 && pixel_rows_.empty()) ||
        (tab == 2 && object_rows_.empty()) || (tab == 3 && !document_->after) ||
        (tab == 4 && !document_->schema_manifest &&
         (!document_->after || !document_->after->schema_manifest)))
        return false;
    initial_tab_ = tab;
    select_tab_ = true;
    return true;
}
bool ContainerView::set_pixel_preview(const std::string &key, bool after, const cyui::ImageData &image) {
    for (auto &row : pixel_rows_)
        if (row.key == key && row.after == after)
            return row.view.set_preview(image);
    return false;
}
bool ContainerView::set_pixel_difference(const ContainerTextureDifference &difference,
                                         const cyui::ImageData &image) {
    bool found = false;
    for (auto &row : pixel_rows_)
        if (row.key == difference.key) {
            found = (difference.hdr ? row.view.set_difference(image, *difference.hdr)
                                    : row.view.set_difference(image, difference.comparison)) ||
                    found;
            if (!difference.comparison.changed_pixels)
                row.view.restore_view_state({{"difference", false}});
        }
    return found;
}
Json ContainerView::mesh_report() const {
    Json rows = Json::array();
    for (const auto &r : mesh_rows_)
        rows.push_back({{"key", r.key},
                        {"after", r.after},
                        {"changes", Json{{"unchanged", r.view.changes().unchanged},
                                         {"added", r.view.changes().added},
                                         {"removed", r.view.changes().removed},
                                         {"modified", r.view.changes().modified}}},
                        {"edges", r.view.visible_edge_count()},
                        {"comparisonKnown", r.comparison_known},
                        {"skinAvailable", r.skin_available},
                        {"skin", r.skin_available ? r.skin.report() : Json(nullptr)},
                        {"morphsAvailable", r.morphs_available},
                        {"morphs", r.morphs_available ? r.morphs.report() : Json(nullptr)},
                        {"clothAvailable", r.cloth_available},
                        {"cloth", r.cloth_available ? r.cloth.report() : Json(nullptr)},
                        {"attributesAvailable", r.attributes_available},
                        {"attributes", r.attributes_available ? r.attributes.report() : Json(nullptr)}});
    return rows;
}
std::pair<const Json *, const Json *> ContainerView::property_snapshots() const {
    if (object_ < 0)
        return {nullptr, nullptr};
    const auto &row = object_rows_[object_];
    const auto a = row.before_values.is_null() ? nullptr : &row.before_values;
    const auto b = row.after_values.is_null() ? nullptr : &row.after_values;
    return properties_comparing_ ? std::make_pair(a, b)
                                 : std::make_pair(a ? a : b, static_cast<const Json *>(nullptr));
}
bool ContainerView::set_property_scope(const std::string &scope, std::size_t first) {
    const auto snapshots = property_snapshots();
    if (!property_available_ || !property_browser_.navigate(snapshots.first, snapshots.second, scope, first))
        return false;
    browse_properties_ = true;
    return true;
}
Json ContainerView::property_report() const {
    auto status = [](const Json &read) -> Json {
        return read.is_null() ? Json("unbound") : read.at("status");
    };
    const auto *row = object_ >= 0 ? &object_rows_[object_] : nullptr;
    Json result = {
        {"available", row && property_available_},
        {"comparing", row && properties_comparing_},
        {"mode", property_mode_ ? "properties" : "object"},
        {"beforeStatus", row ? status(row->before_read) : Json(nullptr)},
        {"afterStatus", row ? status(row->after_read) : Json(nullptr)},
        {"visibleProperties",
         row ? (browse_properties_ ? property_browser_.properties().visible_count() : values_.visible_count())
             : 0},
        {"displayTruncated", row && property_table().truncated},
        {"browsing", browse_properties_},
        {"comparisonUnavailable", property_coverage_.comparison_unavailable},
        {"cookedMetadata", property_coverage_.cooked_properties}};
    if (row && browse_properties_) {
        const auto &page = property_browser_.page();
        const auto selected = property_browser_.properties().selected_row();
        result["browser"] = {{"scope", page.scope},
                             {"first", page.first},
                             {"totalChildren", page.total_children},
                             {"array", page.array},
                             {"available", page.available},
                             {"selected", selected ? Json{{"path", selected->path},
                                                          {"before", selected->before},
                                                          {"after", selected->after},
                                                          {"change", change_name(selected->change)}}
                                                   : Json(nullptr)}};
    }
    return result;
}
void ContainerView::bind_physical_cloth() {
    const auto a = document_ && before_physical_cloth_ >= 0
                       ? document_->physical_cloth_previews.at(before_physical_cloth_).model
                       : nullptr;
    const auto b = document_ && document_->after && after_physical_cloth_ >= 0
                       ? document_->after->physical_cloth_previews.at(after_physical_cloth_).model
                       : nullptr;
    physical_cloth_.set_assets(a, b, document_ && document_->after != nullptr, physical_pair_explicit_);
}
void ContainerView::bind_animation() {
    const auto before = document_ && before_animation_ >= 0
                            ? document_->animation_previews.at(before_animation_).model
                            : nullptr;
    const auto after = document_ && document_->after && after_animation_ >= 0
                           ? document_->after->animation_previews.at(after_animation_).model
                           : nullptr;
    animation_.set_animations(before, after, document_ && document_->after != nullptr);
}
void ContainerView::bind_nanite() {
    const auto a =
        document_ && before_nanite_ >= 0 ? document_->nanite_previews.at(before_nanite_).resource : nullptr;
    const auto b = document_ && document_->after && after_nanite_ >= 0
                       ? document_->after->nanite_previews.at(after_nanite_).resource
                       : nullptr;
    nanite_.set_resources(a, b, document_ && document_->after != nullptr);
}
Json ContainerView::nanite_report() const {
    if (!nanite_present_)
        return nullptr;
    auto result = nanite_.report();
    result["beforeObject"] =
        before_nanite_ >= 0 ? Json(document_->nanite_previews.at(before_nanite_).object_path) : Json(nullptr);
    result["afterObject"] = after_nanite_ >= 0
                                ? Json(document_->after->nanite_previews.at(after_nanite_).object_path)
                                : Json(nullptr);
    result["beforeDiagnostics"] = document_->mesh_diagnostics;
    result["afterDiagnostics"] = document_->after ? Json(document_->after->mesh_diagnostics) : Json::array();
    return result;
}
Json ContainerView::animation_report() const {
    if (!animation_present_)
        return nullptr;
    auto result = animation_.report();
    result["beforeObject"] = before_animation_ >= 0
                                 ? Json(document_->animation_previews.at(before_animation_).object_path)
                                 : Json(nullptr);
    result["afterObject"] = after_animation_ >= 0
                                ? Json(document_->after->animation_previews.at(after_animation_).object_path)
                                : Json(nullptr);
    result["beforeDiagnostics"] = document_->animation_diagnostics;
    result["afterDiagnostics"] =
        document_->after ? Json(document_->after->animation_diagnostics) : Json::array();
    return result;
}
Json ContainerView::view_state() const {
    Json result = {{"filter", filter_.query},
                   {"objectFilter", object_filter_.query},
                   {"packagesOnly", packages_only_},
                   {"changedOnly", changed_only_},
                   {"member", member_ >= 0 ? Json(selected_key()) : Json(nullptr)},
                   {"object", object_ >= 0 ? Json(object_rows_[object_].path) : Json(nullptr)},
                   {"propertyMode", property_mode_},
                   {"inspection", inspection_.view_state()},
                   {"properties", properties_.view_state()},
                   {"values", values_.view_state()},
                   {"comparison", comparison_.view_state()},
                   {"schema", schema_.view_state()}};
    if (property_available_) {
        result["browseProperties"] = browse_properties_;
        result["propertyBrowser"] = property_browser_.view_state();
    }
    if (!pixel_rows_.empty() && pixel_ >= 0 && std::size_t(pixel_) < pixel_rows_.size()) {
        result["pixel"] = pixel_rows_[pixel_].key;
        result["pixelAfter"] = pixel_rows_[pixel_].after;
        result["pixelView"] = pixel_rows_[pixel_].view.view_state();
    }
    if (!mesh_rows_.empty()) {
        result["mesh"] = mesh_rows_[mesh_].key;
        result["meshAfter"] = mesh_rows_[mesh_].after;
        result["meshView"] = mesh_rows_[mesh_].view.view_state();
        result["meshAttributes"] = mesh_attributes_;
        if (mesh_rows_[mesh_].attributes_available)
            result["meshAttributeView"] = mesh_rows_[mesh_].attributes.view_state();
        result["meshCloth"] = mesh_cloth_;
        if (mesh_rows_[mesh_].cloth_available)
            result["meshClothView"] = mesh_rows_[mesh_].cloth.view_state();
        result["meshRenderMorphs"] = mesh_morphs_;
        if (mesh_rows_[mesh_].morphs_available)
            result["meshRenderMorphView"] = mesh_rows_[mesh_].morphs.view_state();
        result["meshSkin"] = mesh_skin_;
        if (mesh_rows_[mesh_].skin_available)
            result["meshSkinView"] = mesh_rows_[mesh_].skin.view_state();
    }
    if (nanite_present_) {
        result["nanite"] = nanite_.view_state();
        result["beforeNanite"] = before_nanite_ >= 0
                                     ? Json(document_->nanite_previews.at(before_nanite_).object_path)
                                     : Json(nullptr);
        result["afterNanite"] = after_nanite_ >= 0
                                    ? Json(document_->after->nanite_previews.at(after_nanite_).object_path)
                                    : Json(nullptr);
    }
    if (animation_present_) {
        result["animation"] = animation_.view_state();
        result["beforeAnimation"] =
            before_animation_ >= 0 ? Json(document_->animation_previews.at(before_animation_).object_path)
                                   : Json(nullptr);
        result["afterAnimation"] =
            after_animation_ >= 0
                ? Json(document_->after->animation_previews.at(after_animation_).object_path)
                : Json(nullptr);
    }
    if (physical_cloth_present_) {
        result["physicalCloth"] = physical_cloth_.view_state();
        result["physicalPairExplicit"] = physical_pair_explicit_;
        result["beforePhysicalCloth"] =
            before_physical_cloth_ >= 0
                ? Json(document_->physical_cloth_previews.at(before_physical_cloth_).object_path)
                : Json(nullptr);
        result["afterPhysicalCloth"] =
            after_physical_cloth_ >= 0
                ? Json(document_->after->physical_cloth_previews.at(after_physical_cloth_).object_path)
                : Json(nullptr);
    }
    if (physics_present_)
        result["physics"] = physics_.view_state();
    if (collisions_present_)
        result["collisions"] = collisions_.view_state();
    if (!layout_.snapshot().empty())
        result["layout"] = layout_.snapshot();
    return result;
}
void ContainerView::restore_view_state(const Json &state) {
    cyuediff::view_state::validate(state, "container");
    if (state.contains("layout"))
        layout_.restore(state["layout"], "container-layout");
    if (state.contains("filter"))
        filter_.query = state["filter"].get<std::string>();
    if (state.contains("objectFilter"))
        object_filter_.query = state["objectFilter"].get<std::string>();
    if (state.contains("packagesOnly"))
        packages_only_ = state["packagesOnly"].get<bool>();
    if (state.contains("changedOnly"))
        changed_only_ = state["changedOnly"].get<bool>();
    rebuild_members();
    rebuild_objects();
    if (state.contains("member")) {
        member_ = -1;
        if (!state["member"].is_null())
            select_member(state["member"].get<std::string>());
    }
    if (state.contains("object")) {
        object_ = -1;
        pending_object_ = -1;
        for (auto i : objects_)
            if (state["object"] == object_rows_[i].path) {
                select_object(static_cast<std::size_t>(i));
                break;
            }
    }
    if (object_ < 0) {
        property_available_ = properties_comparing_ = property_mode_ = false;
        properties_ = PropertyView{};
        values_ = PropertyView{};
        property_browser_ = PropertyBrowserView{};
        browse_properties_ = false;
        if (schema_details_.is_object())
            schema_details_.erase("selectedObject");
        schema_.set_values(&schema_details_, nullptr, false);
    }
    if (state.contains("propertyMode"))
        property_mode_ = property_available_ && state["propertyMode"].get<bool>();
    if (property_available_) {
        const auto snapshots = property_snapshots();
        if (state.contains("propertyBrowser"))
            property_browser_.restore_view_state(snapshots.first, snapshots.second,
                                                 state.at("propertyBrowser"));
        if (state.contains("browseProperties"))
            browse_properties_ = state.at("browseProperties").get<bool>();
    }
    auto restore = [&](const char *key, PropertyView &v) {
        if (state.contains(key))
            v.restore_view_state(state[key]);
    };
    restore("inspection", inspection_);
    restore("properties", properties_);
    restore("values", values_);
    restore("comparison", comparison_);
    restore("schema", schema_);
    if (physical_cloth_present_) {
        const auto old_before = before_physical_cloth_, old_after = after_physical_cloth_;
        const auto old_pair = physical_pair_explicit_;
        const auto select = [&](const char *key, const ContainerDocument *side, int &selected) {
            if (!state.contains(key))
                return;
            selected = -1;
            if (side && state[key].is_string())
                for (std::size_t i = 0; i < side->physical_cloth_previews.size(); ++i)
                    if (side->physical_cloth_previews[i].object_path == state[key]) {
                        selected = int(i);
                        break;
                    }
        };
        select("beforePhysicalCloth", document_.get(), before_physical_cloth_);
        select("afterPhysicalCloth", document_->after.get(), after_physical_cloth_);
        if (state.contains("physicalPairExplicit"))
            physical_pair_explicit_ = state["physicalPairExplicit"].get<bool>();
        if (old_before != before_physical_cloth_ || old_after != after_physical_cloth_ ||
            old_pair != physical_pair_explicit_)
            bind_physical_cloth();
        if (state.contains("physicalCloth"))
            physical_cloth_.restore_view_state(state["physicalCloth"]);
    }
    if (animation_present_) {
        bool changed = false;
        auto select = [&](const char *key, const ContainerDocument *side, int &selected) {
            if (!state.contains(key))
                return;
            int index = -1;
            if (side && !state[key].is_null())
                for (std::size_t i = 0; i < side->animation_previews.size(); ++i)
                    if (side->animation_previews[i].object_path == state[key]) {
                        index = int(i);
                        break;
                    }
            changed = changed || selected != index;
            selected = index;
        };
        select("beforeAnimation", document_.get(), before_animation_);
        select("afterAnimation", document_->after.get(), after_animation_);
        if (changed)
            bind_animation();
        if (state.contains("animation"))
            animation_.restore_view_state(state["animation"]);
    }
    if (nanite_present_) {
        bool changed = false;
        auto select = [&](const char *key, const ContainerDocument *side, int &index) {
            if (!state.contains(key))
                return;
            int value = -1;
            if (side && !state[key].is_null())
                for (std::size_t i = 0; i < side->nanite_previews.size(); ++i)
                    if (side->nanite_previews[i].object_path == state[key]) {
                        value = int(i);
                        break;
                    }
            changed = changed || index != value;
            index = value;
        };
        select("beforeNanite", document_.get(), before_nanite_);
        select("afterNanite", document_->after.get(), after_nanite_);
        if (changed)
            bind_nanite();
        if (state.contains("nanite"))
            nanite_.restore_view_state(state["nanite"]);
    }
    if (physics_present_ && state.contains("physics"))
        physics_.restore_view_state(state["physics"]);
    if (collisions_present_ && state.contains("collisions"))
        collisions_.restore_view_state(state["collisions"]);
    if (state.contains("mesh"))
        for (std::size_t i = 0; i < mesh_rows_.size(); ++i)
            if (mesh_rows_[i].key == state["mesh"] &&
                mesh_rows_[i].after == state.value("meshAfter", false)) {
                mesh_ = static_cast<int>(i);
                if (state.contains("meshView"))
                    mesh_rows_[i].view.restore_view_state(state["meshView"]);
                if (state.contains("meshAttributes"))
                    mesh_attributes_ = state["meshAttributes"].get<bool>();
                if (state.contains("meshAttributeView") && mesh_rows_[i].attributes_available)
                    mesh_rows_[i].attributes.restore_view_state(state["meshAttributeView"]);
                if (state.contains("meshCloth"))
                    mesh_cloth_ = state["meshCloth"].get<bool>();
                if (state.contains("meshClothView") && mesh_rows_[i].cloth_available)
                    mesh_rows_[i].cloth.restore_view_state(state["meshClothView"]);
                if (state.contains("meshRenderMorphs"))
                    mesh_morphs_ = state["meshRenderMorphs"].get<bool>();
                if (state.contains("meshRenderMorphView") && mesh_rows_[i].morphs_available)
                    mesh_rows_[i].morphs.restore_view_state(state["meshRenderMorphView"]);
                if (state.contains("meshSkin"))
                    mesh_skin_ = state["meshSkin"].get<bool>();
                if (state.contains("meshSkinView") && mesh_rows_[i].skin_available)
                    mesh_rows_[i].skin.restore_view_state(state["meshSkinView"]);
                break;
            }
    if (state.contains("pixel"))
        for (std::size_t i = 0; i < pixel_rows_.size(); ++i)
            if (pixel_rows_[i].key == state["pixel"] &&
                pixel_rows_[i].after == state.value("pixelAfter", false)) {
                pixel_ = static_cast<int>(i);
                if (state.contains("pixelView"))
                    pixel_rows_[i].view.restore_view_state(state["pixelView"]);
                break;
            }
}
ContainerAction ContainerView::draw(bool busy) {
    if (!document_)
        return ContainerAction::none;
    ContainerAction action = ContainerAction::none;
    cyui::heading(cyui::tr("Container contents").c_str(), 3);
    cyui::textDim("%s", document_->source.path.u8string().c_str());
    if (document_->after) {
        cyui::textDim("%s: %s", cyui::tr("After container").c_str(),
                      document_->after->source.path.u8string().c_str());
        cyui::textDim("%s", cyui::tr(document_->comparison.is_null()
                                         ? "Common members remain unchecked until decoded."
                                         : "Decoded comparison complete.")
                                .c_str());
        if (ImGui::Checkbox(cyui::trLabel("Hide unchanged").c_str(), &changed_only_)) {
            rebuild_members();
            rebuild_objects();
        }
    }
    if (!diagnostics_.empty() && layout_.section("diagnostics", "Container diagnostics")) {
        for (const auto &message : diagnostics_)
            ImGui::TextWrapped("%s", message.c_str());
        cyui::endSection();
    }
    if (!ImGui::BeginTabBar("##container-tabs"))
        return action;
    auto flags = [&](int tab) {
        return select_tab_ && initial_tab_ == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    };
    if (ImGui::BeginTabItem(cyui::trLabel("Members").c_str(), nullptr, flags(0))) {
        active_tab_ = 0;
        bool update = cyui::filterBox("##container-search", filter_);
        update = ImGui::Checkbox(cyui::trLabel("Zen packages only").c_str(), &packages_only_) || update;
        if (update)
            rebuild_members();
        cyui::textDim(cyui::tr("%zu / %zu members").c_str(), members_.size(), member_rows_.size());
        cyui::ScopedDisabled disabled(busy);
        const auto height = std::max(ImGui::GetFrameHeight() * 3, ImGui::GetContentRegionAvail().y -
                                                                      ImGui::GetFrameHeightWithSpacing() * 2);
        if (members_.empty()) {
            if (ImGui::BeginChild("##empty-container", ImVec2(0, height)))
                cyui::emptyState(
                    cyui::tr("No matching member").c_str(),
                    cyui::tr("Change the search or filters, or show unchanged members.").c_str());
            ImGui::EndChild();
        } else if (layout_.begin_table("##container-members", "memberColumns", 4,
                                       ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                           ImGuiTableFlags_ScrollY,
                                       ImVec2(0, height))) {
            layout_.column("Virtual path / chunk ID", 0, 5);
            layout_.column("Decoded bytes", 1, 1.2f);
            layout_.column("Chunk type", 2);
            layout_.column("State", 3);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(members_.size()));
            while (clipper.Step())
                for (int at = clipper.DisplayStart; at < clipper.DisplayEnd; ++at) {
                    const auto index = members_[at];
                    const auto &pair = member_rows_[index];
                    const auto &row = *(pair.after ? pair.after : pair.before);
                    cyui::ScopedID scope(index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const auto x = ImGui::GetCursorPosX();
                    if (ImGui::Selectable("##member", member_ == index, ImGuiSelectableFlags_SpanAllColumns))
                        member_ = index;
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(x);
                    ImGui::TextUnformatted(row.path.empty() ? row.id.c_str() : row.path.c_str());
                    cyui::tooltip(row.id.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%llu", static_cast<unsigned long long>(row.size));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", unsigned(row.chunk_type));
                    ImGui::TableSetColumnIndex(3);
                    if (!pair.state.empty())
                        cyui::badge(cyui::tr(state_label(pair.state)).c_str(), state_tone(pair.state));
                    else
                        ImGui::TextUnformatted(
                            cyui::tr(row.deleted ? "Deleted" : (row.encrypted ? "Encrypted" : "Live"))
                                .c_str());
                }
            layout_.end_table();
        }
        const auto *member = selected_member();
        {
            cyui::ScopedDisabled unavailable(!member || member->deleted || member->encrypted);
            if (cyui::primaryButton(cyui::trLabel("Inspect selected member").c_str()))
                action = ContainerAction::inspect;
        }
        ImGui::SameLine();
        {
            cyui::ScopedDisabled unavailable(!member);
            if (cyui::button(cyui::trLabel("Copy member ID").c_str()))
                ImGui::SetClipboardText(member->id.c_str());
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(cyui::trLabel("Inspection").c_str(), nullptr, flags(1))) {
        active_tab_ = 1;
        if (document_->inspection.is_null() && (!document_->after || document_->after->inspection.is_null()))
            cyui::textDim("%s", cyui::tr("Select a member and inspect its decoded bytes.").c_str());
        else if (!document_->zen && (!document_->after || !document_->after->zen)) {
            const auto &side = document_->inspection.is_null() ? *document_->after : *document_;
            const bool zen =
                side.metadata["kind"] == "iostore" && side.inspection.at("entry").at("chunkType") == 1;
            cyui::textDim("%s",
                          cyui::tr(zen ? "Choose an explicit UE profile for Zen object inspection."
                                       : "Decoded bytes; package data is not interpreted for this member.")
                              .c_str());
        }
        inspection_.draw("##container-inspection");
        ImGui::EndTabItem();
    }
    if (!object_rows_.empty() &&
        ImGui::BeginTabItem(cyui::trLabel("Cooked objects").c_str(), nullptr, flags(2))) {
        active_tab_ = 2;
        if (document_->after && !compare_objects_)
            cyui::callout(
                cyui::Tone::info,
                cyui::tr("Object comparison requires explicit profiles on both present members.").c_str());
        if (!document_->selected_comparison.is_null() &&
            !document_->selected_comparison.at("zen").is_null() &&
            document_->selected_comparison.at("zen").at("contextChanged").get<bool>())
            cyui::callout(cyui::Tone::info, cyui::tr("Cooked interpretation context differs.").c_str());
        cyui::callout(cyui::Tone::warning,
                      cyui::tr("Only explicitly supported native profiles are decoded.").c_str());
        if (cyui::filterBox("##cooked-object-search", object_filter_))
            rebuild_objects();
        const auto available = ImGui::GetContentRegionAvail();
        if (layout_.begin_table("##cooked-split", "objectColumns", 2)) {
            layout_.column("##objects", 0, 1);
            layout_.column("##properties", 1, 2);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::BeginChild("##cooked-objects", ImVec2(0, available.y))) {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(objects_.size()));
                while (clipper.Step())
                    for (int at = clipper.DisplayStart; at < clipper.DisplayEnd; ++at) {
                        const auto index = objects_[at];
                        const auto &row = object_rows_[index];
                        cyui::ScopedID scope(index);
                        const auto x = ImGui::GetCursorPosX();
                        if (ImGui::Selectable("##object", object_ == index)) {
                            pending_object_ = index;
                            action = ContainerAction::select_object;
                        }
                        ImGui::SameLine();
                        ImGui::SetCursorPosX(x);
                        ImGui::TextUnformatted(row.name.c_str());
                        cyui::tooltip(row.path.c_str());
                        if (!row.state.empty())
                            cyui::badge(cyui::tr(state_label(row.state)).c_str(), state_tone(row.state));
                    }
            }
            ImGui::EndChild();
            ImGui::TableSetColumnIndex(1);
            if (ImGui::BeginChild("##cooked-object-properties", ImVec2(0, available.y))) {
                if (object_ >= 0) {
                    const auto details = cyui::tr("Object details"), values = cyui::tr("Decoded properties");
                    const char *modes[] = {details.c_str(), values.c_str()};
                    int mode = property_mode_ ? 1 : 0;
                    if (cyui::segmented("##cooked-inspection-mode", modes, 2, &mode, -1))
                        property_mode_ = mode == 1;
                    if (property_mode_) {
                        if (!property_notice_.empty())
                            cyui::callout(cyui::Tone::info, cyui::tr(property_notice_.c_str()).c_str());
                        if (property_available_) {
                            ImGui::Checkbox(cyui::trLabel("Browse by level").c_str(), &browse_properties_);
                            if (browse_properties_) {
                                const auto snapshots = property_snapshots();
                                property_browser_.draw(snapshots.first, snapshots.second, "##cooked-browser");
                            } else
                                values_.draw("##cooked-values");
                        } else
                            cyui::emptyState(
                                cyui::tr("No decoded properties").c_str(),
                                cyui::tr("Supply a matching schema context and inspect the member.").c_str());
                    } else
                        properties_.draw("##cooked-object");
                } else
                    cyui::emptyState(cyui::tr("Select a cooked object.").c_str(), "");
            }
            ImGui::EndChild();
            layout_.end_table();
        }
        ImGui::EndTabItem();
    }
    if (document_->after && ImGui::BeginTabItem(cyui::trLabel("Comparison").c_str(), nullptr, flags(3))) {
        active_tab_ = 3;
        comparison_.draw("##container-comparison");
        ImGui::EndTabItem();
    }
    if ((document_->schema_manifest || (document_->after && document_->after->schema_manifest)) &&
        ImGui::BeginTabItem(cyui::trLabel("Schema context").c_str(), nullptr, flags(4))) {
        active_tab_ = 4;
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("Explicit mappings do not verify cooked build compatibility or resolve defaults.")
                .c_str());
        schema_.draw("##schema-provenance");
        ImGui::EndTabItem();
    }
    if (!pixel_rows_.empty() &&
        ImGui::BeginTabItem(cyui::trLabel("Cooked pixels").c_str(), nullptr, flags(5))) {
        active_tab_ = 5;
        auto label = [&](const PixelRow &row) {
            return (document_->after ? cyui::tr(row.after ? "After" : "Before") + " | " : std::string{}) +
                   row.key;
        };
        if (ImGui::BeginCombo("##cooked-pixel-selection", label(pixel_rows_[pixel_]).c_str())) {
            for (std::size_t i = 0; i < pixel_rows_.size(); ++i)
                if (ImGui::Selectable(label(pixel_rows_[i]).c_str(), pixel_ == static_cast<int>(i)))
                    pixel_ = static_cast<int>(i);
            ImGui::EndCombo();
        }
        cyui::textDim(
            "%s", cyui::tr("Stored channels; no color transform. HDR previews use Reinhard / sRGB at 0 EV.")
                      .c_str());
        pixel_rows_[pixel_].view.draw("##cooked-pixels");
        ImGui::EndTabItem();
    }
    if (!mesh_rows_.empty() &&
        ImGui::BeginTabItem(cyui::trLabel("Cooked meshes").c_str(), nullptr, flags(6))) {
        active_tab_ = 6;
        auto label = [&](const MeshRow &r) {
            return (document_->after ? cyui::tr(r.after ? "After" : "Before") + " | " : std::string{}) +
                   r.key;
        };
        if (ImGui::BeginCombo("##cooked-mesh-selection", label(mesh_rows_[mesh_]).c_str())) {
            for (std::size_t i = 0; i < mesh_rows_.size(); ++i)
                if (ImGui::Selectable(label(mesh_rows_[i]).c_str(), mesh_ == static_cast<int>(i)))
                    mesh_ = static_cast<int>(i);
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s", cyui::tr("Stored render LOD geometry.").c_str());
        auto &row = mesh_rows_[mesh_];
        if (!row.comparison_known)
            cyui::callout(cyui::Tone::warning,
                          cyui::tr("Peer mesh unavailable; changes cannot be classified.").c_str());
        if (row.skin_available && ImGui::Checkbox(cyui::trLabel("Bones and weights").c_str(), &mesh_skin_) &&
            mesh_skin_) {
            mesh_morphs_ = false;
            mesh_cloth_ = false;
            mesh_attributes_ = false;
        }
        if (row.morphs_available &&
            ImGui::Checkbox(cyui::trLabel("Render morph targets").c_str(), &mesh_morphs_) && mesh_morphs_) {
            mesh_skin_ = false;
            mesh_cloth_ = false;
            mesh_attributes_ = false;
        }
        if (row.cloth_available && ImGui::Checkbox(cyui::trLabel("Cloth mappings").c_str(), &mesh_cloth_) &&
            mesh_cloth_) {
            mesh_skin_ = false;
            mesh_morphs_ = false;
            mesh_attributes_ = false;
        }
        if (row.attributes_available &&
            ImGui::Checkbox(cyui::trLabel("Vertex attributes").c_str(), &mesh_attributes_) &&
            mesh_attributes_) {
            mesh_skin_ = mesh_morphs_ = mesh_cloth_ = false;
        }
        if (row.attributes_available && mesh_attributes_)
            row.attributes.draw("##cooked-attributes");
        else if (row.cloth_available && mesh_cloth_)
            row.cloth.draw("##cooked-cloth");
        else if (row.morphs_available && mesh_morphs_)
            row.morphs.draw("##cooked-morphs");
        else if (row.skin_available && mesh_skin_)
            row.skin.draw("##cooked-skin");
        else
            row.view.draw("##cooked-mesh");
        ImGui::EndTabItem();
    }
    if (collisions_present_ &&
        ImGui::BeginTabItem(cyui::trLabel("Cooked collisions").c_str(), nullptr, flags(7))) {
        active_tab_ = 7;
        collisions_.draw("##cooked-collision-review");
        ImGui::EndTabItem();
    }
    if (physics_present_ && ImGui::BeginTabItem(cyui::trLabel("Native physics").c_str(), nullptr, flags(8))) {
        active_tab_ = 8;
        physics_.draw("##native-physics-review");
        ImGui::EndTabItem();
    }
    if (animation_present_ &&
        ImGui::BeginTabItem(cyui::trLabel("Cooked animations").c_str(), nullptr, flags(9))) {
        active_tab_ = 9;
        auto choose = [&](const char *label, const ContainerDocument *side, int &selected) {
            if (!side || side->animation_previews.empty())
                return;
            const auto preview = selected >= 0 ? side->animation_previews.at(selected).object_path
                                               : cyui::tr("Animation object unavailable");
            if (ImGui::BeginCombo(cyui::trLabel(label).c_str(), preview.c_str())) {
                for (std::size_t i = 0; i < side->animation_previews.size(); ++i)
                    if (ImGui::Selectable(side->animation_previews[i].object_path.c_str(),
                                          selected == int(i))) {
                        selected = int(i);
                        bind_animation();
                    }
                ImGui::EndCombo();
            }
        };
        choose("Before animation object", document_.get(), before_animation_);
        choose("After animation object", document_->after.get(), after_animation_);
        for (const auto &diagnostic : document_->animation_diagnostics)
            ImGui::TextWrapped("%s: %s", cyui::tr("Before").c_str(), diagnostic.c_str());
        if (document_->after)
            for (const auto &diagnostic : document_->after->animation_diagnostics)
                ImGui::TextWrapped("%s: %s", cyui::tr("After").c_str(), diagnostic.c_str());
        animation_.draw();
        ImGui::EndTabItem();
    }
    if (nanite_present_ && ImGui::BeginTabItem(cyui::trLabel("Nanite").c_str(), nullptr, flags(10))) {
        active_tab_ = 10;
        auto choose = [&](const char *label, const ContainerDocument *side, int &selected) {
            const auto text = side && selected >= 0 ? side->nanite_previews[std::size_t(selected)].object_path
                                                    : cyui::tr("Nanite resource unavailable");
            if (ImGui::BeginCombo(cyui::trLabel(label).c_str(), text.c_str())) {
                bool changed = false;
                if (ImGui::Selectable(cyui::trLabel("Unavailable").c_str(), selected < 0)) {
                    selected = -1;
                    changed = true;
                }
                if (side)
                    for (std::size_t i = 0; i < side->nanite_previews.size(); ++i)
                        if (ImGui::Selectable(side->nanite_previews[i].object_path.c_str(),
                                              selected == int(i))) {
                            selected = int(i);
                            changed = true;
                        }
                ImGui::EndCombo();
                if (changed)
                    bind_nanite();
            }
        };
        choose("Before Nanite resource", document_.get(), before_nanite_);
        if (document_->after)
            choose("After Nanite resource", document_->after.get(), after_nanite_);
        for (const auto &diagnostic : document_->mesh_diagnostics)
            ImGui::TextWrapped("%s: %s", cyui::tr("Before").c_str(), diagnostic.c_str());
        if (document_->after)
            for (const auto &diagnostic : document_->after->mesh_diagnostics)
                ImGui::TextWrapped("%s: %s", cyui::tr("After").c_str(), diagnostic.c_str());
        nanite_.draw();
        ImGui::EndTabItem();
    }
    if (physical_cloth_present_ &&
        ImGui::BeginTabItem(cyui::trLabel("Physical cloth").c_str(), nullptr, flags(11))) {
        active_tab_ = 11;
        const auto choose = [&](const char *label, const ContainerDocument *side, int &selected) {
            const auto text = side && selected >= 0 ? side->physical_cloth_previews.at(selected).object_path
                                                    : cyui::tr("Unavailable");
            if (ImGui::BeginCombo(cyui::trLabel(label).c_str(), text.c_str())) {
                bool changed = false;
                if (ImGui::Selectable(cyui::trLabel("Unavailable").c_str(), selected < 0)) {
                    selected = -1;
                    changed = true;
                }
                if (side)
                    for (std::size_t i = 0; i < side->physical_cloth_previews.size(); ++i)
                        if (ImGui::Selectable(side->physical_cloth_previews[i].object_path.c_str(),
                                              selected == int(i))) {
                            selected = int(i);
                            changed = true;
                        }
                ImGui::EndCombo();
                if (changed)
                    bind_physical_cloth();
            }
        };
        choose("Before physical cloth object", document_.get(), before_physical_cloth_);
        if (document_->after) {
            choose("After physical cloth object", document_->after.get(), after_physical_cloth_);
            if (ImGui::Checkbox(cyui::trLabel("Compare selected physical objects").c_str(),
                                &physical_pair_explicit_))
                bind_physical_cloth();
        }
        for (const auto *side : {document_.get(), document_->after.get()})
            if (side) {
                for (const auto &d : side->physical_cloth_diagnostics)
                    ImGui::TextWrapped(
                        "%s: %s", cyui::tr(side == document_.get() ? "Before" : "After").c_str(), d.c_str());
                for (const auto &p : side->physical_cloth_previews)
                    for (const auto &d : p.model->diagnostics())
                        ImGui::TextWrapped("%s", d.c_str());
            }
        physical_cloth_.draw();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    select_tab_ = false;
    return action;
}
} // namespace cyuediff::desktop
