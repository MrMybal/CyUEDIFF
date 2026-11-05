#include <cyuediff/animation_pose_view.hpp>
#include <cyuediff/asset_view.hpp>
#include "animation_pose_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0, frames = 0;
    try {
        auto check = [&](bool ok, const char *why) {
            ++checks;
            if (!ok)
                throw std::runtime_error(why);
        };
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        auto a = hierarchy_animation_model(), s = hierarchy_skeleton_model();
        auto before = animation_pose(a, s, 0), after = animation_pose(a, s, 1);
        const auto original = before.to_json();
        AnimationPoseView view;
        view.set_poses(&before, &after, true);
        check(view.joint_count() == 3 && view.comparison()["changedJoints"] == 3,
              "Prepared source geometry compares by exact names");
        check(view.select_joint("tip") && !view.select_joint("missing"), "Named joint selection");
        view.set_position_tolerance(5);
        check(view.comparison()["status"] == "identical", "Shared Core tolerance affects review");
        auto state = view.view_state();
        state["camera"] = {{"target", {3., 4., 0.}}, {"distance", 100.}, {"yaw", .3}, {"pitch", .4}};
        view.restore_view_state(state);
        state = view.view_state();
        view.set_position_tolerance(4);
        check(view.view_state()["camera"] == state["camera"], "Tolerance refresh preserves camera");
        state = view.view_state();
        AnimationPoseView restored;
        restored.set_poses(&before, &after, true);
        restored.restore_view_state(state);
        check(restored.view_state() == state, "Independent component state roundtrip");
        for (const auto &bad : std::vector<Json>{{{"positionTolerance", -1.}},
                                                 {{"positionTolerance", "invalid"}},
                                                 {{"camera", {{"distance", -1.}}}},
                                                 {{"selected", Json::array()}}}) {
            bool rejected = false;
            try {
                restored.restore_view_state(bad);
            } catch (const Error &e) {
                rejected = e.code == "view-state";
            }
            check(rejected && restored.view_state() == state, "State validation is atomic");
        }
        before.display_pose.joints[0].position = {900., 900., 900.};
        check(view.comparison()["joints"]["root"]["before"]["position"] == Json({0., 0., 0.}),
              "View owns its bounded pose copies");
        before = animation_pose(a, s, 0);
        AnimationPoseView single, missing, added, empty, tolerance;
        single.set_poses(&before);
        added.set_poses(nullptr, &after, true);
        tolerance.set_poses(&before, &after, true);
        tolerance.set_position_tolerance(5);
        auto bad = after;
        bad.display_pose.joints[0].ready = false;
        bad.display_pose.complete = false;
        missing.set_poses(&before, &bad, true);
        check(added.comparison()["status"] == "changed" && added.comparison()["changedJoints"] == 3,
              "Explicit absent asset produces additions");
        check(missing.comparison()["status"] == "incomplete",
              "Unavailable prepared source remains incomplete");
        AnimationPose many;
        many.hierarchy_complete = true;
        many.display_pose.complete = true;
        for (int i = 0; i < 4100; ++i) {
            ReferenceJoint j;
            j.name = "joint" + std::to_string(i);
            j.ready = true;
            many.display_pose.joints.push_back(j);
        }
        AnimationPoseView capped;
        capped.set_poses(&many);
        check(capped.joint_count() == 4096, "Owned preview is bounded");
        AssetView integrated;
        integrated.set_packages(std::make_shared<Package>(a), std::make_shared<Package>(a));
        integrated.set_animation_poses(&before, &after, true);
        integrated.select_tab(AssetTab::animations);
        integrated.restore_view_state({{"animationPose", state}, {"animationPosePreview", true}});
        check(integrated.view_state()["animationPose"] == state &&
                  integrated.view_state()["animationPosePreview"],
              "Asset view retains prepared pose and review mode");
        auto invalid_state = integrated.view_state();
        invalid_state["animationPose"]["positionTolerance"] = -1;
        const auto integrated_original = integrated.view_state();
        bool rejected = false;
        try {
            integrated.restore_view_state(invalid_state);
        } catch (const Error &e) {
            rejected = e.code == "view-state";
        }
        check(rejected && integrated.view_state() == integrated_original,
              "Integrated pose state validates before mutation");
        auto oversized = before;
        oversized.display_pose.joints[0].position = {1e308, 1e308, 1e308};
        AnimationPoseView extreme;
        extreme.set_poses(&oversized, &after, true);
        check(extreme.comparison()["status"] == "incomplete" &&
                  extreme.comparison()["joints"]["root"]["distance"].is_null(),
              "Out-of-display-range positions stay unavailable before metric arithmetic");
        AnimationPose unavailable_pose;
        unavailable_pose.frame = 1;
        AnimationPoseView failed_side, absent_joint;
        failed_side.set_poses(&before, &unavailable_pose, true);
        auto without_tip = after;
        without_tip.display_pose.joints.pop_back();
        absent_joint.set_poses(&before, &without_tip, true);
        absent_joint.select_joint("tip");
        check(failed_side.comparison()["joints"]["tip"]["kind"] == "unavailable",
              "Unread requested side retains unavailable membership");
        check(absent_joint.comparison()["joints"]["tip"]["kind"] == "removed",
              "Known absent hierarchy joint remains distinct from failed binding");
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
                    for (auto *scene : {&view, &restored, &single, &missing, &added, &empty, &tolerance,
                                        &failed_side, &absent_joint})
                        for (int frame = 0; frame < 2; ++frame) {
                            io.DisplaySize = {width, 1000};
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(io.DisplaySize);
                            ImGui::Begin("Animation hierarchy pose", nullptr, ImGuiWindowFlags_NoDecoration);
                            scene->draw();
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0,
                                  "Pose UI renders across themes/locales/widths");
                        }
            }
        cyui::setLocale("fr");
        check(cyui::tr("Joint absent from selected hierarchy")=="Articulation absente de la hiérarchie choisie","Known absence status has a French translation");
        check(before.to_json() == original, "Drawing leaves prepared input unchanged");
        ImGui::DestroyContext();
        std::cout << checks << " animation pose view checks passed\n"
                  << frames << " animation pose UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
