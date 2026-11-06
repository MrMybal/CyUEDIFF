#include <cyuediff/view_layout.hpp>
// ImGui exposes column setup but not the resulting stretch weights publicly.
// Keep this read-only dependency here; no internal IDs or settings blobs are serialized.
#include <imgui_internal.h>
#include <algorithm>
namespace cyuediff {
void ViewLayout::restore(const Json &state, std::string_view kind) {
    view_state::validate(state, kind);
    state_ = state;
    ++generation_;
}
bool ViewLayout::section(const char *key, const char *label, bool default_open, bool tree, bool remember_default) {
    if (state_.contains(key))
        ImGui::SetNextItemOpen(state_[key].get<bool>(), ImGuiCond_Always);
    const auto translated = cyui::trLabel(label);
    const bool open =
        tree ? ImGui::TreeNode(translated.c_str()) : cyui::beginSection(translated.c_str(), default_open);
    if (remember_default || state_.contains(key) || open != default_open)
        state_[key] = open;
    return open;
}
bool ViewLayout::begin_table(const char *id, const char *key, int count, ImGuiTableFlags flags, ImVec2 size) {
    ImGui::PushID(this);
    ImGui::PushID(static_cast<int>(generation_));
    ImGui::PushID(count);
    if (!ImGui::BeginTable(id, count, flags | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings,
                           size)) {
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        return false;
    }
    columns_ = key;
    return true;
}
void ViewLayout::column(const char *label, int index, float default_weight) {
    const auto it = state_.find(columns_);
    const float weight = it != state_.end() && index < static_cast<int>(it->size())
                             ? (*it)[index].get<float>()
                             : default_weight;
    ImGui::TableSetupColumn(cyui::trLabel(label).c_str(), ImGuiTableColumnFlags_WidthStretch, weight);
}
void ViewLayout::end_table() {
    const auto *table = GImGui->CurrentTable;
    if (table && table->IsLayoutLocked) {
        Json weights = Json::array();
        double sum = 0;
        for (const auto &c : table->Columns)
            sum += std::max(.0001f, c.StretchWeight);
        bool changed = !state_.contains(columns_);
        for (int i = 0; i < table->ColumnsCount; ++i) {
            const double value = std::clamp(
                std::max(.0001f, table->Columns[i].StretchWeight) * table->ColumnsCount / sum, .0001, 1000.);
            weights.push_back(value);
            if (!changed) {
                const auto &old = state_[columns_];
                double old_sum = 0;
                for (const auto &w : old)
                    old_sum += w.get<double>();
                changed = old.size() != static_cast<std::size_t>(table->ColumnsCount) ||
                          std::abs(old[i].get<double>() * table->ColumnsCount / old_sum - value) > .0001;
            }
        }
        if (changed)
            state_[columns_] = std::move(weights);
    }
    ImGui::EndTable();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
}
float ViewLayout::preview_fraction(float fallback) const {
    return state_.value("previewFraction", std::clamp(fallback, .1f, .7f));
}
void ViewLayout::resize_preview(float height) {
    if (height <= 0)
        return;
    auto fraction = preview_fraction(std::min(cyui::dp(180) / height, .4f));
    bool changed = !state_.contains("previewFraction");
    const auto position = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##preview-resize",
                           {std::max(1.f, ImGui::GetContentRegionAvail().x), cyui::dp(6)});
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y != 0) {
        fraction = std::clamp(fraction - ImGui::GetIO().MouseDelta.y / height, .1f, .7f);
        changed = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        fraction = std::min(cyui::dp(180) / height, .4f);
        changed = true;
    }
    if (changed)
        state_["previewFraction"] = std::clamp(fraction, .1f, .7f);
    const auto color = ImGui::IsItemActive() ? cyui::palette().accent : cyui::palette().lineStrong;
    ImGui::GetWindowDrawList()->AddLine({position.x, position.y + cyui::dp(3)},
                                        {position.x + ImGui::GetItemRectSize().x, position.y + cyui::dp(3)},
                                        ImGui::GetColorU32(color));
}
float ViewLayout::sidebar_width() const {
    const float maximum =
        std::max(1.f, std::min(640.f, ImGui::GetMainViewport()->Size.x / cyui::dp(1) * .45f));
    return std::clamp(state_.value("sidebarWidth", 310.f), std::min(180.f, maximum), maximum);
}
void ViewLayout::resize_sidebar() {
    if (!state_.contains("sidebarWidth"))
        state_["sidebarWidth"] = 310;
    const auto old_cursor = ImGui::GetCursorScreenPos();
    const auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const auto padding = ImGui::GetStyle().WindowPadding;
    ImGui::SetCursorScreenPos({pos.x + size.x - padding.x - cyui::dp(6), pos.y + padding.y});
    ImGui::InvisibleButton("##sidebar-resize", {cyui::dp(6), std::max(1.f, size.y - padding.y * 2)});
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive())
        state_["sidebarWidth"] =
            std::clamp(sidebar_width() + ImGui::GetIO().MouseDelta.x / cyui::dp(1), 180.f, 640.f);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        state_["sidebarWidth"] = 310;
    ImGui::SetCursorScreenPos(old_cursor);
}
} // namespace cyuediff
