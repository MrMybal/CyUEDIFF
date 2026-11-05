#include <cyuediff/animation_pose_view.hpp>
#include <cyuediff/asset_view.hpp>
#include "animation_pose_model.hpp"
#include "attachments_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0, frames = 0;
    try {
        auto check = [&](bool v, const char *why) {
            ++checks;
            if (!v)
                throw std::runtime_error(why);
        };
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        auto a = hierarchy_animation_model(), s = attachments_model();
        auto pa = animation_pose(a, s, 0), pb = animation_pose(a, s, 1);
        auto before = animation_socket_pose(pa, s), after = animation_socket_pose(pb, s);
        const auto original = before.to_json();
        AnimationPoseView view;
        view.set_socket_poses(&before, &after, true);
        check(view.socket_count() == 2 && view.comparison()["changedSockets"] == 2 &&
                  !view.comparison().contains("joints"),
              "Socket view retains dedicated Core comparison contract");
        check(view.select_socket("Rig.Grip") && !view.select_socket("missing"),
              "Exact socket object selection");
        view.set_position_tolerance(5);
        check(view.comparison()["status"] == "identical",
              "Source socket Core tolerance controls colors and metrics");
        auto state = view.view_state();
        state["camera"] = {{"target", {1., 2., 3.}}, {"distance", 80.}, {"yaw", .3}, {"pitch", .4}};
        view.restore_view_state(state);
        state = view.view_state();
        view.set_position_tolerance(4);
        check(view.view_state()["camera"] == state["camera"], "Socket tolerance update preserves camera");
        state = view.view_state();
        AnimationPoseView restored;
        restored.set_socket_poses(&before, &after, true);
        restored.restore_view_state(state);
        check(restored.view_state() == state, "Owned socket view state roundtrip");
        before.display_pose.joints[0].position = {900., 900., 900.};
        check(view.comparison()["sockets"]["Rig.Grip"]["before"]["position"] == Json({11., 2., 3.}),
              "Socket view owns prepared values");
        before = animation_socket_pose(pa, s);
        auto partial = after;
        partial.display_pose.joints[0].ready = false;
        partial.display_pose.complete = false;
        partial.display_pose.joints.pop_back();
        AnimationPoseView missing, single, added, empty, capped;
        missing.set_socket_poses(&before, &partial, true);
        single.set_socket_poses(&before);
        added.set_socket_poses(nullptr, &after, true);
        empty.set_socket_poses(nullptr);
        check(missing.comparison()["sockets"]["Rig.Muzzle"]["kind"] == "removed" &&
                  missing.display_difference("Rig.Muzzle")->at("kind") == "removed",
              "Viewport and details agree on known membership despite unrelated unavailable point");
        check(missing.comparison()["sockets"]["Rig.Grip"]["kind"] == "unavailable",
              "Unavailable origin is not a zero point");
        check(added.comparison()["changedSockets"] == 2, "Absent asset certifies added socket identities");
        AnimationSocketPose many;
        many.membership_complete = true;
        many.display_pose.complete = true;
        for (int i = 0; i < 4100; ++i) {
            ReferenceJoint j;
            j.name = "Socket" + std::to_string(i);
            j.ready = true;
            many.display_pose.joints.push_back(j);
        }
        capped.set_socket_poses(&many, &many, true);
        check(capped.socket_count() == 4096 && !capped.comparison()["complete"].get<bool>(),
              "Socket presentation owns a bounded union without certifying truncation");
        AssetView integrated;
        integrated.set_packages(std::make_shared<Package>(a), std::make_shared<Package>(a));
        integrated.set_animation_poses(&pa, &pb, true);
        integrated.set_animation_socket_poses(&before, &after, true);
        integrated.select_tab(AssetTab::animations);
        integrated.restore_view_state(
            {{"animationSockets", state}, {"animationSocketsPreview", true}, {"animationPosePreview", true}});
        check(integrated.view_state()["animationSockets"] == state &&
                  integrated.view_state()["animationSocketsPreview"],
              "Integrated source socket review retained separately from bone review");
        const auto saved = integrated.view_state();
        auto bad = saved;
        bad["animationSockets"]["positionTolerance"] = -1;
        bool rejected = false;
        try {
            integrated.restore_view_state(bad);
        } catch (const Error &e) {
            rejected = e.code == "view-state";
        }
        check(rejected && integrated.view_state() == saved, "Socket state validation is atomic");
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
                for (float width : {1200.f, 640.f})
                    for (auto *scene : {&view, &restored, &missing, &single, &added, &empty})
                        for (int frame = 0; frame < 2; ++frame) {
                            io.DisplaySize = {width, 1000};
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(io.DisplaySize);
                            ImGui::Begin("Sockets", nullptr, ImGuiWindowFlags_NoDecoration);
                            scene->draw();
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0,
                                  "Socket view draws in five themes, two languages and narrow/wide layouts");
                        }
                io.DisplaySize = {1000, 1000};
                ImGui::NewFrame();
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Integrated");
                integrated.draw("asset", false);
                ImGui::End();
                ImGui::Render();
                ++frames;
                check(ImGui::GetDrawData()->TotalVtxCount > 0, "Integrated animation socket mode draws");
            }
        check(cyui::tr("Show source socket origins") == "Afficher les origines des sockets animés",
              "Socket mode French translation");
        check(before.to_json() == original, "Drawing never mutates caller socket poses");
        ImGui::DestroyContext();
        std::cout << checks << " source socket view checks passed\n"
                  << frames << " source socket UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
