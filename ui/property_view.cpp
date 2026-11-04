#include <cyuediff/property_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
namespace cyuediff {
void PropertyView::register_translations() {
    cyui::registerTranslations("fr", {{"Property preview", "Aperçu de la propriété"},
                                      {"Copy path", "Copier le chemin"},
                                      {"Copy before preview", "Copier l'aperçu avant"},
                                      {"Copy after preview", "Copier l'aperçu après"},
                                      {"Copy value preview", "Copier l'aperçu de la valeur"},
                                      {"No matching property", "Aucune propriété correspondante"},
                                      {"Change the filter or show unchanged properties.",
                                       "Modifiez le filtre ou affichez les propriétés inchangées."},
                                      {"Select a property to inspect its preview.",
                                       "Sélectionnez une propriété pour examiner son aperçu."},
                                      {"Copies the displayed preview.", "Copie l'aperçu affiché."}});
}
void PropertyView::set_filter(std::string text) {
    filter_.query = std::move(text);
    rebuild();
}
void PropertyView::set_changed_only(bool value) {
    changed_only_ = comparing_ && value;
    rebuild();
}
bool PropertyView::select_row(std::size_t index) {
    if (index >= table_.rows.size() ||
        std::find(visible_.begin(), visible_.end(), static_cast<int>(index)) == visible_.end())
        return false;
    selected_ = static_cast<int>(index);
    return true;
}

void PropertyView::rebuild() {
    visible_.clear();
    for (std::size_t i = 0; i < table_.rows.size(); ++i) {
        const auto &row = table_.rows[i];
        auto keywords = row.label + " " + row.type + " " + row.before + " " + row.after;
        if ((!changed_only_ || row.change != ChangeKind::unchanged) && filter_.matches(row.path, keywords))
            visible_.push_back(static_cast<int>(i));
    }
    if (std::find(visible_.begin(), visible_.end(), selected_) == visible_.end())
        selected_ = -1;
}
void PropertyView::set_values(const Json *before, const Json *after, bool comparing) {
    layout_.restore(Json::object(), "property-layout");
    comparing_ = comparing;
    if (!comparing) {
        if (before)
            after = before;
        else
            before = after;
    }
    PropertyTableOptions options;
    options.escape_non_bmp = sizeof(ImWchar) == 2;
    table_ = property_table(before, after, options);
    filter_ = {};
    changed_only_ = comparing;
    selected_ = -1;
    rebuild();
    if (!visible_.empty())
        selected_ = visible_.front();
}
Json PropertyView::view_state() const {
    Json result = {{"filter", filter_.query},
                   {"changedOnly", changed_only_},
                   {"selected", selected_row() ? Json(selected_row()->path) : Json(nullptr)}};
    if (!layout_.snapshot().empty())
        result["layout"] = layout_.snapshot();
    return result;
}
void PropertyView::restore_view_state(const Json &state) {
    cyuediff::view_state::validate(state, "property");
    if (state.contains("layout"))
        layout_.restore(state["layout"], "property-layout");
    if (state.contains("filter"))
        filter_.query = state["filter"].get<std::string>();
    if (state.contains("changedOnly"))
        changed_only_ = comparing_ && state["changedOnly"].get<bool>();
    rebuild();
    if (state.contains("selected")) {
        selected_ = -1;
        if (!state["selected"].is_null())
            for (auto i : visible_)
                if (table_.rows[i].path == state["selected"].get<std::string>()) {
                    selected_ = i;
                    break;
                }
    }
}
void PropertyView::draw(const char *id) {
    cyui::ScopedID scope(id);
    bool update = cyui::filterBox("##property-filter", filter_);
    if (comparing_)
        update = ImGui::Checkbox(cyui::trLabel("Changed only").c_str(), &changed_only_) || update;
    if (update)
        rebuild();
    if (table_.truncated)
        cyui::callout(cyui::Tone::warning, cyui::tr("Property table reached its display limit.").c_str());
    if (visible_.empty()) {
        cyui::emptyState(cyui::tr("No matching property").c_str(),
                         cyui::tr("Change the filter or show unchanged properties.").c_str());
        return;
    }
    auto remaining = ImGui::GetContentRegionAvail();
    const auto fraction = layout_.preview_fraction(std::min(cyui::dp(180) / std::max(1.f, remaining.y), .4f));
    auto detail_height = std::max(1.f, remaining.y) * fraction;
    if (layout_.begin_table("##typed-properties", comparing_ ? "comparisonColumns" : "singleColumns",
                            comparing_ ? 5 : 3,
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                            ImVec2(remaining.x, std::max(ImGui::GetFrameHeight() * 2,
                                                         remaining.y - detail_height - cyui::dp(6) -
                                                             ImGui::GetStyle().ItemSpacing.y * 2)))) {
        int column = 0;
        for (auto label : {"Property", "Type", comparing_ ? "Before" : "Value"})
            layout_.column(label, column++);
        if (comparing_) {
            layout_.column("After", 3);
            layout_.column("Change", 4);
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto &row = table_.rows[visible_[i]];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                cyui::ScopedID row_id(visible_[i]);
                auto x = ImGui::GetCursorPosX() + cyui::dp(float(std::min(row.depth, 12u)) * 6.f);
                ImGui::SetCursorPosX(x);
                if (ImGui::Selectable("##property-row", selected_ == visible_[i],
                                      ImGuiSelectableFlags_SpanAllColumns))
                    selected_ = visible_[i];
                cyui::tooltip(row.path.c_str());
                ImGui::SameLine();
                ImGui::SetCursorPosX(x);
                ImGui::TextUnformatted(row.label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.type.c_str());
                cyui::tooltip(row.type.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.before.c_str());
                cyui::tooltip(row.before.c_str());
                if (comparing_) {
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(row.after.c_str());
                    cyui::tooltip(row.after.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::PushStyleColor(ImGuiCol_Text, detail::change_color(row.change));
                    ImGui::TextUnformatted(cyui::tr(change_name(row.change)).c_str());
                    ImGui::PopStyleColor();
                }
            }
        layout_.end_table();
    }
    layout_.resize_preview(remaining.y);
    if (ImGui::BeginChild("##property-preview", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (const auto *row = selected_row()) {
            cyui::sectionTitle(cyui::tr("Property preview").c_str());
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(row->path.empty() ? "/" : row->path.c_str());
            ImGui::PopTextWrapPos();
            if (cyui::button(cyui::trLabel("Copy path").c_str()))
                ImGui::SetClipboardText(row->path.empty() ? "/" : row->path.c_str());
            auto copy = [&](const char *label, const std::string &value, bool present) {
                const auto translated = cyui::trLabel(label);
                const auto &style = ImGui::GetStyle();
                const auto width = ImGui::CalcTextSize(translated.c_str()).x + style.FramePadding.x * 2;
                const auto right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
                if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
                    ImGui::SameLine();
                cyui::ScopedDisabled disabled(!present);
                if (cyui::button(translated.c_str()))
                    ImGui::SetClipboardText(value.c_str());
                cyui::tooltip(cyui::tr("Copies the displayed preview.").c_str());
            };
            copy(comparing_ ? "Copy before preview" : "Copy value preview", row->before, row->before_present);
            if (comparing_)
                copy("Copy after preview", row->after, row->after_present);
            if (cyui::beginProperties("##preview-values")) {
                cyui::property(cyui::tr("Type").c_str());
                ImGui::TextUnformatted(row->type.c_str());
                cyui::property(cyui::tr(comparing_ ? "Before" : "Value").c_str());
                ImGui::TextWrapped("%s", row->before_present ? row->before.c_str() : "—");
                if (comparing_) {
                    cyui::property(cyui::tr("After").c_str());
                    ImGui::TextWrapped("%s", row->after_present ? row->after.c_str() : "—");
                }
                cyui::endProperties();
            }
        } else
            cyui::textDim(cyui::tr("Select a property to inspect its preview.").c_str());
    }
    ImGui::EndChild();
}
} // namespace cyuediff
