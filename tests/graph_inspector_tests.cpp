#include <cyuediff/graph_inspector.hpp>
#include <iostream>
using namespace cyuediff;
int main() {
    int checks = 0, frames = 0;
    try {
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 850};
        io.DeltaTime = 1.f / 60;
        GraphInspector::register_translations();
        PropertyView::register_translations();
        Json before = {{"a", {{"properties", {{"Power", {{"value", 1}, {"type", "IntProperty"}}}}}}},
                       {"b", {{"pins", {{"pin", {{"defaultText", "Old"}}}}}}},
                       {"c", nullptr}};
        auto after = before;
        after["a"]["properties"]["Power"]["value"] = 2;
        after.erase("b");
        std::vector<GraphInspectionItem> items = {{1, "a", "Power", ChangeKind::modified},
                                                  {2, "b", "Removed node", ChangeKind::removed},
                                                  {3, "c", "Null node", ChangeKind::unchanged}};
        GraphInspector view;
        view.set_nodes(&before, &after, items, true);
        check(view.visible_count() == 3 && view.view_state().empty(), "Unselected default is compact");
        check(view.next_change() && view.selected_node() == "a", "First changed node");
        check(view.details().rows.size() > 2, "Prepared typed node properties");
        check(view.next_change() && view.selected_node() == "b", "Next removed node");
        check(view.details().rows.front().change == ChangeKind::removed,
              "Removed node retains before details");
        check(view.next_change() && view.selected_node() == "a", "Forward wrap");
        check(view.next_change(true) && view.selected_node() == "b", "Backward wrap");
        view.set_filter("Power");
        check(view.visible_count() == 1 && view.selected_node().empty(), "Filter clears hidden selection");
        check(!view.select_node("b") && view.next_change() && view.selected_node() == "a",
              "Navigation respects search");
        auto saved = view.view_state();
        auto renumbered = items;
        renumbered[0].id = 999;
        GraphInspector resumed;
        resumed.set_nodes(&before, &after, renumbered, true);
        resumed.restore_view_state(saved);
        check(resumed.selected_canvas_id() == 999 && resumed.view_state() == saved,
              "Saved identities survive canvas ID changes");
        auto invalid = saved;
        invalid["properties"]["changedOnly"] = 1;
        bool rejected = false;
        try {
            resumed.restore_view_state(invalid);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && resumed.view_state() == saved, "Nested invalid state is atomic");
        resumed.set_nodes(nullptr, &after, {{1, "a", "Power", ChangeKind::added}}, true);
        resumed.restore_view_state(saved);
        check(resumed.details().rows.front().change == ChangeKind::added, "Added node retains after details");
        resumed.set_nodes(&before, nullptr, items, false);
        resumed.set_changed_only(true);
        check(!resumed.view_state().contains("changedOnly"), "Single package cannot hide unchanged nodes");
        view.set_filter("");
        view.set_changed_only(true);
        check(view.visible_count() == 2, "Changed filter omits unchanged JSON null");
        view.select_node("a");
        std::vector<cyui::Node> nodes = {
            {1, {0, 0}, "Power"}, {2, {300, 0}, "Removed node"}, {3, {0, 200}, "Null node"}};
        std::map<cyui::ItemId, ChangeKind> changes = {{1, ChangeKind::modified}, {2, ChangeKind::removed}};
        cyui::NodeState state;
        bool fit = true;
        const auto original = before;
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            cyui::Config config;
            config.themeKey = theme;
            cyui::apply(config);
            cyui::loadFonts();
            unsigned char *pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            for (const auto *locale : {"en", "fr"})
                for (int width : {1200, 640})
                    for (int n = 0; n < 2; ++n) {
                        cyui::setLocale(locale);
                        io.DisplaySize = {float(width), 850};
                        ImGui::NewFrame();
                        ImGui::SetNextWindowPos({0, 0});
                        ImGui::SetNextWindowSize(io.DisplaySize);
                        ImGui::Begin("Graph inspection");
                        view.draw(state, nodes, {}, changes, fit, {0, 0}, {510, 300});
                        ImGui::End();
                        ImGui::Render();
                        check(ImGui::GetDrawData()->TotalVtxCount > 0, "Responsive graph draw");
                        ++frames;
                    }
        }
        check(before == original, "Read-only graph interaction leaves snapshots intact");
        ImGui::DestroyContext();
        std::cout << checks << " graph inspector checks passed\n"
                  << frames << " graph inspector frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
