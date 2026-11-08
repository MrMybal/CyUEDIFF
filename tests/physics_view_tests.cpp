#include <cyuediff/collision_view.hpp>
#include <cyuediff/cooked_physics.hpp>
#include <cyuediff/asset_view.hpp>
#include "cooked_physics_fixture.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    unsigned checks = 0, frames = 0;
    auto check = [&](bool b, const char *s) {
        ++checks;
        if (!b)
            throw std::runtime_error(s);
    };
    try {
        CookedPhysicsOptions options;
        options.profile = CookedPhysicsProfile::unreal_5_3_windows;
        Json a = {{"schema", "cyuediff.cooked-physics/1"},
                  {"complete", true},
                  {"bodyTableKnown", true},
                  {"provenance", Json::object()},
                  {"bodies",
                   Json::array(
                       {{{"object", "Body"},
                         {"class", "/Script/Engine.BodySetup"},
                         {"complete", true},
                         {"hasCookedCollisionData", true},
                         {"formats", Json::array({{{"name", "Chaos"},
                                                   {"number", 0},
                                                   {"status", "decoded"},
                                                   {"physics", read_chaos_physics(physics_fixture().blob.data,
                                                                                  options)}}})}}})}};
        auto original = a;
        auto b = a;
        b["bodies"][0]["formats"][0]["physics"]["objects"][0]["vertices"][1][0] = .5;
        auto before = cooked_physics_review(a, options), after = cooked_physics_review(b, options);
        CollisionViewport viewport;
        viewport.set_models(before.wire, after.wire, &before.model, &after.model, true);
        viewport.select("Body");
        check(viewport.changes().modified == 2 && viewport.visible_edge_count() == 15,
              "Native shared geometry produces two modified stored references");
        auto missing = b;
        missing["complete"] = false;
        missing["bodies"][0]["complete"] = false;
        missing["bodies"][0]["formats"][0]["status"] = "missing-chunk";
        missing["bodies"][0]["formats"][0].erase("physics");
        auto unavailable = cooked_physics_review(missing, options);
        viewport.set_models(before.wire, unavailable.wire, &before.model, &unavailable.model, true);
        viewport.select("Body");
        check(!viewport.changes().changed(), "Unavailable native format is not painted as removed geometry");
        auto absent = a;
        absent["bodies"][0]["formats"] = Json::array();
        auto removed = cooked_physics_review(absent, options);
        viewport.set_models(before.wire, removed.wire, &before.model, &removed.model, true);
        viewport.select("Body");
        check(viewport.changes().removed == 3, "Known native format removal colors only nonempty geometry");
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 850};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        CollisionView missing_view;
        missing_view.set_models(before.model, unavailable.model, before.wire, unavailable.wire, true);
        check(!missing_view.next_changed_shape(),
              "Unavailable native formats do not become changed-shape navigation targets");
        const auto identity = "chaos/Chaos#0/convex/index:0";
        for (const auto *locale : {"en", "fr"})
            for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
                cyui::setLocale(locale);
                check(std::string(locale) != "fr" ||
                          cyui::tr("Decoded native Chaos geometry in cooker coordinates; no simulation.") !=
                              "Decoded native Chaos geometry in cooker coordinates; no simulation.",
                      "French native geometry scope label is translated");
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                unsigned char *pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                CollisionView view;
                view.set_models(before.model, after.model, before.wire, after.wire, true);
                check(view.select_shape(identity), "Native shape selection");
                for (unsigned i = 0; i < 4; ++i) {
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0});
                    ImGui::SetNextWindowSize(io.DisplaySize);
                    ImGui::Begin("Native physics review", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    view.draw();
                    ImGui::End();
                    ImGui::Render();
                    ++frames;
                }
                auto report = view.report();
                check(report["complete"] && report["beforeEdges"] == 6 && report["afterEdges"] == 6 &&
                          report["changes"]["modified"] == 1,
                      "Native wire review and selected changes");
                auto state = view.view_state();
                state["viewport"]["yaw"] = .31;
                state["viewport"]["distance"] = 4.;
                view.restore_view_state(state);
                check(view.view_state()["viewport"]["yaw"] == .31 && view.report()["shape"] == identity,
                      "Native selection and camera round trip");
            }
        check(a == original, "Native review leaves input snapshot immutable");
        ImGui::DestroyContext();
        std::cout << checks << " native physics UI checks, " << frames << " frames passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Physics view test after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
