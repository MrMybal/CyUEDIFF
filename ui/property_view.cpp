#include <cyuediff/property_view.hpp>
#include "diff_style.hpp"
namespace cyuediff {
void PropertyView::rebuild() {
    visible_.clear();
    for (std::size_t i = 0; i < table_.rows.size(); ++i) {
        const auto &row = table_.rows[i];
        if ((!changed_only_ || row.change != ChangeKind::unchanged) &&
            filter_.matches(row.path.c_str(), row.type.c_str()))
            visible_.push_back(static_cast<int>(i));
    }
}
void PropertyView::set_values(const Json *before, const Json *after, bool comparing) {
    comparing_ = comparing;
    if (!comparing) {
        if (before)
            after = before;
        else
            before = after;
    }
    table_ = property_table(before, after);
    filter_ = {};
    changed_only_ = comparing;
    rebuild();
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
    if (ImGui::BeginTable("##typed-properties", comparing_ ? 5 : 3,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                          ImGui::GetContentRegionAvail())) {
        for (auto label : {"Property", "Type", comparing_ ? "Before" : "Value"})
            ImGui::TableSetupColumn(cyui::tr(label).c_str());
        if (comparing_) {
            ImGui::TableSetupColumn(cyui::tr("After").c_str());
            ImGui::TableSetupColumn(cyui::tr("Change").c_str());
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
                auto slash = row.path.find_last_of('/');
                const char *label =
                    row.path.empty() ? "/" : row.path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     cyui::dp(float(std::min(row.depth, 12u)) * 6.f));
                ImGui::TextUnformatted(label);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", row.path.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.type.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", row.type.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.before.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", row.before.c_str());
                if (comparing_) {
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(row.after.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", row.after.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::PushStyleColor(ImGuiCol_Text, detail::change_color(row.change));
                    ImGui::TextUnformatted(cyui::tr(change_name(row.change)).c_str());
                    ImGui::PopStyleColor();
                }
            }
        ImGui::EndTable();
    }
}
} // namespace cyuediff
