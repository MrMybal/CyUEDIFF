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
    PropertyView::register_translations();
    cyui::registerTranslations(
        "fr", {{"Cooked meshes", "Meshes cooked"},
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
               {"Cooked render buffers; later Nanite and collision data remains opaque.",
                "Buffers de rendu cooked ; les données Nanite et de collision suivantes restent opaques."},
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
    if (properties_comparing_)
        values_.set_values(row.before_values.is_null() ? nullptr : &row.before_values,
                           row.after_values.is_null() ? nullptr : &row.after_values, true);
    else
        values_.set_values(!row.before_values.is_null()
                               ? &row.before_values
                               : (!row.after_values.is_null() ? &row.after_values : nullptr),
                           nullptr, false);
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
    properties_ = PropertyView{};
    schema_ = PropertyView{};
    member_rows_.clear();
    object_rows_.clear();
    physics_present_ = false;
    physics_.set_models(nullptr, nullptr, {}, {});
    collisions_present_ = false;
    collisions_.set_models(nullptr, nullptr, {}, {});
    mesh_rows_.clear();
    mesh_ = 0;
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
                if (peer)
                    for (const auto &p : peer->mesh_previews)
                        if (p.key == preview.key)
                            other = p.geometry;
                MeshRow row;
                row.key = preview.key;
                row.after = after;
                row.view.set_mesh(preview.geometry, other, after, peer != nullptr);
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
        select_tab_ = true;
    } else {
        members_.clear();
        objects_.clear();
    }
}

bool ContainerView::select_tab(int tab) {
    if (!document_ || tab < 0 || tab > 8 || (tab == 8 && !physics_present_) ||
        (tab == 7 && !collisions_present_) || (tab == 6 && mesh_rows_.empty()) ||
        (tab == 5 && pixel_rows_.empty()) || (tab == 2 && object_rows_.empty()) ||
        (tab == 3 && !document_->after) ||
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
                        {"edges", r.view.visible_edge_count()}});
    return rows;
}
Json ContainerView::property_report() const {
    auto status = [](const Json &read) -> Json {
        return read.is_null() ? Json("unbound") : read.at("status");
    };
    const auto *row = object_ >= 0 ? &object_rows_[object_] : nullptr;
    return {{"available", row && property_available_},
            {"comparing", row && properties_comparing_},
            {"mode", property_mode_ ? "properties" : "object"},
            {"beforeStatus", row ? status(row->before_read) : Json(nullptr)},
            {"afterStatus", row ? status(row->after_read) : Json(nullptr)},
            {"visibleProperties", row ? values_.visible_count() : 0}};
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
    if (!pixel_rows_.empty() && pixel_ >= 0 && std::size_t(pixel_) < pixel_rows_.size()) {
        result["pixel"] = pixel_rows_[pixel_].key;
        result["pixelAfter"] = pixel_rows_[pixel_].after;
        result["pixelView"] = pixel_rows_[pixel_].view.view_state();
    }
    if (!mesh_rows_.empty()) {
        result["mesh"] = mesh_rows_[mesh_].key;
        result["meshAfter"] = mesh_rows_[mesh_].after;
        result["meshView"] = mesh_rows_[mesh_].view.view_state();
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
        if (schema_details_.is_object())
            schema_details_.erase("selectedObject");
        schema_.set_values(&schema_details_, nullptr, false);
    }
    if (state.contains("propertyMode"))
        property_mode_ = property_available_ && state["propertyMode"].get<bool>();
    auto restore = [&](const char *key, PropertyView &v) {
        if (state.contains(key))
            v.restore_view_state(state[key]);
    };
    restore("inspection", inspection_);
    restore("properties", properties_);
    restore("values", values_);
    restore("comparison", comparison_);
    restore("schema", schema_);
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
                        if (property_available_)
                            values_.draw("##cooked-values");
                        else
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
        ImGui::TextWrapped(
            "%s", cyui::tr("Cooked render buffers; later Nanite and collision data remains opaque.").c_str());
        mesh_rows_[mesh_].view.draw("##cooked-mesh");
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
    ImGui::EndTabBar();
    select_tab_ = false;
    return action;
}
} // namespace cyuediff::desktop
