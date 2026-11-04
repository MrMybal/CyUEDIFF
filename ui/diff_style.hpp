#pragma once
#include <cyuediff/visual_diff.hpp>
#include <cyui/modules/Nodes.h>
namespace cyuediff::detail {
inline ImU32 change_color(ChangeKind change) {
    const auto &p = cyui::palette();
    switch (change) {
    case ChangeKind::added:
        return p.success;
    case ChangeKind::removed:
        return p.error;
    case ChangeKind::modified:
        return p.warning;
    default:
        return p.textDim;
    }
}
inline void change_legend(const ChangeCounts &counts) {
    for (auto item : {std::make_pair(ChangeKind::added, counts.added),
                      std::make_pair(ChangeKind::removed, counts.removed),
                      std::make_pair(ChangeKind::modified, counts.modified)}) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, change_color(item.first));
        ImGui::Text("%s: %zu", cyui::tr(change_name(item.first)).c_str(), item.second);
        ImGui::PopStyleColor();
    }
}
inline void node_marker(const cyui::Node &node, const cyui::Rect &rect,
                        const std::map<cyui::ItemId, ChangeKind> &changes) {
    auto it = changes.find(node.id);
    if (it == changes.end() || it->second == ChangeKind::unchanged)
        return;
    auto color = change_color(it->second);
    auto *draw = ImGui::GetWindowDrawList();
    ImVec2 a{float(rect.min.x), float(rect.min.y)}, b{float(rect.max.x), float(rect.max.y)};
    draw->AddRect(a, b, color, cyui::dp(2), 0, cyui::dp(2));
}
} // namespace cyuediff::detail
