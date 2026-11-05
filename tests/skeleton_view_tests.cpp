#include <cyuediff/asset_view.hpp>
#include "skeleton_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0, frames = 0;
    try {
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 850};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        auto before = skeleton_model(), after = before;
        after.exports[0].native_data["localPoses"][1]["translation"][0] = 12.;
        const auto original = before.to_json();
        SkeletonView view;
        view.set_packages(&before, &after);
        check(!view.empty() && view.next_changed_bone(), "Changed raw bone navigation");
        auto saved = view.view_state();
        check(saved["inspection"]["selected"] == "arm", "Stable selected bone name");
        saved["canvas"] = {{"pan", {-20., -30.}}, {"zoom", 1.1}};
        saved["inspection"]["properties"]["filter"] = "translation";
        saved["inspection"]["properties"]["selected"] = "/localPose/translation/0";
        view.restore_view_state(saved);
        SkeletonView restored;
        restored.set_packages(&before, &after);
        restored.restore_view_state(saved);
        check(restored.view_state() == saved, "Hierarchy camera and bone/property selection round trip");
        auto invalid = saved;
        invalid["canvas"]["zoom"] = 0;
        bool rejected = false;
        try {
            restored.restore_view_state(invalid);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && restored.view_state() == saved, "Malformed nested state rejects atomically");
        auto reorder = before;
        auto &native = reorder.exports[0].native_data;
        std::swap(native["bones"][1], native["bones"][2]);
        std::swap(native["localPoses"][1], native["localPoses"][2]);
        native["nameToIndex"] = {{"root", 0}, {"tip", 1}, {"arm", 2}};
        restored.set_packages(&reorder, &after);
        restored.restore_view_state(saved);
        check(restored.view_state() == saved, "Selection survives sibling source ordering");
        view.set_filter("tip");
        check(view.visible_count() == 1 && !view.select_bone("arm"),
              "Bone search uses exact visible identities");
        view.set_changed_only(true);
        check(!view.visible_count() && !view.next_changed_bone(), "Changed navigation respects search");
        view.restore_view_state(saved);
        SkeletonView empty, added, missing;
        empty.set_packages(nullptr);
        added.set_packages(nullptr, &after, true);
        auto partial = before;
        partial.exports[0].native_data = Json::object();
        partial.exports[0].native_coverage = "opaque";
        missing.set_packages(&partial);
        check(empty.empty() && !added.empty() && !missing.empty(),
              "Empty, added and opaque hierarchy states");
        restored.restore_view_state({{"object", "missing"}, {"inspection", {{"selected", "arm"}}}});
        check(restored.view_state()["object"].is_null() &&
                  !restored.view_state()["inspection"].contains("selected"),
              "Missing object cannot select an unrelated bone");
        AssetView asset;
        asset.set_packages(std::make_shared<Package>(before), std::make_shared<Package>(after));
        asset.restore_view_state({{"skeleton", saved}});
        asset.select_tab(AssetTab::skeleton);
        check(asset.active_tab() == AssetTab::skeleton && asset.view_state()["skeleton"] == saved,
              "Asset tab binds saved hierarchy state");
        SkeletonView pose_view, pose_missing;
        pose_view.set_packages(&before, &after);
        pose_view.set_pose_preview(true);
        check(pose_view.select_bone("arm"), "Select joint for reference preview");
        auto pose_saved = pose_view.view_state();
        pose_saved["poseCamera"] = {
            {"target", {3., 0., 0.}}, {"up", {0., 0., 1.}}, {"yaw", .7}, {"pitch", .35}, {"distance", 35.}};
        pose_saved["poseProperties"] = {
            {"changedOnly", true}, {"filter", "position"}, {"selected", "/reference/position/0"}};
        pose_view.restore_view_state(pose_saved);
        check(pose_view.view_state() == pose_saved && pose_view.pose_preview(),
              "Pose mode and shared camera round trip");
        auto bad_pose = pose_saved;
        bad_pose["poseCamera"]["distance"] = -1;
        rejected = false;
        try {
            pose_view.restore_view_state(bad_pose);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && pose_view.view_state() == pose_saved,
              "Invalid pose camera rejects before state mutation");
        auto nonunit = before;
        nonunit.exports[0].native_data["localPoses"][0]["rotation"] = {0., 0., 0., 0.};
        pose_missing.set_packages(&nonunit);
        pose_missing.set_pose_preview(true);
        check(pose_missing.select_bone("arm"), "Unavailable derived position retains stored bone selection");
        auto display_pose = reference_pose(before);
        ReferencePoseViewport bounded;
        auto many = display_pose;
        many.joints.clear();
        for (unsigned i = 0; i < 5000; ++i) {
            auto joint = display_pose.joints[0];
            joint.name = "joint" + std::to_string(i);
            many.joints.push_back(joint);
        }
        bounded.set_models(&many, nullptr, false);
        check(bounded.visible_joint_count() == 4096, "Reference viewport bounds prepared display work");
        ReferencePoseViewport moves;
        auto moved_pose = reference_pose(after);
        moves.set_models(&display_pose, &moved_pose, true);
        check(moves.joint_difference("arm")->at("distance") == 2.,
              "Pose display uses Core displacement classification");
        const auto changed_camera = Json{{"changedOnly", true}};
        moves.restore_view_state(changed_camera);
        check(moves.visible_joint_count() == 1 && moves.visible_joint_count(true) == 1,
              "Moved-joint filter uses derived motion");
        auto far = display_pose;
        far.joints[0].position[0] = 1e13;
        bounded.set_models(&far, nullptr, false);
        check(bounded.visible_joint_count() == 2,
              "Coordinates beyond display camera range stay unavailable in preview only");
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            cyui::Config config;
            config.themeKey = theme;
            cyui::apply(config);
            cyui::loadFonts();
            unsigned char *pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            for (const auto *locale : {"en", "fr"})
                for (int width : {1200, 640})
                    for (int scene = 0; scene < 7; ++scene)
                        for (int n = 0; n < 2; ++n) {
                            cyui::setLocale(locale);
                            io.DisplaySize = {float(width), 850};
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(io.DisplaySize);
                            ImGui::Begin("Skeleton inspection");
                            if (scene == 0)
                                view.draw();
                            if (scene == 1)
                                asset.draw("Asset", false);
                            if (scene == 2)
                                empty.draw();
                            if (scene == 3)
                                added.draw();
                            if (scene == 4)
                                missing.draw();
                            if (scene == 5)
                                pose_view.draw();
                            if (scene == 6)
                                pose_missing.draw();
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0,
                                  "Hierarchy draws in each theme/language/width");
                        }
        }
        check(pose_view.rendered_pose_joints() > 0 && pose_missing.rendered_pose_joints() == 0,
              "Finite prepared positions render; invalid rotations do not invent joints");
        ReferencePose one;
        one.complete = true;
        one.object_path = "Pick";
        ReferenceJoint origin_joint;
        origin_joint.name = "origin";
        origin_joint.ready = true;
        one.joints.push_back(origin_joint);
        ReferencePoseViewport pick;
        pick.set_models(&one, nullptr, false);
        ImVec2 center{};
        bool picked = false;
        for (int frame = 0; frame < 4; ++frame) {
            io.DisplaySize = {640, 600};
            if (frame > 0)
                io.AddMousePosEvent(center.x, center.y);
            if (frame == 2)
                io.AddMouseButtonEvent(0, true);
            if (frame == 3)
                io.AddMouseButtonEvent(0, false);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Pose picking", nullptr, ImGuiWindowFlags_NoDecoration);
            pick.draw();
            const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
            if (frame == 0)
                center = {(low.x + high.x) * .5f,
                          (low.y + ImGui::GetTextLineHeightWithSpacing() + high.y) * .5f};
            picked = picked || pick.take_picked_joint() == "origin";
            ImGui::End();
            ImGui::Render();
            ++frames;
        }
        check(picked, "Clicking a projected reference point picks its exact stored joint name");
        check(before.to_json() == original, "Hierarchy UI is read-only");
        ImGui::DestroyContext();
        std::cout << checks << " skeleton view checks passed\n"
                  << frames << " skeleton UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
