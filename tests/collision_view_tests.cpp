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
        CollisionOptions snapshot_options;
        snapshot_options.source_indices = false;
        auto a = collision_snapshot(before, snapshot_options),
             b = collision_snapshot(after, snapshot_options);
        CollisionViewport viewport;
        viewport.set_models(collision_wireframe(before), collision_wireframe(after), &a, &b, true);
        viewport.select("SM.Body");
        check(viewport.changes().modified == 1 && viewport.changes().unchanged == 5,
              "Wireframe colors follow stable stored shape identity");
        viewport.set_changed_only(true);
        check(viewport.visible_edge_count() == 96 && viewport.visible_edge_count(true) == 96,
              "Changed-only includes just the changed sphere on both sides");
        check(viewport.focus_changes(), "Fit changed shape bounds");
        viewport.select("");
        check(viewport.visible_edge_count() == 0, "Distinct body coordinate systems are not overlaid");
        viewport.select("SM.Body", "sphere:name:Ball");
        check(viewport.visible_edge_count() == 96, "Typed selection filters the prepared geometry");
        CollisionViewport added_viewport;
        added_viewport.set_models({}, collision_wireframe(after), nullptr, &b, true);
        added_viewport.select("SM.Body");
        check(added_viewport.changes().added == 6 && added_viewport.visible_edge_count() == 0 &&
                  added_viewport.visible_edge_count(true) > 0,
              "Added package has an explicit empty before pane");
        auto unknown_after = after;
        unknown_after.exports[0].properties.erase("AggGeom");
        auto unknown_snapshot = collision_snapshot(unknown_after, snapshot_options);
        CollisionViewport unknown_view;
        unknown_view.set_models(collision_wireframe(before), collision_wireframe(unknown_after), &a,
                                &unknown_snapshot, true);
        unknown_view.select("SM.Body");
        check(!unknown_view.changes().changed(), "Unavailable collection is not painted as deleted geometry");
        auto unknown_transform = before;
        unknown_transform.exports[0].properties["AggGeom"]["value"]["ConvexElems"]["value"][0].erase(
            "Transform");
        auto unknown_geometry = collision_snapshot(unknown_transform, snapshot_options);
        unknown_view.set_models(collision_wireframe(before), collision_wireframe(unknown_transform), &a,
                                &unknown_geometry, true);
        unknown_view.select("SM.Body");
        check(!unknown_view.changes().changed(), "Unavailable peer geometry is not painted as modified");
        auto removed_body = before;
        removed_body.exports.erase(removed_body.exports.begin());
        auto removed_snapshot = collision_snapshot(removed_body, snapshot_options);
        CollisionViewport removed_view;
        removed_view.set_models(collision_wireframe(before), collision_wireframe(removed_body), &a,
                                &removed_snapshot, true);
        removed_view.select("SM.Body");
        check(removed_view.changes().removed > 0,
              "Known body-table removal remains visible despite unresolved owner references");
        auto reordered_package = before;
        auto &spheres = reordered_package.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"];
        std::swap(spheres[0], spheres[1]);
        auto reorder_snapshot = collision_snapshot(reordered_package, snapshot_options);
        CollisionViewport reorder_viewport;
        reorder_viewport.set_models(collision_wireframe(before), collision_wireframe(reordered_package), &a,
                                    &reorder_snapshot, true);
        reorder_viewport.select("SM.Body");
        check(!reorder_viewport.changes().changed(), "Named reorder does not recolor geometry");
        CollisionView view;
        view.set_packages(&before, &after);
        check(!view.empty() && view.next_changed_shape(), "Navigate changed stored shape");
        auto saved = view.view_state();
        check(saved["shape"] == "sphere:name:Ball", "Changed shape uses its name");
        saved["viewport"] = {{"target", {1., 2., 3.}}, {"up", {0., 0., 1.}}, {"yaw", .3}, {"pitch", .2},
                             {"distance", 25.},        {"changedOnly", true}};
        saved["properties"]["filter"] = "radius";
        saved["properties"]["selected"] = "/geometry/radius";
        view.restore_view_state(saved);
        CollisionView restored;
        restored.set_packages(&before, &after);
        restored.restore_view_state(saved);
        check(restored.view_state() == saved, "Collision selection and property filter round trip");
        auto bad = saved;
        bad["viewport"]["distance"] = -1;
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
        CollisionView missing, added, incomplete, failed_preview;
        auto huge = collision_fixture();
        auto &huge_sphere = huge.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0];
        huge_sphere["Center"]["value"] = {1e308, 1e308, 1e308};
        huge_sphere["Radius"]["value"] = 1e308;
        failed_preview.set_packages(&huge);
        check(!failed_preview.empty() && failed_preview.select_shape("sphere:name:Ball"),
              "Preview coordinate overflow retains stored collision inspection");
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
        check(viewport.set_camera_view(CollisionCameraView::front), "Front collision camera preset");
        auto camera_saved = viewport.view_state();
        check(camera_saved["yaw"] == 0. && camera_saved["pitch"] == 0.,
              "Preset retains body fit and uses Z up");
        auto camera_bad = camera_saved;
        camera_bad["up"] = {0., 0., 0.};
        rejected = false;
        try {
            viewport.restore_view_state(camera_bad);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && viewport.view_state() == camera_saved, "Bad camera is rejected atomically");
        CollisionViewport camera_restored;
        camera_restored.set_models(collision_wireframe(before), collision_wireframe(after), &a, &b, true);
        camera_restored.select("SM.Body", "sphere:name:Ball");
        camera_restored.restore_view_state(camera_saved);
        check(camera_restored.view_state() == camera_saved, "Orbit camera state round trip");

        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            cyui::Config config;
            config.themeKey = theme;
            cyui::apply(config);
            cyui::loadFonts();
            unsigned char *pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            for (const auto *locale : {"en", "fr"})
                for (int scene = 0; scene < 8; ++scene)
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
                        if (scene == 5)
                            viewport.draw();
                        if (scene == 6)
                            added_viewport.draw();
                        if (scene == 7)
                            failed_preview.draw();
                        ImGui::End();
                        ImGui::Render();
                        check(ImGui::GetDrawData()->TotalVtxCount > 0,
                              "Collision view draws in each theme and language");
                        if (scene == 5)
                            check(viewport.rendered_edges() == 192,
                                  "Both changed sphere wireframes render in every theme and language");
                        ++frames;
                    }
        }
        // Drive real ImGui mouse events against the after pane, then verify that an orbit
        // delta is applied once although the same camera is displayed twice.
        cyui::setLocale("en");
        io.ConfigInputTrickleEventQueue = false;
        ImVec2 pane_low{}, pane_high{};
        auto input_frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Collision mouse test", nullptr, ImGuiWindowFlags_NoSavedSettings);
            viewport.draw();
            pane_low = ImGui::GetItemRectMin();
            pane_high = ImGui::GetItemRectMax();
            ImGui::End();
            ImGui::Render();
            ++frames;
        };
        viewport.restore_view_state(
            {{"target", {1., 2., 3.}}, {"up", {0., 0., 1.}}, {"yaw", .7}, {"pitch", .35}, {"distance", 30.}});
        input_frame();
        input_frame();
        cyui::OrbitCamera shown;
        shown.target = {1., 2., 3.};
        shown.yaw = .7;
        shown.pitch = .35;
        shown.distance = 30.;
        shown.up = {0., 0., 1.};
        const auto matrix = cyui::cameraViewMatrix(shown);
        // Use the midpoint of the first sampled line of the after sphere.
        const double angle = 3.14159265358979323846 / 32.;
        CollisionPoint sample{1 + 7 * std::cos(angle), 2 + 7 * std::sin(angle), 3};
        const double x = matrix[0] * sample[0] + matrix[4] * sample[1] + matrix[8] * sample[2] + matrix[12],
                     y = matrix[1] * sample[0] + matrix[5] * sample[1] + matrix[9] * sample[2] + matrix[13],
                     z = matrix[2] * sample[0] + matrix[6] * sample[1] + matrix[10] * sample[2] + matrix[14];
        const float height = pane_high.y - pane_low.y - ImGui::GetTextLineHeightWithSpacing(),
                    width = pane_high.x - pane_low.x;
        const ImVec2 click{pane_low.x + float((x / (-z * .41421356237) * (height / width) + 1) * .5 * width),
                           pane_low.y + ImGui::GetTextLineHeightWithSpacing() +
                               float((1 - y / (-z * .41421356237)) * .5 * height)};
        io.AddMousePosEvent(click.x, click.y);
        input_frame();
        io.AddMouseButtonEvent(0, true);
        input_frame();
        check(viewport.take_picked_shape() == "sphere:name:Ball",
              "Clicking a rendered after wire picks its stored shape");
        io.AddMouseButtonEvent(0, false);
        input_frame();
        io.AddKeyEvent(ImGuiMod_Alt, true);
        io.AddMouseButtonEvent(0, true);
        input_frame();
        const double yaw_before = viewport.view_state()["yaw"].get<double>();
        io.AddMousePosEvent(click.x + 10, click.y);
        input_frame();
        const double yaw_after = viewport.view_state()["yaw"].get<double>();
        check(std::abs((yaw_after - yaw_before) + .07) < 1e-6,
              "Shared split camera applies orbit delta once");
        check(viewport.take_picked_shape().empty(), "Alt orbit does not pick a shape");
        io.AddMouseButtonEvent(0, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        input_frame();
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
