#include <cyuediff/catalog_view.hpp>
#include <algorithm>
namespace cyuediff {
void CatalogView::register_translations() {
    cyui::registerTranslations("fr", {{"All packages", "Tous les packages"},
                                      {"Assets", "Assets"},
                                      {"Maps", "Cartes"},
                                      {"Asset", "Asset"},
                                      {"Map", "Carte"},
                                      {"Package", "Package"},
                                      {"Presence", "Présence"},
                                      {"Both revisions", "Deux révisions"},
                                      {"Before only", "Avant uniquement"},
                                      {"After only", "Après uniquement"},
                                      {"Local file", "Fichier local"},
                                      {"Type", "Type"},
                                      {"%zu / %zu packages", "%zu / %zu packages"},
                                      {"No matching package", "Aucun package correspondant"},
                                      {"Change the filter or refresh the content catalogue.",
                                       "Modifiez le filtre ou actualisez le catalogue."},
                                      {"Open package", "Ouvrir le package"},
                                      {"Compare revisions", "Comparer les révisions"},
                                      {"Use before", "Utiliser avant"},
                                      {"Use after", "Utiliser après"}});
}
void CatalogView::set_items(std::shared_ptr<const std::vector<CatalogItem>> items, bool revisions) {
    items_ = std::move(items);
    revisions_ = revisions;
    selected_ = std::numeric_limits<std::size_t>::max();
    dirty_ = true;
}
void CatalogView::set_filter(std::string text) {
    filter_.query = std::move(text);
    dirty_ = true;
}
bool CatalogView::select(std::size_t index) {
    if (!items_ || index >= items_->size())
        return false;
    selected_ = index;
    return true;
}
void CatalogView::filter() {
    if (!dirty_)
        return;
    filtered_.clear();
    if (items_)
        for (std::size_t i = 0; i < items_->size(); ++i) {
            const auto &item = (*items_)[i];
            if ((kind_ == 1 && item.extension != ".uasset") || (kind_ == 2 && item.extension != ".umap"))
                continue;
            if (filter_.matches(item.label.c_str(), item.path.c_str()))
                filtered_.push_back(i);
        }
    if (std::find(filtered_.begin(), filtered_.end(), selected_) == filtered_.end())
        selected_ = std::numeric_limits<std::size_t>::max();
    dirty_ = false;
}
std::size_t CatalogView::visible_count() {
    filter();
    return filtered_.size();
}
CatalogAction CatalogView::draw(const char *id) {
    cyui::ScopedID scope(id);
    if (cyui::filterBox("##asset-search", filter_))
        dirty_ = true;
    const auto all = cyui::tr("All packages"), assets = cyui::tr("Assets"), maps = cyui::tr("Maps");
    const char *kinds[] = {all.c_str(), assets.c_str(), maps.c_str()};
    ImGui::SetNextItemWidth(cyui::dp(170));
    if (ImGui::Combo("##kind", &kind_, kinds, 3))
        dirty_ = true;
    filter();
    ImGui::SameLine();
    cyui::textDim(cyui::tr("%zu / %zu packages").c_str(), filtered_.size(), items_ ? items_->size() : 0);
    CatalogAction action = CatalogAction::none;
    auto height = std::min(cyui::dp(340), ImGui::GetMainViewport()->WorkSize.y * 0.48f);
    if (ImGui::BeginTable("##packages", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable,
                          {0, height})) {
        ImGui::TableSetupColumn(cyui::tr("Package").c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(cyui::tr("Type").c_str(), ImGuiTableColumnFlags_WidthFixed, cyui::dp(65));
        ImGui::TableSetupColumn(cyui::tr("Presence").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                cyui::dp(135));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered_.size()));
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                auto index = filtered_[static_cast<std::size_t>(row)];
                const auto &item = (*items_)[index];
                cyui::ScopedID row_id(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                auto position = ImGui::GetCursorPos();
                if (ImGui::Selectable("##package", selected_ == index,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected_ = index;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        action = CatalogAction::open;
                }
                cyui::tooltip(item.path.c_str());
                ImGui::SetCursorPos(position);
                ImGui::TextUnformatted(item.label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(cyui::tr(item.extension == ".umap" ? "Map" : "Asset").c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(
                    cyui::tr(!revisions_
                                 ? "Local file"
                                 : (item.before && item.after ? "Both revisions"
                                                              : (item.before ? "Before only" : "After only")))
                        .c_str());
            }
        ImGui::EndTable();
    }
    if (filtered_.empty())
        cyui::textDim("%s", cyui::tr("No matching package").c_str());
    const auto *item = items_ && selected_ < items_->size() ? &(*items_)[selected_] : nullptr;
    {
        cyui::ScopedDisabled disabled(!item);
        if (cyui::primaryButton(cyui::trLabel(revisions_ ? "Compare revisions" : "Open package").c_str()))
            action = CatalogAction::open;
    }
    ImGui::SameLine();
    {
        cyui::ScopedDisabled disabled(!item || !item->before);
        if (cyui::button(cyui::trLabel("Use before").c_str()))
            action = CatalogAction::use_before;
    }
    ImGui::SameLine();
    {
        cyui::ScopedDisabled disabled(!item || (revisions_ && !item->after));
        if (cyui::button(cyui::trLabel("Use after").c_str()))
            action = CatalogAction::use_after;
    }
    return action;
}
} // namespace cyuediff
