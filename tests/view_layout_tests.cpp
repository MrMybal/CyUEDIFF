#include <cyuediff/property_view.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool good, const char *text) {
    ++checks;
    if (!good)
        throw std::runtime_error(text);
}
bool near(double a, double b, double epsilon = .02) {
    return std::abs(a - b) < epsilon;
}
} // namespace
int main() {
    try {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 800};
        io.DeltaTime = 1.f / 60;
        PropertyView::register_translations();
        ViewLayout columns, sections, shell, preview;
        const Json expected = {{"singleColumns", {3, 1, 2}}, {"comparisonColumns", {1, 2, 3, 2, 1}}};
        columns.restore(expected, "property-layout");
        sections.restore({{"diagnostics", true}}, "graph-layout");
        shell.restore({{"sidebarWidth", 620}}, "shell-layout");
        preview.restore({{"previewFraction", .45}}, "property-layout");
        auto retained = columns.snapshot();
        bool rejected = false;
        try {
            columns.restore({{"singleColumns", {0, 1, 2}}}, "property-layout");
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && columns.snapshot() == retained, "Invalid layout leaves current columns intact");
        int frames = 0;
        ImVec2 border{};
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            cyui::Config config;
            config.themeKey = theme;
            cyui::apply(config);
            cyui::loadFonts();
            unsigned char *pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            for (const auto *locale : {"en", "fr"}) {
                cyui::setLocale(locale);
                for (const auto size : {ImVec2(1280, 800), ImVec2(640, 480)}) {
                    io.DisplaySize = size;
                    for (int frame = 0; frame < 3; ++frame) {
                        ImGui::NewFrame();
                        ImGui::SetNextWindowPos({0, 0});
                        ImGui::SetNextWindowSize(size);
                        ImGui::Begin("Layout validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
                        const bool open = sections.section("diagnostics", "Graph diagnostics", false, true);
                        check(open, "Expanded diagnostic section survives frames and locale changes");
                        if (open) {
                            ImGui::TextUnformatted("Fixture diagnostic");
                            ImGui::TreePop();
                        }
                        for (const int count : {3, 5}) {
                            const auto key = count == 3 ? "singleColumns" : "comparisonColumns";
                            if (columns.begin_table("##columns", key, count, ImGuiTableFlags_RowBg,
                                                    {0, 80})) {
                                const char *labels[] = {"Property", "Type", "Before", "After", "Change"};
                                for (int i = 0; i < count; ++i)
                                    columns.column(labels[i], i);
                                ImGui::TableHeadersRow();
                                auto *table = GImGui->CurrentTable;
                                const auto &weights = expected[key];
                                for (int i = 1; i < count; ++i)
                                    check(near(table->Columns[i].StretchWeight /
                                                   table->Columns[0].StretchWeight,
                                               weights[i].get<double>() / weights[0].get<double>()),
                                          "Restored column proportions reach the actual table");
                                if (count == 3)
                                    border = {table->Columns[0].MaxX, table->OuterRect.Min.y + 12};
                                ImGui::TableNextRow();
                                for (int i = 0; i < count; ++i) {
                                    ImGui::TableSetColumnIndex(i);
                                    ImGui::TextUnformatted("Value");
                                }
                                columns.end_table();
                            }
                        }
                        check(columns.snapshot() == expected,
                              "Unchanged column weights retain exact saved data");
                        preview.resize_preview(300);
                        check(near(preview.preview_fraction(.2), .45), "Preview split survives drawing");
                        check(shell.sidebar_width() * cyui::dp(1) <= size.x * .45f + .1f,
                              "Sidebar leaves document space at smaller viewport sizes");
                        check(shell.snapshot()["sidebarWidth"] == 620,
                              "Narrow display preserves requested width for a larger display");
                        ImGui::End();
                        ImGui::Render();
                        check(ImGui::GetDrawData()->TotalVtxCount > 0, "Layout draw data exists");
                        ++frames;
                    }
                }
            }
        }
        // A real mouse drag changes a table width; capture it, then restore on the same instance.
        io.DisplaySize = {1280, 800};
        auto draw_drag = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Drag validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
            if (columns.begin_table("##drag", "singleColumns", 3, ImGuiTableFlags_BordersInnerV, {0, 180})) {
                for (int i = 0; i < 3; ++i)
                    columns.column(i == 0 ? "Property" : i == 1 ? "Type" : "Value", i);
                ImGui::TableHeadersRow();
                auto *table = GImGui->CurrentTable;
                border = {table->Columns[0].MaxX, table->OuterRect.Min.y + 12};
                ImGui::TableNextRow();
                for (int i = 0; i < 3; ++i) {
                    ImGui::TableSetColumnIndex(i);
                    ImGui::TextUnformatted("Value");
                }
                columns.end_table();
            }
            ImGui::End();
            ImGui::Render();
            ++frames;
        };
        draw_drag();
        draw_drag();
        const auto before_drag = columns.snapshot();
        io.AddMousePosEvent(border.x, border.y);
        draw_drag();
        io.AddMouseButtonEvent(0, true);
        draw_drag();
        io.AddMousePosEvent(border.x + 90, border.y);
        draw_drag();
        draw_drag();
        io.AddMouseButtonEvent(0, false);
        draw_drag();
        draw_drag();
        const auto dragged = columns.snapshot();
        check(dragged != before_drag, "Mouse resize is captured as new proportions");
        columns.restore(expected, "property-layout");
        draw_drag();
        draw_drag();
        check(columns.snapshot() == expected, "Restoring layout replaces an existing resized table");
        columns.restore(dragged, "property-layout");
        draw_drag();
        draw_drag();
        check(columns.snapshot() == dragged, "Captured mouse resize restores without drift");
        ImVec2 sidebar_handle{}, preview_handle{}, section_handle{};
        auto draw_handles = [&] {
            ImGui::NewFrame();
            if (cyui::beginSidebar("##resizable-sidebar", shell.sidebar_width())) {
                if (ImGui::BeginChild("##inputs", {-cyui::dp(6), 0}))
                    ImGui::TextUnformatted("Inputs");
                ImGui::EndChild();
                shell.resize_sidebar();
                const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                sidebar_handle = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
            }
            cyui::endSidebar();
            const auto *vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::Begin("Handle validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
            const bool open = sections.section("diagnostics", "Graph diagnostics", false, true);
            auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            section_handle = {a.x + 8, (a.y + b.y) * .5f};
            if (open) {
                ImGui::TextUnformatted("Diagnostic");
                ImGui::TreePop();
            }
            ImGui::Dummy({0, 120});
            preview.resize_preview(300);
            a = ImGui::GetItemRectMin();
            b = ImGui::GetItemRectMax();
            preview_handle = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
            ImGui::End();
            ImGui::Render();
            ++frames;
        };
        draw_handles();
        draw_handles();
        auto drag_handle = [&](ImVec2 origin, ImVec2 delta) {
            io.AddMousePosEvent(origin.x, origin.y);
            draw_handles();
            draw_handles();
            io.AddMouseButtonEvent(0, true);
            draw_handles();
            io.AddMousePosEvent(origin.x + delta.x, origin.y + delta.y);
            draw_handles();
            draw_handles();
            io.AddMouseButtonEvent(0, false);
            draw_handles();
            draw_handles();
        };
        const auto original_width = shell.sidebar_width();
        drag_handle(sidebar_handle, {-60, 0});
        check(shell.sidebar_width() < original_width - 20,
              "Sidebar edge is draggable beside its scrolling content");
        const auto original_fraction = preview.preview_fraction(.2);
        drag_handle(preview_handle, {0, 30});
        check(preview.preview_fraction(.2) < original_fraction - .05,
              "Preview divider changes its saved fraction");
        drag_handle(preview_handle, {0, 1000});
        check(near(preview.preview_fraction(.2), .1), "Preview divider clamps its lower bound");
        drag_handle(section_handle, {0, 0});
        check(sections.snapshot()["diagnostics"] == false,
              "A mouse click can close a restored expanded section");
        const auto handle_state = shell.snapshot();
        cyui::Config scaled;
        scaled.dpiScale = 2;
        cyui::apply(scaled);
        cyui::loadFonts();
        unsigned char *scaled_pixels;
        int sw, sh;
        io.Fonts->GetTexDataAsRGBA32(&scaled_pixels, &sw, &sh);
        draw_handles();
        draw_handles();
        check(shell.sidebar_width() * cyui::dp(1) <= io.DisplaySize.x * .45f + .1f &&
                  shell.snapshot() == handle_state,
              "DPI changes clamp drawn width without changing the saved logical width");
        check(!ImGui::GetIO().WantSaveIniSettings || io.IniFilename == nullptr,
              "No automatic INI file persistence is required");
        ImGui::DestroyContext();
        std::cout << checks << " layout checks passed\n" << frames << " layout UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
