#include <cyuediff/asset_view.hpp>
#include "rich_curve_model.hpp"
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
        auto state = view.view_state();
        state["inspection"]["properties"] = {
            {"changedOnly", false}, {"filter", "value"}, {"selected", "/value"}};
        view.restore_view_state(state);
        CurveView restored;
        restored.set_packages(&a, &b);
        restored.restore_view_state(state);
        check(restored.view_state() == state, "Owner/lane/key/time and typed properties round trip");
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
                    for (int scene = 0; scene < 7; ++scene)
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
