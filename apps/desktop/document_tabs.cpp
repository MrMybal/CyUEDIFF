#include "document_tabs.hpp"
namespace cyuediff::desktop {
void DocumentTabsView::register_translations() {
    cyui::registerTranslations("fr", {{"Duplicate document", "Dupliquer le document"},
                                      {"Reopen closed document", "Rouvrir le document fermé"},
                                      {"Close other documents", "Fermer les autres documents"}});
    cyui::registerTranslations("en", {{"Queued document", "Waiting for document loader"}});
    cyui::registerTranslations("fr", {{"New document", "Nouveau document"},
                                      {"New document (Ctrl+N)", "Nouveau document (Ctrl+N)"},
                                      {"Close document", "Fermer le document"},
                                      {"Untitled", "Sans titre"},
                                      {"Queued document", "En attente du chargement"},
                                      {"Documents", "Documents"}});
}
void DocumentTabsView::set_tabs(std::vector<DocumentTab> tabs, DocumentId active, bool can_create) {
    if (active != active_)
        select_ = active;
    active_ = active;
    can_create_ = can_create;
    tabs_ = std::move(tabs);
    labels_.clear();
    for (const auto &tab : tabs_) {
        auto label = tab.label;
        // ImGui ID delimiters and control bytes in filenames are display data only.
        for (char &c : label)
            if (c == '#' || static_cast<unsigned char>(c) < 32)
                c = '_';
        if (label.empty())
            label = cyui::tr("Untitled");
        if (tab.failed)
            label += " !";
        else if (tab.busy)
            label += " ...";
        labels_.push_back(label + "###document-" + std::to_string(tab.id));
    }
}
DocumentTabAction DocumentTabsView::draw() {
    DocumentTabAction result;
    if (!ImGui::BeginTabBar("##documents", ImGuiTabBarFlags_FittingPolicyScroll))
        return result;
    const bool syncing = select_ != 0;
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        const auto &tab = tabs_[i];
        bool open = true;
        const auto flags = tab.id == select_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        const bool shown = ImGui::BeginTabItem(labels_[i].c_str(), &open, flags);
        const bool clicked = ImGui::IsItemClicked();
        cyui::tooltip(tab.tooltip.c_str());
        if (shown)
            ImGui::EndTabItem();
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem(cyui::trLabel("Duplicate document").c_str(), "Ctrl+D", false, can_create_))
                result = {DocumentTabOperation::duplicate, tab.id};
            if (ImGui::MenuItem(cyui::trLabel("Close document").c_str(), "Ctrl+W"))
                result = {DocumentTabOperation::close, tab.id};
            if (ImGui::MenuItem(cyui::trLabel("Close other documents").c_str(), nullptr, false,
                                tabs_.size() > 1))
                result = {DocumentTabOperation::close_others, tab.id};
            ImGui::EndPopup();
        }

        if (!open)
            result = {DocumentTabOperation::close, tab.id};
        else if (result.operation == DocumentTabOperation::none && tab.id != active_ &&
                 (clicked || (shown && !syncing)))
            result = {DocumentTabOperation::select, tab.id};
    }
    {
        cyui::ScopedDisabled disabled(!can_create_);
        if (ImGui::TabItemButton("+##new-document", ImGuiTabItemFlags_Trailing))
            result = {DocumentTabOperation::create, 0};
        cyui::tooltip(cyui::tr("New document (Ctrl+N)").c_str());
    }
    ImGui::EndTabBar();
    select_ = 0;
    return result;
}
} // namespace cyuediff::desktop
