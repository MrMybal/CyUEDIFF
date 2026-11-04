#include "../apps/desktop/document_tabs.hpp"
#include <iostream>
using namespace cyuediff::desktop;
int main() {
    try {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1100, 700);
        io.DeltaTime = 1.f / 60;
        io.IniFilename = nullptr;
        DocumentTabsView::register_translations();
        cyui::setLocale("fr");
        if (cyui::tr("Reopen closed document") != "Rouvrir le document ferm\u00e9")
            throw std::runtime_error("French document actions");
        DocumentTabsView view;
        int frames = 0;
        for (const auto &theme : cyui::builtInThemes())
            for (auto language : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme.key.c_str();
                cyui::apply(config);
                cyui::setLocale(language);
                unsigned char *pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                for (int scene = 0; scene < 6; ++scene) {
                    std::vector<DocumentTab> tabs;
                    if (scene != 5) {
                        tabs = {
                            {1, "Same.uasset", "Before/Same.uasset", false, false},
                            {2, "Same.uasset", "After/Same.uasset", false, false},
                            {3, "name###id\u00e9.utoc", "Literal delimiters in source names", true, false}};
                        if (scene == 4)
                            for (int j = 4; j <= 8; ++j)
                                tabs.push_back({static_cast<DocumentId>(j), std::string(80, 'x') + ".pak",
                                                "Long member", false, j == 8});
                    }
                    const auto active = scene == 5 ? DocumentId(0) : DocumentId(scene % 3 + 1);
                    view.set_tabs(std::move(tabs), active, scene != 4);
                    for (int frame = 0; frame < 2; ++frame) {
                        ImGui::NewFrame();
                        ImGui::SetNextWindowSize(ImVec2(1000, 600));
                        ImGui::Begin("Documents");
                        const auto action = view.draw();
                        if (action.operation != DocumentTabOperation::none)
                            throw std::runtime_error(
                                "Programmatic tab changes do not select the previous document");
                        ImGui::End();
                        ImGui::Render();
                        if (ImGui::GetDrawData()->TotalVtxCount <= 0)
                            throw std::runtime_error("Document tab drawing");
                        ++frames;
                    }
                }
            }
        ImGui::DestroyContext();
        std::cout << frames << " document tab UI frames validated (five themes, English/French)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
