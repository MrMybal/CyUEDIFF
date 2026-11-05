#include <cyuediff/asset_view.hpp>
#include "rich_curve_model.hpp"
#include "transform_curve_model.hpp"
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
        auto a = rich_model(), b = a;
        const auto original = a.exports[0].properties;
        b.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][1]["value"] = 12.;
        const auto snapshot = curve_snapshot(a);
        const auto middle = snapshot["owners"]["Curve"]["lanes"]["value"]["keyOrder"][1].get<std::string>();
        CurveView view;
        view.set_packages(&a, &b);
        check(view.visible_count() == 3 && view.select_key(middle) && view.selected_known(),
              "Exact stored key selection");
        view.set_time(.5);
        check(view.value()["value"] == 5. && view.value(true)["value"] == 6.,
              "Synchronized evaluated source time");
        view.set_changed_only(true);
        check(view.visible_count() == 1 && view.next_change(), "Confirmed changed-key navigation");
        view.set_range(-1., 3.);
        view.set_value_tolerance(.25);
        check(view.sampled_comparison()["status"] == "incomplete" &&
                  view.sampled_comparison()["unavailableSamples"].get<std::size_t>() > 0,
              "Custom unknown domain preserves evaluation gaps");
        view.set_range(0., 2.);
        view.set_value_tolerance(2.);
        check(view.sampled_comparison()["status"] == "identical",
              "Value tolerance affects evaluated grid independently of stored key changes");
        view.set_value_tolerance(0.);
        auto state = view.view_state();
        state["inspection"]["properties"] = {
            {"changedOnly", false}, {"filter", "value"}, {"selected", "/value"}};
        view.restore_view_state(state);
        CurveView restored;
        restored.set_packages(&a, &b);
        restored.restore_view_state(state);
        check(restored.view_state() == state, "Owner/lane/key/time and typed properties round trip");
        auto reversed = state;
        reversed["rangeBegin"] = 3.;
        reversed["rangeEnd"] = 1.;
        bool range_rejected = false;
        try {
            restored.restore_view_state(reversed);
        } catch (const Error &e) {
            range_rejected = e.code == "view-state";
        }
        check(range_rejected && restored.view_state() == state, "Custom range validation atomic");
        auto bad = state;
        bad["time"] = std::numeric_limits<double>::infinity();
        bool no = false;
        try {
            restored.restore_view_state(bad);
        } catch (const Error &e) {
            no = e.code == "view-state";
        }
        check(no && restored.view_state() == state, "Invalid state rejected atomically");
        auto weighted = a;
        weighted.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][0]["interpMode"] = 2;
        weighted.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][0]["tangentWeightMode"] = 2;
        CurveView weight;
        weight.set_packages(&weighted);
        weight.set_time(.5);
        check(weight.value()["ready"].get<bool>(), "Stored weighted segments are evaluated");
        auto unknown = a;
        unknown.exports[0].properties["FloatCurve"]["value"].erase("Keys");
        CurveView unavailable;
        unavailable.set_packages(&a, &unknown);
        unavailable.select_key(middle);
        check(!unavailable.selected_known(), "Unknown opposite keys never claim removal");
        auto empty = a;
        empty.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"] = Json::array();
        CurveView empty_view;
        empty_view.set_packages(&empty);
        check(empty_view.visible_count() == 0 && !empty_view.value()["ready"].get<bool>(),
              "Explicit empty keys");
        CurveView missing;
        missing.set_packages(&a, &b);
        check(!missing.select_objects("missing", "Curve") &&
                  missing.view_state()["beforeObject"] == "missing",
              "Missing owner choice never falls back");
        CurveView single;
        single.set_packages(&a);
        single.set_time(.5);
        AssetView asset;
        asset.set_packages(std::make_shared<Package>(a), std::make_shared<Package>(b));
        asset.select_tab(AssetTab::curves);
        auto integrated = asset.view_state();
        integrated["curves"] = state;
        asset.restore_view_state(integrated);
        check(asset.view_state()["curves"] == state, "Integrated asset/workspace curve state");
        auto transform_before = transform_curve_model(), transform_after = transform_before;
        transform_after.exports[0]
            .properties["CurveData"]["value"]["TransformCurves"]["value"][0]["TranslationCurve"]["value"]
                       ["FloatCurves"]["value"]["Keys"]["value"][1]["value"] = 4.;
        CurveView transforms;
        transforms.set_packages(&transform_before, &transform_after);
        check(transforms.visible_count() == 0, "Float scope does not expose transform keys");
        transforms.set_animation_family(AnimationCurveFamily::Transform);
        check(transforms.select_lane("name:Root~/control/translation.x"),
              "Exact transform component selection");
        transforms.set_time(1.);
        check(transforms.value()["value"] == 1. && transforms.value(true)["value"] == 2.,
              "Prepared transform values before/after");
        auto transform_state = transforms.view_state();
        CurveView transform_restored;
        transform_restored.set_packages(&transform_before, &transform_after);
        transform_restored.restore_view_state(transform_state);
        check(transform_restored.view_state() == transform_state, "Family/lane/range state round trip");
        auto invalid_family = transform_state;
        invalid_family["animationFamily"] = "unknown";
        bool rejected = false;
        try {
            transform_restored.restore_view_state(invalid_family);
        } catch (const Error &e) {
            rejected = e.code == "view-state";
        }
        check(rejected && transform_restored.view_state() == transform_state, "Invalid family is atomic");
        transforms.set_animation_family(AnimationCurveFamily::Float);
        check(transforms.visible_count() == 0, "Switching families does not invent opposite data");
        transforms.set_animation_family(AnimationCurveFamily::Transform);
        transforms.select_lane("name:Root~/control/translation.x");
        auto transform_partial = transform_before;
        transform_partial.exports[0].properties["CurveData"]["value"]["TransformCurves"]["value"][0].erase(
            "ScaleCurve");
        CurveView transform_missing;
        transform_missing.set_packages(&transform_before, &transform_partial);
        transform_missing.set_animation_family(AnimationCurveFamily::Transform);
        transform_missing.select_lane("name:Root~/control/scale.y");
        check(!transform_missing.selected_known(), "Unknown opposite transform channels stay unknown");
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
                    for (int scene = 0; scene < 10; ++scene)
                        for (int frame = 0; frame < 2; ++frame) {
                            io.DisplaySize = {width, 1000};
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(io.DisplaySize);
                            ImGui::Begin("Stored curve inspection", nullptr, ImGuiWindowFlags_NoDecoration);
                            if (scene == 0)
                                restored.draw();
                            if (scene == 1)
                                weight.draw();
                            if (scene == 2)
                                unavailable.draw();
                            if (scene == 3)
                                empty_view.draw();
                            if (scene == 4)
                                missing.draw();
                            if (scene == 5)
                                single.draw();
                            if (scene == 6)
                                asset.draw("Asset", false);
                            if (scene == 7)
                                transforms.draw();
                            if (scene == 8)
                                transform_restored.draw();
                            if (scene == 9)
                                transform_missing.draw();
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0,
                                  "Curve scenes render across themes/locales/widths");
                        }
            }
        check(original == a.exports[0].properties, "UI leaves original data unchanged");
        ImGui::DestroyContext();
        std::cout << checks << " curve view checks passed\n" << frames << " curve UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
