#include <cyuediff/asset_view.hpp>
#include "animation_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0, frames = 0;
    try {
        auto check = [&](bool v, const char *msg) {
            ++checks;
            if (!v)
                throw std::runtime_error(msg);
        };
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 850};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        auto before = animation_model(), after = before;
        after.exports[0]
            .native_data["curveMetadata"]["containers"]["AnimationCurves"]["Smile~/curve"]["maxLOD"] = 3;
        after.exports[0].native_data["syncMarkers"]["names"] = {"Right", "Left"};
        const auto original = before.to_json();
        SkeletonView view;
        view.set_packages(&before, &after);
        view.set_animation_preview(true);
        auto &metadata = view.animation_view();
        check(metadata.visible_count() == 4, "Exact curve/marker union");
        check(metadata.select_curve("AnimationCurves", "Smile~/curve") && metadata.selected_known(),
              "Select escaped exact curve identity");
        auto saved = view.view_state();
        saved["animation"]["properties"]["filter"] = "maxLOD";
        saved["animation"]["properties"]["selected"] = "/maxLOD";
        view.restore_view_state(saved);
        SkeletonView restored;
        restored.set_packages(&before, &after);
        restored.restore_view_state(saved);
        check(restored.view_state() == saved, "Animation mode, exact row and typed details round trip");
        auto bad = saved;
        bad["animation"]["changedOnly"] = 3;
        bool rejected = false;
        try {
            restored.restore_view_state(bad);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && restored.view_state() == saved, "Invalid nested state rejects atomically");
        bad = saved;
        bad["posePreview"] = true;
        rejected = false;
        try {
            restored.restore_view_state(bad);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && restored.view_state() == saved, "Conflicting modes reject atomically");
        metadata.set_filter("Smile");
        check(metadata.visible_count() == 1, "Curve name search");
        metadata.set_filter("AnimationCurves");
        check(metadata.visible_count() == 2, "Container search");
        metadata.set_filter("");
        metadata.set_changed_only(true);
        check(metadata.visible_count() == 2, "Changed curve/marker filter");
        check(metadata.next_change() && metadata.select_marker("Left"),
              "Change navigation and marker selection");
        auto missing = after;
        missing.exports[0].native_data["curveMetadata"] = {{"coverage", "opaque"}, {"reason", "test"}};
        missing.exports[0].native_data["syncMarkers"] = {{"coverage", "stripped"},
                                                         {"stripFlags", {{"global", 1}, {"class", 0}}}};
        SkeletonView unknown;
        unknown.set_packages(&before, &missing);
        unknown.set_animation_preview(true);
        check(unknown.animation_view().select_marker("Left") && !unknown.animation_view().selected_known(),
              "Unknown opposite marker table never claims removal");
        check(!unknown.animation_view().next_change(), "Unknown rows not claimed as confirmed changes");
        SkeletonView single;
        single.set_packages(nullptr, &before);
        single.set_animation_preview(true);
        check(single.animation_view().select_curve("AnimationCurves", "EyeWeight"),
              "After-only single inspection");
        auto no_metadata = skeleton_model();
        SkeletonView empty;
        empty.set_packages(&no_metadata);
        empty.set_animation_preview(true);
        auto reordered = before;
        reordered.exports[0].native_data["syncMarkers"]["names"] = {"Left", "Left", "Right"};
        SkeletonView order;
        order.set_packages(&before, &reordered);
        order.set_animation_preview(true);
        order.animation_view().set_changed_only(true);
        check(order.animation_view().visible_count() == 0,
              "Marker order alone creates no highlighted change");
        AnimationMetadataView bounded;
        auto many = skeleton_animation_snapshot(before)["skeletons"]["Rig.Skeleton"];
        for (int i = 0; i < 5000; ++i)
            many["syncMarkers"]["names"]["Marker" + std::to_string(i)] = {{"occurrences", 1}};
        bounded.set_models(&many);
        check(bounded.visible_count() == 4096, "Bounded display union");
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const auto *locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                cyui::setLocale(locale);
                for (float screen_width : {1200.f, 560.f})
                    for (int scene = 0; scene < 6; ++scene)
                        for (int frame = 0; frame < 2; ++frame) {
                            io.DisplaySize = {screen_width, 850};
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(io.DisplaySize);
                            ImGui::Begin("Animation review", nullptr, ImGuiWindowFlags_NoDecoration);
                            if (scene == 0)
                                restored.draw();
                            if (scene == 1)
                                view.draw();
                            if (scene == 2)
                                unknown.draw();
                            if (scene == 3)
                                single.draw();
                            if (scene == 4)
                                empty.draw();
                            if (scene == 5)
                                order.draw();
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0,
                                  "Animation metadata renders across locales, themes and widths");
                        }
            }
        check(before.to_json() == original, "Animation UI preserves input snapshots");
        ImGui::DestroyContext();
        std::cout << checks << " animation view checks passed\n"
                  << frames << " animation UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
