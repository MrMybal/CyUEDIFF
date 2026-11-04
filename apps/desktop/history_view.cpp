#include "history_view.hpp"
#include <algorithm>
namespace cyuediff::desktop {
void HistoryView::register_translations() {
    cyui::registerTranslations(
        "fr", {{"Git history", "Historique Git"},
               {"Browse history...", "Parcourir l'historique..."},
               {"Start revision", "Révision de départ"},
               {"Refresh history", "Actualiser l'historique"},
               {"Previous page", "Page précédente"},
               {"Next page", "Page suivante"},
               {"Reading Git history...", "Lecture de l'historique Git..."},
               {"No matching commits on this page", "Aucun commit correspondant sur cette page"},
               {"Search applies to the current page.", "La recherche s'applique à la page actuelle."},
               {"Commit", "Commit"},
               {"Author date", "Date d'auteur"},
               {"Author", "Auteur"},
               {"Subject", "Sujet"},
               {"Use as before", "Utiliser comme avant"},
               {"Use as after", "Utiliser comme après"},
               {"Compare selected revisions", "Comparer les révisions choisies"},
               {"Copy commit ID", "Copier l'identifiant du commit"},
               {"Pinned head: %s", "Commit de départ fixé : %s"},
               {"Commits %u–%u", "Commits %u–%u"},
               {"Close history", "Fermer l'historique"}});
}
void HistoryView::set_document(std::shared_ptr<const HistoryDocument> value) {
    document_ = std::move(value);
    selected_ = -1;
    rebuild();
    if (!visible_.empty())
        selected_ = visible_.front();
}
void HistoryView::set_filter(std::string value) {
    filter_.query = std::move(value);
    rebuild();
}
void HistoryView::rebuild() {
    visible_.clear();
    if (document_)
        for (std::size_t i = 0; i < document_->page.commits.size(); ++i) {
            const auto &row = document_->page.commits[i];
            if (filter_.matches(row.subject, row.id + " " + row.author + " " + row.authored_at))
                visible_.push_back(static_cast<int>(i));
        }
    if (std::find(visible_.begin(), visible_.end(), selected_) == visible_.end())
        selected_ = -1;
}
bool HistoryView::select_commit(const std::string &id) {
    if (document_)
        for (auto i : visible_)
            if (document_->page.commits[i].id == id) {
                selected_ = i;
                return true;
            }
    return false;
}
const GitCommit *HistoryView::selected() const {
    return document_ && selected_ >= 0 ? &document_->page.commits[static_cast<std::size_t>(selected_)]
                                       : nullptr;
}
HistoryAction HistoryView::draw(bool busy, const std::string &error, const std::string &before,
                                const std::string &after, bool can_compare) {
    HistoryAction action = HistoryAction::none;
    if (cyui::button(cyui::trLabel("Refresh history").c_str()))
        action = HistoryAction::refresh;
    if (busy) {
        ImGui::SameLine();
        cyui::spinner("##history-loading");
        cyui::textDim("%s", cyui::tr("Reading Git history...").c_str());
    }
    if (!error.empty())
        cyui::callout(cyui::Tone::error, error.c_str());
    const bool ready = document_ && !busy && error.empty();
    if (document_) {
        cyui::textDim(cyui::tr("Pinned head: %s").c_str(), document_->page.head.c_str());
        cyui::textDim(cyui::tr("Commits %u–%u").c_str(),
                      document_->page.commits.empty() ? 0 : document_->page.skip + 1,
                      document_->page.next_skip);
    }
    if (cyui::filterBox("##history-search", filter_))
        rebuild();
    cyui::textFaint("%s", cyui::tr("Search applies to the current page.").c_str());
    {
        cyui::ScopedDisabled disabled(!ready);
        const auto height = std::min(cyui::dp(300), ImGui::GetMainViewport()->WorkSize.y * .35f);
        if (visible_.empty()) {
            if (ImGui::BeginChild("##history-empty", ImVec2(0, height)))
                cyui::emptyState(cyui::tr("No matching commits on this page").c_str(),
                                 cyui::tr("Search applies to the current page.").c_str());
            ImGui::EndChild();
        } else if (ImGui::BeginTable("##history-commits", 4,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                         ImGuiTableFlags_ScrollY,
                                     ImVec2(0, height))) {
            ImGui::TableSetupColumn(cyui::tr("Commit").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    cyui::dp(110));
            ImGui::TableSetupColumn(cyui::tr("Author date").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    cyui::dp(190));
            ImGui::TableSetupColumn(cyui::tr("Author").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    cyui::dp(130));
            ImGui::TableSetupColumn(cyui::tr("Subject").c_str(), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible_.size()));
            while (clipper.Step())
                for (int at = clipper.DisplayStart; at < clipper.DisplayEnd; ++at) {
                    const auto index = visible_[at];
                    const auto &row = document_->page.commits[index];
                    cyui::ScopedID scope(index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const auto x = ImGui::GetCursorPosX();
                    if (ImGui::Selectable("##commit", selected_ == index,
                                          ImGuiSelectableFlags_SpanAllColumns))
                        selected_ = index;
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(x);
                    ImGui::TextUnformatted(row.id.substr(0, 12).c_str());
                    cyui::tooltip(row.id.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(row.authored_at.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(row.author.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(row.subject.c_str());
                    cyui::tooltip(row.subject.c_str());
                }
            ImGui::EndTable();
        }
        if (const auto *row = selected()) {
            if (cyui::button(cyui::trLabel("Use as before").c_str()))
                action = HistoryAction::before;
            ImGui::SameLine();
            if (cyui::button(cyui::trLabel("Use as after").c_str()))
                action = HistoryAction::after;
            ImGui::SameLine();
            if (cyui::button(cyui::trLabel("Copy commit ID").c_str()))
                ImGui::SetClipboardText(row->id.c_str());
        }
    }
    if (cyui::beginProperties("##history-selection", 80)) {
        cyui::property(cyui::tr("Before ref").c_str());
        ImGui::TextUnformatted(before.c_str());
        cyui::property(cyui::tr("After ref").c_str());
        ImGui::TextUnformatted(after.c_str());
        cyui::endProperties();
    }
    {
        cyui::ScopedDisabled disabled(!ready || !document_->page.skip);
        if (cyui::button(cyui::trLabel("Previous page").c_str()))
            action = HistoryAction::previous;
    }
    ImGui::SameLine();
    {
        cyui::ScopedDisabled disabled(!ready || !document_->page.has_more);
        if (cyui::button(cyui::trLabel("Next page").c_str()))
            action = HistoryAction::next;
    }
    {
        cyui::ScopedDisabled disabled(!ready || !can_compare);
        if (cyui::primaryButton(cyui::trLabel("Compare selected revisions").c_str()))
            action = HistoryAction::compare;
    }
    ImGui::SameLine();
    if (cyui::button(cyui::trLabel("Close history").c_str()))
        action = HistoryAction::close;
    return action;
}
} // namespace cyuediff::desktop
