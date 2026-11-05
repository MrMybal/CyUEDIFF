#include <cyuediff/asset_view.hpp>
#include "skeleton_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
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
        AssetView::register_translations();
        auto before = skeleton_model(), after = before;
        after.exports[0].native_data["localPoses"][1]["translation"][0] = 12.;
        const auto original = before.to_json();
        SkeletonView view;
        view.set_packages(&before, &after);
        check(!view.empty() && view.next_changed_bone(), "Changed raw bone navigation");
        auto saved = view.view_state();
        check(saved["inspection"]["selected"] == "arm", "Stable selected bone name");
        saved["canvas"] = {{"pan", {-20., -30.}}, {"zoom", 1.1}};
        saved["inspection"]["properties"]["filter"] = "translation";
        saved["inspection"]["properties"]["selected"] = "/localPose/translation/0";
        view.restore_view_state(saved);
        SkeletonView restored;
        restored.set_packages(&before, &after);
        restored.restore_view_state(saved);
        check(restored.view_state() == saved, "Hierarchy camera and bone/property selection round trip");
        auto invalid = saved;
        invalid["canvas"]["zoom"] = 0;
        bool rejected = false;
        try {
            restored.restore_view_state(invalid);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && restored.view_state() == saved, "Malformed nested state rejects atomically");
        auto reorder = before;
        auto &native = reorder.exports[0].native_data;
        std::swap(native["bones"][1], native["bones"][2]);
        std::swap(native["localPoses"][1], native["localPoses"][2]);
        native["nameToIndex"] = {{"root", 0}, {"tip", 1}, {"arm", 2}};
        restored.set_packages(&reorder, &after);
        restored.restore_view_state(saved);
        check(restored.view_state() == saved, "Selection survives sibling source ordering");
        view.set_filter("tip");
        check(view.visible_count() == 1 && !view.select_bone("arm"),
              "Bone search uses exact visible identities");
        view.set_changed_only(true);
        check(!view.visible_count() && !view.next_changed_bone(), "Changed navigation respects search");
        view.restore_view_state(saved);
        SkeletonView empty, added, missing;
        empty.set_packages(nullptr);
        added.set_packages(nullptr, &after, true);
        auto partial = before;
        partial.exports[0].native_data = Json::object();
        partial.exports[0].native_coverage = "opaque";
        missing.set_packages(&partial);
        check(empty.empty() && !added.empty() && !missing.empty(),
              "Empty, added and opaque hierarchy states");
        restored.restore_view_state({{"object", "missing"}, {"inspection", {{"selected", "arm"}}}});
        check(restored.view_state()["object"].is_null() &&
                  !restored.view_state()["inspection"].contains("selected"),
              "Missing object cannot select an unrelated bone");
        AssetView asset;
        asset.set_packages(std::make_shared<Package>(before), std::make_shared<Package>(after));
        asset.restore_view_state({{"skeleton", saved}});
        asset.select_tab(AssetTab::skeleton);
        check(asset.active_tab() == AssetTab::skeleton && asset.view_state()["skeleton"] == saved,
              "Asset tab binds saved hierarchy state");
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
                    for (int scene = 0; scene < 5; ++scene)
                        for (int n = 0; n < 2; ++n) {
                            cyui::setLocale(locale);
                            io.DisplaySize = {float(width), 850};
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(io.DisplaySize);
                            ImGui::Begin("Skeleton inspection");
                            if (scene == 0)
                                view.draw();
                            if (scene == 1)
                                asset.draw("Asset", false);
                            if (scene == 2)
                                empty.draw();
                            if (scene == 3)
                                added.draw();
                            if (scene == 4)
                                missing.draw();
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0,
                                  "Hierarchy draws in each theme/language/width");
                        }
        }
        check(before.to_json() == original, "Hierarchy UI is read-only");
        ImGui::DestroyContext();
        std::cout << checks << " skeleton view checks passed\n"
                  << frames << " skeleton UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
