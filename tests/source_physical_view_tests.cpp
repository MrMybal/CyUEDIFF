#include "source_physical_fixture.hpp"
#include <cyuediff/asset_view.hpp>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    unsigned checks = 0, frames = 0;
    auto check = [&](bool ok, const char *why) {
        ++checks;
        if (!ok)
            throw std::runtime_error(why);
    };
    try {
        PhysicalClothOptions o;
        o.profile = PhysicalClothProfile::unreal_5_3_windows;
        auto p = std::make_shared<Package>(source_physical_fixture()), q = std::make_shared<Package>(*p);
        q->summary.package_name = "/Game/After";
        for (auto &e : q->exports)
            e.path = "/Game/After" + e.path;
        source_physical_fields(*q)["InverseMasses"]["value"][0] = .5f;
        auto a = std::make_shared<PhysicalClothAsset>(read_physical_cloth(*p, o)),
             b = std::make_shared<PhysicalClothAsset>(read_physical_cloth(*q, o));
        AssetView v;
        v.set_packages(p, q);
        v.set_physical_cloth_assets({a}, {b}, true);
        check(!v.physical_cloth().report()["knownComparison"].get<bool>(),
              "Different source objects do not imply a correspondence");
        v.set_physical_cloth_pair(true);
        check(v.physical_cloth().report()["knownComparison"].get<bool>(),
              "Explicit selected source pairing enables known shared vertex values");
        check(v.select_physical_cloth(false, a->object_path()) && !v.select_physical_cloth(true, "missing"),
              "Independent source object selections reject unknown identities");
        auto state = v.view_state();
        state["physicalCloth"]["beforeLOD"] = 1;
        state["physicalCloth"]["afterLOD"] = 1;
        state["physicalCloth"]["mode"] = 1;
        state["physicalCloth"]["selected"] = 0;
        v.restore_view_state(state);
        auto saved = v.view_state();
        AssetView restarted;
        restarted.set_packages(p, q);
        restarted.set_physical_cloth_assets({a}, {b}, true);
        restarted.restore_view_state(saved);
        check(restarted.view_state() == saved,
              "Source selection, explicit pairing and physical LOD/vertex/camera state round trip");
        auto missing = saved;
        missing["physicalClothBefore"] = "/Game/Missing.Object";
        restarted.restore_view_state(missing);
        check(!restarted.physical_cloth().report().at("explicitObjectPair").get<bool>() &&
                  !restarted.physical_cloth().report().at("knownComparison").get<bool>(),
              "Missing saved source objects cannot silently restore pairing on another model");
        restarted.restore_view_state(saved);
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        cyui::Config config;
        cyui::apply(config);
        cyui::loadFonts();
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        for (const auto *language : {"en", "fr"}) {
            cyui::setLocale(language);
            check(std::string(language) == "en" ||
                      cyui::tr("Compare selected physical objects") != "Compare selected physical objects",
                  "Source physical translations exist");
            for (int w : {1280, 440})
                for (int i = 0; i < 3; ++i) {
                    io.DisplaySize = {float(w), 900};
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0});
                    ImGui::SetNextWindowSize(io.DisplaySize);
                    ImGui::Begin("Source cloth", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    restarted.select_tab(AssetTab::physical_cloth);
                    restarted.draw("Source", false);
                    ImGui::End();
                    ImGui::Render();
                    ++frames;
                }
        }
        check(frames == 12 && restarted.active_tab() == AssetTab::physical_cloth,
              "Reusable source physical tab draws both languages at desktop and narrow widths");
        restarted.set_packages({});
        check(!restarted.physical_cloth().report()["geometryAvailable"].get<bool>(),
              "New package binding releases previous physical models");
        ImGui::DestroyContext();
        std::cout << checks << " source physical view checks; " << frames << " frames passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
