#include <cyuediff/property_browser_view.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool x, const char *message) {
    ++checks;
    if (!x)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        PropertyBrowserView::register_translations();
        Json a = {{"items", {{"type", "ArrayProperty"}, {"value", Json::array()}}}, {"z", 0}};
        for (int i = 0; i < 40001; ++i)
            a["items"]["value"].push_back({{"length", 1.25}, {"particle", i}});
        auto b = a;
        b["items"]["value"][40000]["length"] = 2.5;
        PropertyBrowserView browser;
        browser.set_values(&a, &b, true);
        check(browser.page().table.rows.size() == 3 && !browser.page().table.truncated,
              "Bounded browser binding");
        check(browser.properties().select_row(1) && browser.open_selected(&a, &b),
              "Open selected typed array");
        check(browser.page().scope == "/items" && browser.page().total_children == 40001,
              "Typed value child page");
        check(browser.go_to_index(&a, &b, 40000), "Go directly to last array element");
        check(browser.properties().selected_row()->path == "/items/value/40000",
              "Index jump selects its exact row");
        const auto last_page = browser.view_state();
        check(!browser.go_to_index(&a, &b, 40001) && browser.view_state() == last_page,
              "Out-of-range index changes no state");
        check(browser.properties().select_row(65) && browser.open_selected(&a, &b),
              "Open last changed record beyond old cap");
        check(browser.page().scope == "/items/value/40000" && browser.properties().select_row(1),
              "Exact last-record field selection");
        const auto saved = browser.view_state();
        PropertyBrowserView restored;
        restored.set_values(&a, &b, true);
        restored.restore_view_state(&a, &b, saved);
        check(restored.view_state() == saved && restored.properties().selected_row()->after == "2.5",
              "Scope restores before relative selection");
        const auto retained = restored.view_state();
        check(!restored.navigate(&a, &b, "/missing") && restored.view_state() == retained,
              "Missing scope leaves current view intact");
        auto stale = saved;
        stale["scope"] = "/missing";
        restored.restore_view_state(&a, &b, stale);
        check(restored.page().scope.empty() && restored.properties().selected_row()->path.empty(),
              "Stale scope returns to root without unrelated child selection");
        auto invalid = saved;
        invalid["scope"] = "/~2";
        const auto intact = restored.view_state();
        try {
            restored.restore_view_state(&a, &b, invalid);
            throw std::runtime_error("Expected invalid state");
        } catch (const Error &e) {
            check(e.code == "view-state" && restored.view_state() == intact, "Validate before mutation");
        }
        Json qualified = {
            {"/Script/Test.Asset.Nested", {{"type", "ArrayProperty"}, {"value", Json::array({1, 2})}}}};
        PropertyBrowserView named;
        named.set_values(&qualified, nullptr, false);
        check(named.properties().table().rows[1].label == "Nested" &&
                  named.page().table.rows[1].label == "/Script/Test.Asset.Nested",
              "UI names stay readable while exact Core keys and paths are retained");
        PropertyBrowserView detached;
        {
            Json short_lived = {{"owned", 123}};
            detached.set_values(&short_lived, nullptr, false);
        }
        check(detached.page().table.rows[1].before == "123",
              "Prepared presentation owns strings after input lifetime");
        // Unknown peers are explicitly normalized by the owner before entering this reusable browser.
        restored.set_values(&a, nullptr, false);
        check(restored.navigate(&a, nullptr, "/items", 39936),
              "Single known snapshot can still be navigated");
        for (const auto &row : restored.page().table.rows)
            check(row.change == ChangeKind::unchanged, "Single snapshot never invents removed values");
        restored.restore_view_state(&a, nullptr,
                                    {{"scope", "/items"}, {"serializationFields", true}, {"first", 0}});
        check(!restored.page().unwrapped && restored.page().total_children == 2,
              "Serialization fields are visible");
        int frames = 0;
        // Click the actual root control with a live selected-row preview; rebind occurs after drawing.
        cyui::setLocale("en");
        io.DisplaySize = {1280, 800};
        unsigned char *atlas;
        int aw, ah;
        io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
        ImVec2 root_button{};
        auto draw_click = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Click validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
            const auto p = ImGui::GetCursorScreenPos();
            root_button = {p.x + 20, p.y + ImGui::GetFrameHeight() * .5f};
            browser.draw(&a, &b);
            ImGui::End();
            ImGui::Render();
            ++frames;
        };
        draw_click();
        draw_click();
        io.AddMousePosEvent(root_button.x, root_button.y);
        draw_click();
        draw_click();
        io.AddMouseButtonEvent(0, true);
        draw_click();
        io.AddMouseButtonEvent(0, false);
        draw_click();
        draw_click();
        check(browser.page().scope.empty(), "Mouse root control applies deferred navigation safely");
        for (const auto &theme : cyui::builtInThemes())
            for (const auto *locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme.key.c_str();
                cyui::apply(config);
                cyui::setLocale(locale);
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                for (const auto size : {ImVec2(1280, 800), ImVec2(640, 520)}) {
                    io.DisplaySize = size;
                    browser.restore_view_state(&a, &b, last_page);
                    for (int i = 0; i < 3; ++i) {
                        ImGui::NewFrame();
                        ImGui::SetNextWindowPos({0, 0});
                        ImGui::SetNextWindowSize(size);
                        ImGui::Begin("Paged array", nullptr, ImGuiWindowFlags_NoSavedSettings);
                        browser.draw(&a, &b);
                        ImGui::End();
                        ImGui::Render();
                        ++frames;
                    }
                    check(browser.properties().selected_row()->path == "/items/value/40000",
                          "Clipped array selection is retained");
                    browser.restore_view_state(&a, &b, saved);
                    for (int i = 0; i < 3; ++i) {
                        ImGui::NewFrame();
                        ImGui::SetNextWindowPos({0, 0});
                        ImGui::SetNextWindowSize(size);
                        ImGui::Begin("Property browser", nullptr, ImGuiWindowFlags_NoSavedSettings);
                        browser.draw(&a, &b);
                        ImGui::End();
                        ImGui::Render();
                        ++frames;
                        check(ImGui::GetDrawData()->TotalVtxCount > 0,
                              "Browser renders in compact and full layouts");
                        check(browser.properties().selected_row()->after == "2.5",
                              "Rendering retains the independently selected value");
                    }
                }
            }
        check(cyui::tr("Serialization fields") == "Champs de sérialisation" &&
                  cyui::tr("Go to index") == "Aller à l'indice",
              "Complete French browser labels");
        ImGui::DestroyContext();
        std::cout << checks << " property browser checks, " << frames << " frames passed\n";
    } catch (const std::exception &e) {
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
