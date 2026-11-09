#include "physical_cloth_fixture.hpp"
#include <cyuediff/physical_cloth_view.hpp>
#include <cyuediff/asset_view.hpp>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    unsigned checks = 0, frames = 0;
    const auto check = [&](bool ok, const char *why) {
        ++checks;
        if (!ok)
            throw std::runtime_error(why);
    };
    try {
        PhysicalClothOptions options;
        options.profile = PhysicalClothProfile::unreal_5_3_windows;
        auto p = physical_fixture(), q = p;
        physical_fields(q)[std::string(PhysicalData) + ".InverseMasses"]["value"][0] = .5f;
        physical_fields(q)[std::string(PhysicalData) + ".EuclideanTethers"]["value"]["native"]["batches"][0]
                          [0]["length"] = 2.f;
        auto a = std::make_shared<PhysicalClothAsset>(read_physical_cloth(p, options)),
             b = std::make_shared<PhysicalClothAsset>(read_physical_cloth(q, options));
        const auto original = a->to_json(true);
        PhysicalClothView view;
        view.set_assets(a, b, true);
        check(view.report()["knownComparison"] && view.report()["modified"] == 2,
              "Named mass/tether changes affect their own physical vertices");
        check(view.select_vertex(1) && view.details().rows.size() < 70,
              "Focused tether/vertex details stay bounded");
        view.set_changed_only(true);
        check(view.report()["visible"] == 2 && view.next_change() && view.next_change(true),
              "Changed vertex filtering and navigation");
        view.set_filter("2");
        check(view.report()["visible"] == 0 && view.report()["selected"].is_null(),
              "Hidden selection clears");
        view.set_filter("");
        view.select_vertex(1);
        const auto state = view.view_state();
        PhysicalClothView restored;
        restored.set_assets(a, b, true);
        restored.restore_view_state(state);
        check(restored.view_state() == state, "Selected LOD, vertex, filter, details and camera round trip");
        check(!view.select_lod(false, 99) && !view.select_mode(3) && !view.select_vertex(99),
              "Invalid selections do not retarget");
        view.set_changed_only(false);
        check(view.select_lod(false, 1) && view.report()["afterLOD"] == 0,
              "Independent selected physical LODs");
        view.select_lod(true, 1);
        view.select_mode(1);
        view.select_vertex(0);
        check(view.report()["beforeWrappedPosition"] == Json::array({0, 0, .75f}),
              "Actual selected CPU reference-pose transition algebra");
        check(view.report()["geometryAvailable"] && view.report()["modified"] == 0,
              "Reference-pose wire and stored transition values remain separate");
        auto padded = p;
        auto m = physical_mapping();
        m.padding = 123;
        physical_lod(padded, 1)["native"]["transitionUp"][0] = cloth_record(m);
        PhysicalClothView padding;
        padding.set_assets(a, std::make_shared<PhysicalClothAsset>(read_physical_cloth(padded, options)),
                           true);
        padding.select_lod(false, 1);
        padding.select_lod(true, 1);
        padding.select_mode(1);
        check(padding.report()["modified"] == 0, "Native padding never produces semantic row changes");
        view.select_lod(false, 0);
        view.select_lod(true, 0);
        view.select_mode(1);
        check(!view.report()["knownComparison"].get<bool>() &&
                  !view.report()["geometryAvailable"].get<bool>(),
              "Missing boundary transition stays unavailable");
        auto unrelated = q;
        unrelated.exports[0].path = "/Game/Other.Asset.Cloth";
        auto peer = std::make_shared<PhysicalClothAsset>(read_physical_cloth(unrelated, options));
        view.set_assets(a, peer, true);
        check(!view.report()["knownComparison"].get<bool>() && view.report()["modified"] == 0,
              "Unrelated objects require explicit pairing");
        view.set_assets(a, peer, true, true);
        check(view.report()["knownComparison"] && view.report()["modified"] == 2,
              "Explicit object pair admits stored-value comparison");
        PhysicalClothView unknown;
        unknown.set_assets(a, {}, true);
        unknown.set_changed_only(true);
        check(unknown.report()["removed"] == 0 && unknown.report()["visible"] == 3 && !unknown.next_change(),
              "Unavailable peer is inspection and cannot prove removals");
        auto tiny_report = options;
        tiny_report.max_report_values = 1;
        PhysicalClothView bounded;
        bounded.set_assets(std::make_shared<PhysicalClothAsset>(read_physical_cloth(p, tiny_report)), b,
                           true);
        check(!bounded.report()["knownComparison"].get<bool>() && !bounded.report()["diagnostics"].empty() &&
                  bounded.report()["removed"] == 0,
              "A bounded report failure stays diagnostic instead of crashing or proving removal");
        auto stale = state;
        stale["beforeLOD"] = 99;
        restored.restore_view_state(stale);
        check(restored.report()["beforeLOD"] == 99 && !restored.report()["knownComparison"].get<bool>(),
              "Stale LOD stays unavailable instead of selecting another scope");
        auto delta = p;
        auto &map = physical_fields(delta)[std::string(PhysicalData) + ".WeightMaps"]["value"];
        map["replace"] = false;
        map["entries"]["7"] = {{"key", 7},
                               {"value",
                                {{"/Script/ClothingSystemRuntimeCommon.PointWeightMap.Values",
                                  cloth_field("ArrayProperty", {.1f, .2f, .3f})}}}};
        view.set_assets(a, std::make_shared<PhysicalClothAsset>(read_physical_cloth(delta, options)), true);
        check(view.report()["modified"] == 0 && !view.report()["completeWeightMapInventory"].get<bool>(),
              "Unresolved default map absence is not a named removal/addition");
        PhysicalClothView only_after;
        only_after.set_assets({}, b, true);
        check(only_after.select_vertex(0) && !only_after.details().rows.empty(),
              "After-only inspection selects its available side");
        auto large = p;
        constexpr unsigned count = 40000;
        auto &f = physical_fields(large);
        Json positions = Json::array(), normals = Json::array(), mass = Json::array();
        for (unsigned i = 0; i < count; ++i) {
            positions.push_back({float(i), 0.f, 0.f});
            normals.push_back({0.f, 0.f, 1.f});
            mass.push_back(1.f);
        }
        f[std::string(PhysicalData) + ".Vertices"]["value"] = std::move(positions);
        f[std::string(PhysicalData) + ".Normals"]["value"] = std::move(normals);
        f[std::string(PhysicalData) + ".InverseMasses"]["value"] = std::move(mass);
        f[std::string(PhysicalData) + ".BoneData"]["value"] = Json::array();
        f[std::string(PhysicalData) + ".WeightMaps"]["value"]["entries"] = Json::object();
        physical_lod(large, 0)["native"]["transitionDown"] = Json::array();
        physical_lod(large, 1)["native"]["transitionUp"] = Json::array();
        PhysicalClothView focused;
        focused.set_assets(std::make_shared<PhysicalClothAsset>(read_physical_cloth(large, options)));
        check(focused.select_vertex(count - 1) && focused.details().rows.size() < 30,
              "Large physical list focuses its last original stored vertex");
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        PhysicalClothView::register_translations();
        const auto frame = [&](PhysicalClothView &v, float width) {
            io.DisplaySize = {width, 900};
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Physical cloth test", nullptr, ImGuiWindowFlags_NoSavedSettings);
            v.draw();
            ImGui::End();
            ImGui::Render();
            ++frames;
            check(ImGui::GetDrawData()->TotalVtxCount > 0, "Physical view emits a complete frame");
        };
        for (const char *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const char *language : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                cyui::setLocale(language);
                unsigned char *pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                check(std::string(language) != "fr" || cyui::tr("Physical cloth") != "Physical cloth",
                      "French physical labels translated");
                for (int panel = 0; panel < 3; ++panel)
                    for (float width : {1280.f, 560.f}) {
                        restored.set_assets(a, b, true);
                        auto settings = state;
                        settings["panel"] = panel;
                        restored.restore_view_state(settings);
                        frame(restored, width);
                        if (panel == 0 && width == 560.f)
                            check(restored.report()["compact"] &&
                                      restored.report()["detailsHeight"].get<float>() > 200,
                                  "Compact controls preserve space for selected values");
                        if (panel == 1)
                            check(restored.report()["geometryAvailable"],
                                  "Stored physical wire remains available in each theme/language");
                    }
            }
        for (unsigned i = 0; i < 5; ++i)
            frame(focused, 1280);
        check(focused.report()["selectedVisible"] && focused.report()["rendered"] < 100,
              "Clipped physical list scrolls to the final selected record");
        view_state::validate(state, "physical-cloth");
        auto malformed = state;
        malformed["mode"] = "invalid";
        malformed["beforeLOD"] = -1;
        malformed["afterLOD"] = 1e100;
        malformed["selected"] = false;
        restored.restore_view_state(malformed);
        check(restored.report()["beforeLOD"] == 0 && restored.report()["selected"].is_null(),
              "Malformed numeric state never wraps or silently retargets");
        check(a->to_json(true) == original, "UI preparation never mutates stored physical data");
        ImGui::DestroyContext();
        std::cout << checks << " physical cloth UI checks, " << frames << " frames\n";
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
