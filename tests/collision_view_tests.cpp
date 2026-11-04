#include <cyuediff/asset_view.hpp>
#include "collision_fixture.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0, frames = 0;
    try {
        auto check = [&](bool good, const char *message) {
            ++checks;
            if (!good)
                throw std::runtime_error(message);
        };
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 850};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        auto before = collision_extended_fixture(), after = before;
        after.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Radius"]["value"] = 7.;
        CollisionView view;
        view.set_packages(&before, &after);
        check(!view.empty() && view.next_changed_shape(), "Navigate changed stored shape");
        auto saved = view.view_state();
        check(saved["shape"] == "sphere:name:Ball", "Changed shape uses its name");
        saved["properties"]["filter"] = "radius";
        saved["properties"]["selected"] = "/geometry/radius";
        view.restore_view_state(saved);
        CollisionView restored;
        restored.set_packages(&before, &after);
        restored.restore_view_state(saved);
        check(restored.view_state() == saved, "Collision selection and property filter round trip");
        auto bad = saved;
        bad["properties"]["changedOnly"] = 1;
        bool rejected = false;
        try {
            restored.restore_view_state(bad);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && restored.view_state() == saved,
              "Malformed nested collision state is rejected atomically");
        check(!view.select_shape("missing") && !view.select_body("missing"),
              "Unknown identities are refused");
        auto shifted = before;
        shifted.exports.insert(shifted.exports.begin(), ObjectExport{});
        CollisionView reordered;
        reordered.set_packages(&shifted, &after);
        reordered.restore_view_state(saved);
        check(reordered.view_state() == saved, "Selection survives export-index movement");
        auto package_a = std::make_shared<Package>(before), package_b = std::make_shared<Package>(after);
        AssetView asset;
        asset.set_packages(package_a, package_b);
        asset.restore_view_state({{"collisions", saved}});
        asset.select_tab(AssetTab::collisions);
        check(asset.active_tab() == AssetTab::collisions && asset.view_state()["collisions"] == saved,
              "Asset host retains the collision tab state");
        CollisionView missing, added, incomplete;
        missing.set_packages(nullptr);
        added.set_packages(nullptr, &after, true);
        auto partial = before;
        partial.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0].erase("Radius");
        incomplete.set_packages(&partial);
        check(missing.empty() && !added.empty() && !incomplete.empty(),
              "Absent, added and incomplete states prepared");
        view.restore_view_state({{"body", "missing"}, {"shape", "sphere:name:Ball"}});
        check(view.view_state()["body"].is_null() && view.view_state()["shape"].is_null(),
              "Missing body does not select a shape from another body");
        view.restore_view_state(saved);
        const auto original = before.to_json();
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            cyui::Config config;
            config.themeKey = theme;
            cyui::apply(config);
            cyui::loadFonts();
            unsigned char *pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            for (const auto *locale : {"en", "fr"})
                for (int scene = 0; scene < 5; ++scene)
                    for (int n = 0; n < 2; ++n) {
                        cyui::setLocale(locale);
                        ImGui::NewFrame();
                        ImGui::SetNextWindowPos({0, 0});
                        ImGui::SetNextWindowSize(io.DisplaySize);
                        ImGui::Begin("Collision inspection");
                        if (scene == 0)
                            view.draw();
                        if (scene == 1)
                            asset.draw("Asset", false);
                        if (scene == 2)
                            missing.draw();
                        if (scene == 3)
                            added.draw();
                        if (scene == 4)
                            incomplete.draw();
                        ImGui::End();
                        ImGui::Render();
                        check(ImGui::GetDrawData()->TotalVtxCount > 0,
                              "Collision view draws in each theme and language");
                        ++frames;
                    }
        }
        check(before.to_json() == original, "Collision views never mutate package data");
        ImGui::DestroyContext();
        std::cout << checks << " collision view checks passed\n"
                  << frames << " collision UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
