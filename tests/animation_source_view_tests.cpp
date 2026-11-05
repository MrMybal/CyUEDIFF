#include <cyuediff/asset_view.hpp>
#include "animation_source_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0, frames = 0;
    try {
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 1000};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        auto a = animation_source_model(), b = a;
        b.exports[0]
            .properties["BoneAnimationTracks"]["value"][1]["InternalTrackData"]["value"]["positions"][1][0] =
            12.;
        const auto original = a.exports[0].properties;
        AnimationSourceView view;
        view.set_packages(&a, &b);
        check(view.visible_count() == 2, "Two named tracks");
        check(view.select_track("name:arm~/track") && view.selected_known(), "Escaped named track selection");
        view.set_frame(1);
        check(view.frame_sample()["positions"][0] == 10. && view.frame_sample(true)["positions"][0] == 12.,
              "Synchronized source frame");
        check(view.curve_samples() == 3, "Selected channel prepared outside draw");
        view.set_changed_only(true);
        check(view.visible_count() == 1 && view.next_change(), "Confirmed changed track navigation");
        auto state = view.view_state();
        state["inspection"]["properties"]["filter"] = "positions";
        state["inspection"]["properties"]["selected"] = "/positions/0";
        view.restore_view_state(state);
        AnimationSourceView restored;
        restored.set_packages(&a, &b);
        restored.restore_view_state(state);
        check(restored.view_state() == state,
              "Models, track, frame, component and typed property filter retained");
        const auto saved = restored.view_state();
        auto invalid = saved;
        invalid["frame"] = 1.5;
        bool rejected = false;
        try {
            restored.restore_view_state(invalid);
        } catch (const Error &e) {
            rejected = e.code == "view-state";
        }
        check(rejected && restored.view_state() == saved, "Invalid state restore atomic");
        auto short_after = b;
        short_after.exports[0].properties["NumberOfKeys"]["value"] = 1;
        AnimationSourceView short_view;
        short_view.set_packages(&a, &short_after);
        short_view.select_track("name:root");
        short_view.set_frame(2);
        check(short_view.frame_sample()["ready"] && !short_view.frame_sample(true)["ready"].get<bool>(),
              "Shorter side unavailable, never clamped");
        auto unknown = b;
        unknown.exports[0].properties.erase("BoneAnimationTracks");
        AnimationSourceView unknown_view;
        unknown_view.set_packages(&a, &unknown);
        unknown_view.select_track("name:root");
        check(!unknown_view.selected_known(), "Unknown opposite table never claims removal");
        AnimationSourceView missing;
        missing.set_packages(&a, &b);
        check(!missing.select_models("missing", "Clip.Model"), "Missing model choice retained");
        check(missing.view_state()["beforeObject"] == "missing", "No fallback to another model");
        auto added_state = view.view_state();
        AnimationSourceView added;
        added.set_packages(nullptr, &b, true);
        added.select_track("name:root");
        check(added.selected_known(), "Known absent package side supports additions");
        AnimationSourceView single;
        single.set_packages(&a);
        single.select_track("name:arm~/track");
        single.set_frame(2);
        AssetView asset;
        asset.set_packages(std::make_shared<Package>(a), std::make_shared<Package>(b));
        asset.select_tab(AssetTab::animations);
        auto integrated = asset.view_state();
        integrated["animations"] = state;
        asset.restore_view_state(integrated);
        check(asset.view_state()["animations"] == state,
              "Asset and workspace state includes animation source");
        for (const char *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const char *locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                cyui::setLocale(locale);
                for (float width : {1200.f, 600.f})
                    for (unsigned scene = 0; scene < 7; ++scene)
                        for (unsigned frame = 0; frame < 2; ++frame) {
                            io.DisplaySize = {width, 1000};
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(io.DisplaySize);
                            ImGui::Begin("Animation source inspection", nullptr,
                                         ImGuiWindowFlags_NoDecoration);
                            if (scene == 0)
                                restored.draw();
                            if (scene == 1)
                                short_view.draw();
                            if (scene == 2)
                                unknown_view.draw();
                            if (scene == 3)
                                missing.draw();
                            if (scene == 4)
                                added.draw();
                            if (scene == 5)
                                single.draw();
                            if (scene == 6)
                                asset.draw("Asset", false);
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0,
                                  "Source animation renders across themes, locales and widths");
                        }
            }
        check(a.exports[0].properties == original, "Drawing leaves source package unchanged");
        ImGui::DestroyContext();
        std::cout << checks << " animation source view checks passed\n"
                  << frames << " animation UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
