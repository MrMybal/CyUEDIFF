#include "../apps/desktop/history_view.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 900);
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    HistoryView::register_translations();
    auto document = std::make_shared<HistoryDocument>();
    document->page.head = std::string(40, 'a');
    document->page.commits.push_back(
        {std::string(40, 'a'), "Auteur é", "2026-01-01T12:30:00+01:00", "Subject ### 100%", {}});
    document->page.commits.push_back(
        {std::string(40, 'b'), "Other", "2026-01-01T11:30:00+01:00", "Another subject", {}});
    HistoryView view;
    view.set_document(document);
    if (view.visible_count() != 2 || !view.selected() || !view.select_commit(std::string(40, 'b')))
        return 1;
    view.set_filter("auteur");
    if (view.visible_count() != 1 || view.selected() || view.select_commit(std::string(40, 'b')) ||
        !view.select_commit(std::string(40, 'a')))
        return 2;
    int frames = 0;
    for (const auto &theme : cyui::builtInThemes())
        for (auto locale : {"en", "fr"}) {
            cyui::Config config;
            config.themeKey = theme.key.c_str();
            cyui::apply(config);
            cyui::setLocale(locale);
            unsigned char *pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            for (int state = 0; state < 3; ++state) {
                ImGui::NewFrame();
                ImGui::SetNextWindowSize(ImVec2(1100, 780));
                ImGui::Begin("History");
                view.draw(state == 1, state == 2 ? "git-history: unavailable" : "", document->page.head,
                          std::string(40, 'b'), true);
                ImGui::End();
                ImGui::Render();
                if (ImGui::GetDrawData()->TotalVtxCount <= 0)
                    return 3;
                ++frames;
            }
        }
    view.set_filter("nothing matches");
    if (view.visible_count() || view.selected())
        return 4;
    view.set_document({});
    if (view.selected() || view.visible_count())
        return 5;
    ImGui::DestroyContext();
    std::cout << frames << " history UI frames validated (five themes, English/French)\n";
    return 0;
}
