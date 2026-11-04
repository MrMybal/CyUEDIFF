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
    cyui::registerTranslations("en", {{"container-diff", "Comparing decoded members"},
                                      {"container-index", "Reading container index"},
                                      {"container-read", "Reading container member"},
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
         {"Native export bodies remain opaque; their hashes are available.",
          "Les corps natifs des exports restent opaques ; leurs empreintes sont disponibles."},
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
    object_data_ = object_rows_[index].before;
    after_object_data_ = object_rows_[index].after;
    if (document_->after && !compare_objects_) {
        properties_.set_values(object_data_.is_null() ? &after_object_data_ : &object_data_, nullptr, false);
        return true;
    }
    properties_.set_values(object_data_.is_null() ? nullptr : &object_data_,
                           after_object_data_.is_null() ? nullptr : &after_object_data_, compare_objects_);
    return true;
}
void ContainerView::apply_object_selection() {
    if (pending_object_ >= 0)
        select_object(static_cast<std::size_t>(pending_object_));
    pending_object_ = -1;
}
void ContainerView::set_document(std::shared_ptr<const ContainerDocument> value) {
    document_ = std::move(value);
    member_ = object_ = pending_object_ = -1;
    compare_objects_ = false;
    member_rows_.clear();
    object_rows_.clear();
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
        if (document_->after)
            diagnostics(*document_->after);
        if (document_->after) {
            const auto before = document_->zen ? document_->zen->snapshot().at("objects") : Json::object();
            const auto after =
                document_->after->zen ? document_->after->zen->snapshot().at("objects") : Json::object();
            std::map<std::string, ObjectRow> pairs;
            auto add = [&](const Json &objects, bool right) {
                for (auto it = objects.begin(); it != objects.end(); ++it) {
                    auto &row = pairs[it.key()];
                    row.path = it.key();
                    row.name = it.value().at("name").get<std::string>();
                    row.search += " " + it.value().at("class").get<std::string>();
                    (right ? row.after : row.before) = it.value();
                }
            };
            add(before, false);
            add(after, true);
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
                row.search += " " + row.state;
                object_rows_.push_back(std::move(row));
            }
        } else if (document_->zen) {
            for (const auto &row : document_->zen->exports)
                object_rows_.push_back({row.name, row.path, "", row.class_name, row.to_json(), nullptr});
        }
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
        select_tab_ = true;
    } else {
        members_.clear();
        objects_.clear();
    }
}

bool ContainerView::select_tab(int tab) {
    if (!document_ || tab < 0 || tab > 3 || (tab == 2 && object_rows_.empty()) ||
        (tab == 3 && !document_->after))
        return false;
    initial_tab_ = tab;
    select_tab_ = true;
    return true;
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
    if (!diagnostics_.empty() && cyui::beginSection(cyui::trLabel("Container diagnostics").c_str(), false)) {
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
        } else if (ImGui::BeginTable("##container-members", 4,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                         ImGuiTableFlags_ScrollY,
                                     ImVec2(0, height))) {
            ImGui::TableSetupColumn(cyui::tr("Virtual path / chunk ID").c_str(),
                                    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(cyui::tr("Decoded bytes").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    cyui::dp(110));
            ImGui::TableSetupColumn(cyui::tr("Chunk type").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    cyui::dp(90));
            ImGui::TableSetupColumn(cyui::tr("State").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    cyui::dp(90));
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
            ImGui::EndTable();
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
                      cyui::tr("Native export bodies remain opaque; their hashes are available.").c_str());
        if (cyui::filterBox("##cooked-object-search", object_filter_))
            rebuild_objects();
        const auto available = ImGui::GetContentRegionAvail();
        if (ImGui::BeginChild("##cooked-objects", ImVec2(available.x * .35f, available.y))) {
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
        ImGui::SameLine();
        if (ImGui::BeginChild("##cooked-object-properties", ImVec2(0, available.y))) {
            if (object_ >= 0)
                properties_.draw("##cooked-object");
            else
                cyui::emptyState(cyui::tr("Select a cooked object.").c_str(), "");
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (document_->after && ImGui::BeginTabItem(cyui::trLabel("Comparison").c_str(), nullptr, flags(3))) {
        active_tab_ = 3;
        comparison_.draw("##container-comparison");
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    select_tab_ = false;
    return action;
}
} // namespace cyuediff::desktop
