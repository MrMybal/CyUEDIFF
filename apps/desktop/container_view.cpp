#include "container_view.hpp"
#include <algorithm>
namespace cyuediff::desktop {
void ContainerView::register_translations() {
    PropertyView::register_translations();
    cyui::registerTranslations("en", {{"container-index", "Reading container index"},
                                      {"container-read", "Reading container member"},
                                      {"cooked-script-objects", "Reading script objects"},
                                      {"cooked-package", "Reading cooked objects"}});
    cyui::registerTranslations(
        "fr", {{"Containers", "Conteneurs"},
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
               {"Change the search or the Zen package filter.",
                "Modifiez la recherche ou le filtre des packages Zen."},
               {"Select a member and inspect its decoded bytes.",
                "Choisissez un membre pour inspecter ses octets décodés."},
               {"Choose an explicit UE profile for Zen object inspection.",
                "Choisissez un profil UE explicite pour inspecter les objets Zen."},
               {"Native export bodies remain opaque; their hashes are available.",
                "Les corps natifs des exports restent opaques ; leurs empreintes sont disponibles."},
               {"Select a cooked object.", "Choisissez un objet cooked."},
               {"Container diagnostics", "Diagnostics du conteneur"},
               {"Decoded bytes; package data is not interpreted for this member.",
                "Octets décodés ; les données de package ne sont pas interprétées pour ce membre."},
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
void ContainerView::rebuild_members() {
    members_.clear();
    if (document_) {
        const auto &rows = document_->input->entries();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto &row = rows[i];
            const bool zen = document_->metadata["kind"] == "iostore" && row.chunk_type == 1;
            if ((!packages_only_ || zen) && filter_.matches(row.path, row.id + " " + (zen ? "Zen" : "")))
                members_.push_back(static_cast<int>(i));
        }
    }
    if (std::find(members_.begin(), members_.end(), member_) == members_.end())
        member_ = -1;
}
void ContainerView::rebuild_objects() {
    objects_.clear();
    if (document_ && document_->zen)
        for (std::size_t i = 0; i < document_->zen->exports.size(); ++i) {
            const auto &row = document_->zen->exports[i];
            if (object_filter_.matches(row.path, row.class_name))
                objects_.push_back(static_cast<int>(i));
        }
    if (std::find(objects_.begin(), objects_.end(), object_) == objects_.end())
        object_ = -1;
}
bool ContainerView::select_member(const std::string &key) {
    if (document_)
        for (auto i : members_) {
            const auto &row = document_->input->entries()[i];
            if (row.id == key || row.path == key) {
                member_ = i;
                return true;
            }
        }
    return false;
}
const ContainerEntry *ContainerView::selected_member() const {
    return document_ && member_ >= 0 ? &document_->input->entries()[static_cast<std::size_t>(member_)]
                                     : nullptr;
}
bool ContainerView::select_object(std::size_t index) {
    if (!document_ || !document_->zen || index >= document_->zen->exports.size() ||
        std::find(objects_.begin(), objects_.end(), static_cast<int>(index)) == objects_.end())
        return false;
    object_ = static_cast<int>(index);
    object_data_ = document_->zen->exports[index].to_json();
    properties_.set_values(&object_data_, nullptr, false);
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
    rebuild_members();
    rebuild_objects();
    diagnostics_.clear();
    if (document_) {
        for (const auto &text : document_->metadata.at("diagnostics"))
            diagnostics_.push_back(text.get<std::string>());
        if (document_->zen)
            diagnostics_.insert(diagnostics_.end(), document_->zen->diagnostics.begin(),
                                document_->zen->diagnostics.end());
        if (!select_member(document_->source.entry) && !members_.empty())
            member_ = members_.front();
        inspection_.set_values(
            document_->inspection.is_null() ? &document_->metadata : &document_->inspection, nullptr, false);
        if (!objects_.empty())
            select_object(static_cast<std::size_t>(objects_.front()));
        initial_tab_ = document_->zen ? 2 : (document_->inspection.is_null() ? 0 : 1);
        select_tab_ = true;
    }
}
ContainerAction ContainerView::draw(bool busy) {
    if (!document_)
        return ContainerAction::none;
    ContainerAction action = ContainerAction::none;
    cyui::heading(cyui::tr("Container contents").c_str(), 3);
    cyui::textDim("%s", document_->source.path.u8string().c_str());
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
        cyui::textDim(cyui::tr("%zu / %zu members").c_str(), members_.size(),
                      document_->input->entries().size());
        cyui::ScopedDisabled disabled(busy);
        const auto height = std::max(ImGui::GetFrameHeight() * 3, ImGui::GetContentRegionAvail().y -
                                                                      ImGui::GetFrameHeightWithSpacing() * 2);
        if (members_.empty()) {
            if (ImGui::BeginChild("##empty-container", ImVec2(0, height)))
                cyui::emptyState(cyui::tr("No matching member").c_str(),
                                 cyui::tr("Change the search or the Zen package filter.").c_str());
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
                    const auto &row = document_->input->entries()[index];
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
                    ImGui::TextUnformatted(
                        cyui::tr(row.deleted ? "Deleted" : (row.encrypted ? "Encrypted" : "Live")).c_str());
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
        if (document_->inspection.is_null())
            cyui::textDim("%s", cyui::tr("Select a member and inspect its decoded bytes.").c_str());
        else if (!document_->zen) {
            const bool zen = document_->metadata["kind"] == "iostore" &&
                             document_->inspection.at("entry").at("chunkType") == 1;
            cyui::textDim("%s",
                          cyui::tr(zen ? "Choose an explicit UE profile for Zen object inspection."
                                       : "Decoded bytes; package data is not interpreted for this member.")
                              .c_str());
        }
        inspection_.draw("##container-inspection");
        ImGui::EndTabItem();
    }
    if (document_->zen && ImGui::BeginTabItem(cyui::trLabel("Cooked objects").c_str(), nullptr, flags(2))) {
        active_tab_ = 2;
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
                    const auto &row = document_->zen->exports[index];
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
    ImGui::EndTabBar();
    select_tab_ = false;
    return action;
}
} // namespace cyuediff::desktop
