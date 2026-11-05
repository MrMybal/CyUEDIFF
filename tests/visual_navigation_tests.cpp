#include <cyuediff/asset_view.hpp>
#include "collision_fixture.hpp"
#include "animation_model.hpp"
#include "platform_win32.hpp"
#include <fstream>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
namespace {
Package fixture() {
    Package p;
    p.source = "SyntheticNavigation.uasset";
    ObjectExport graph;
    graph.path = graph.name = "EventGraph";
    graph.properties = {{"GraphGuid", {{"value", std::string(32, '1')}}},
                        {"Nodes", {{"value", Json::array({"EventGraph.Input", "EventGraph.Output"})}}}};
    p.exports.push_back(graph);
    for (int i = 0; i < 2; ++i) {
        ObjectExport node;
        node.name = i ? "Output" : "Input";
        node.path = "EventGraph." + node.name;
        node.class_name = "K2Node_CallFunction";
        node.outer_index = 1;
        node.native_coverage = "pins";
        node.coverage = "properties";
        node.properties = {{"NodeGuid", {{"value", std::string(32, i ? 'b' : 'a')}}},
                           {"NodePosX", {{"value", i * 300}}},
                           {"NodePosY", {{"value", 0}}},
                           {"FunctionReference",
                            {{"value", {{"MemberName", {{"value", i ? "PrintString" : "BuildMessage"}}}}}}}};
        Json pin = {{"name", "Message"},
                    {"direction", i ? "input" : "output"},
                    {"type", {{"category", "text"}}},
                    {"defaultText", {{"kind", "localized"}, {"source", "Before message"}}},
                    {"linkedTo", Json::array({{{"node", i ? "EventGraph.Input" : "EventGraph.Output"},
                                               {"pin", std::string(32, i ? '1' : '2')}}})},
                    {"subPins", Json::array()},
                    {"parentPin", nullptr},
                    {"passThrough", nullptr}};
        node.native_data = {{"kind", "edgraph-node"}, {"pins", {{std::string(32, i ? '2' : '1'), pin}}}};
        p.exports.push_back(node);
    }
    ObjectExport material;
    material.path = material.name = "M_Surface";
    material.class_name = "/Script/Engine.Material";
    material.coverage = "properties";
    material.properties = {{"Expressions", {{"value", Json::array({"M_Surface.Power"})}}}};
    p.exports.push_back(material);
    ObjectExport expression;
    expression.path = "M_Surface.Power";
    expression.class_name = "/Script/Engine.MaterialExpressionScalarParameter";
    expression.coverage = "properties";
    expression.outer_index = 4;
    expression.properties = {{"MaterialExpressionGuid", {{"value", std::string(32, 'e')}}},
                             {"ParameterName", {{"value", "Power"}}},
                             {"DefaultValue", {{"type", {{"name", "FloatProperty"}}}, {"value", 0.5}}}};
    p.exports.push_back(expression);
    ObjectExport instance;
    instance.path = instance.name = "MI_Surface";
    instance.class_name = "/Script/Engine.MaterialInstanceConstant";
    instance.coverage = "properties";
    instance.properties = {
        {"Parent", {{"value", "M_Surface"}}},
        {"ScalarParameterValues",
         {{"value",
           Json::array(
               {{{"ParameterName", {{"value", "Power"}}}, {"ParameterValue", {{"value", 0.5}}}},
                {{"ParameterName", {{"value", "Roughness"}}}, {"ParameterValue", {{"value", 0.75}}}}})}}}};
    p.exports.push_back(instance);
    return p;
}
std::shared_ptr<MeshGeometry> geometry() {
    auto mesh = std::make_shared<MeshGeometry>();
    mesh->vertices = {{0, {0, 0, 0}},    {1, {1, 0, 0}},    {2, {0, 1, 0}},
                      {3, {1000, 0, 0}}, {4, {1001, 0, 0}}, {5, {1000, 1, 0}}};
    for (std::uint32_t i = 0; i < 6; ++i) {
        MeshInstance instance;
        instance.id = instance.vertex = i;
        mesh->instances.push_back(instance);
    }
    mesh->triangles = {{0, 0, 0, {0, 1, 2}}, {1, 0, 0, {3, 4, 5}}};
    mesh->material_slots[0] = "Surface";
    return mesh;
}
} // namespace
int main(int argc, char **argv) {
    try {
        fs::path capture_dir;
        if (argc == 3 && std::string(argv[1]) == "--capture-dir") {
            capture_dir = fs::u8path(argv[2]);
            fs::create_directories(capture_dir);
        } else if (argc != 1)
            throw std::runtime_error("Expected [--capture-dir DIRECTORY]");
        int checks = 0, frames = 0, captures = 0;
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        NativeWindow window(true);
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        AssetView::register_translations();
        cyui::Config config;
        config.density = cyui::Density::compact;
        cyui::apply(config);
        cyui::loadFonts();
        {
            Renderer renderer(window.handle, true);
            auto before = fixture(), after = before;
            after.exports[1].native_data["pins"][std::string(32, '1')]["defaultText"]["source"] =
                "After message";
            after.exports[4].properties["DefaultValue"]["value"] = 0.9;
            after.exports[5].properties["ScalarParameterValues"]["value"][0]["ParameterValue"]["value"] = 0.9;
            const auto original_before = before.to_json(), original_after = after.to_json();
            BlueprintView blueprint, no_nodes;
            blueprint.set_package(before, &after);
            no_nodes.set_package(before, &after);
            check(blueprint.next_changed_node(), "Blueprint fixture has a changed node");
            auto key = no_nodes.view_state()["selected"].get<std::string>();
            no_nodes.restore_view_state({{"inspectors", {{key, {{"filter", "NoMatchingNode"}}}}}});
            MaterialView material, parameters;
            material.set_package(before, &after);
            parameters.set_package(before, &after);
            check(material.next_changed_node(), "Material fixture has a changed expression");
            check(parameters.select_object("MI_Surface") && parameters.set_parameter_filter("Power", true) &&
                      parameters.visible_parameter_count() == 1,
                  "Changed instance fixture");
            auto mesh = geometry(), peer = std::make_shared<MeshGeometry>(*mesh);
            peer->vertices[0].position[2] = .25;
            MeshView changed_mesh, stable_mesh;
            changed_mesh.set_mesh(mesh, peer);
            changed_mesh.set_changed_only(true);
            check(changed_mesh.focus_changes(), "Changed mesh fixture");
            stable_mesh.set_mesh(mesh, mesh);
            stable_mesh.set_changed_only(true);
            ImageRgba8 image{128, 128, std::vector<std::uint8_t>(128 * 128 * 4, 0)};
            for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 0; x < 128; ++x) {
                    auto i = (y * 128 + x) * 4;
                    auto sample = std::uint8_t(((x / 16 + y / 16) % 2) ? 170 : 70);
                    image.pixels[i] = sample;
                    image.pixels[i + 1] = sample;
                    image.pixels[i + 2] = sample;
                    image.pixels[i + 3] = 255;
                }
            auto changed = image;
            for (unsigned y = 48; y < 64; ++y)
                for (unsigned x = 56; x < 72; ++x)
                    changed.pixels[(y * 128 + x) * 4] = 255;
            auto delta = compare_images(image, changed, 0, true);
            auto image_gpu = renderer.upload(image), mask_gpu = renderer.upload(delta.heatmap);
            TextureSource source;
            source.object_path = "T_Checker";
            source.width = source.height = 128;
            source.format = "TSF_BGRA8";
            source.compression = "TSCF_None";
            TextureView texture;
            texture.set_source(source);
            texture.set_preview({(ImTextureID)image_gpu.Get(), 128, 128});
            texture.set_difference({(ImTextureID)mask_gpu.Get(), 128, 128}, delta);
            check(texture.focus_changes(), "Texture fixture has changed bounds");
            auto collision_before = cyuediff::test::collision_extended_fixture(),
                 collision_after = collision_before;
            collision_after.exports[0]
                .properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Radius"]["value"] = 7.;
            CollisionView collision;
            collision.set_packages(&collision_before, &collision_after);
            check(collision.next_changed_shape(), "Native collision fixture has a changed shape");
            collision.restore_view_state(
                {{"properties", {{"filter", "radius"}, {"selected", "/geometry/radius"}}}});
            auto all_before = cyuediff::test::collision_extended_fixture();
            auto &all_agg = all_before.exports[0].properties["AggGeom"]["value"];
            all_agg["BoxElems"]["value"][0]["Center"]["value"] = {-40., 0., 0.};
            all_agg["SphylElems"]["value"][0]["Center"]["value"] = {0., -25., 0.};
            all_agg["SphereElems"]["value"][0]["Center"]["value"] = {25., 0., 0.};
            all_agg["SphereElems"]["value"][1]["Center"]["value"] = {40., 0., 0.};
            all_agg["TaperedCapsuleElems"]["value"][0]["Center"]["value"] = {0., 25., 0.};
            all_agg["ConvexElems"]["value"][0]["Transform"]["value"]["scale"] = {10., 10., 10.};
            auto all_after = all_before;
            all_after.exports[0]
                .properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Radius"]["value"] = 7.;
            all_after.exports[0].properties["AggGeom"]["value"]["BoxElems"]["value"][0]["Rotation"]["value"] =
                {20., 35., 45.};
            CollisionView collision_all, collision_incomplete;
            collision_all.set_packages(&all_before, &all_after);
            auto incomplete_package = all_after;
            incomplete_package.exports[0].properties["AggGeom"]["value"]["ConvexElems"]["value"][0].erase(
                "Transform");
            collision_incomplete.set_packages(&incomplete_package);
            auto skeleton_before = cyuediff::test::skeleton_model(), skeleton_after = skeleton_before;
            skeleton_after.exports[0].native_data["localPoses"][1]["translation"][0] = 12.;
            SkeletonView skeleton;
            skeleton.set_packages(&skeleton_before, &skeleton_after);
            check(skeleton.next_changed_bone(), "Native skeleton fixture has a changed bone");
            skeleton.restore_view_state(
                {{"inspection",
                  {{"selected", "arm"},
                   {"properties", {{"filter", "translation"}, {"selected", "/localPose/translation/0"}}}}}});
            auto pose_before = skeleton_before, pose_after = pose_before;
            pose_before.exports[0].native_data["bones"][2]["parentIndex"] = 1;
            pose_before.exports[0].native_data["localPoses"][2]["translation"] = {0., 10., 5.};
            pose_after = pose_before;
            pose_after.exports[0].native_data["localPoses"][0]["translation"] = {0., 0., 4.};
            SkeletonView reference_pose_view, reference_unavailable, reference_added;
            reference_pose_view.set_packages(&pose_before, &pose_after);
            reference_pose_view.set_pose_preview(true);
            reference_pose_view.select_bone("arm");
            auto unavailable = pose_before;
            unavailable.exports[0].native_data["localPoses"][0]["rotation"] = {0., 0., 0., 0.};
            reference_unavailable.set_packages(&unavailable);
            reference_unavailable.set_pose_preview(true);
            reference_added.set_packages(nullptr, &pose_after, true);
            reference_added.set_pose_preview(true);
            auto retarget_before = cyuediff::test::retarget_model(), retarget_after = retarget_before;
            retarget_after.exports[0].native_data["retargeting"]["sources"]["A~/pose"]["localPoses"][0]
                                                 ["translation"] = {0., 0., 5.};
            SkeletonView retarget_view, retarget_missing;
            retarget_view.set_packages(&retarget_before, &retarget_after);
            retarget_view.set_pose_preview(true);
            retarget_view.select_retarget_sources("A~/pose", "A~/pose");
            retarget_view.select_bone("arm");
            auto removed_source = retarget_after;
            removed_source.exports[0].native_data["retargeting"]["sources"].erase("A~/pose");
            retarget_missing.set_packages(&retarget_before, &removed_source);
            retarget_missing.set_pose_preview(true);
            retarget_missing.select_retarget_sources("A~/pose", "A~/pose");
            retarget_missing.select_bone("arm");
            auto animation_before = cyuediff::test::animation_model(), animation_after = animation_before;
            animation_after.exports[0]
                .native_data["curveMetadata"]["containers"]["AnimationCurves"]["Smile~/curve"]["maxLOD"] = 3;
            animation_after.exports[0].native_data["syncMarkers"]["names"] = {"Right", "Left"};
            SkeletonView animation_view, animation_unknown, animation_empty;
            animation_view.set_packages(&animation_before, &animation_after);
            animation_view.set_animation_preview(true);
            animation_view.animation_view().select_curve("AnimationCurves", "Smile~/curve");
            auto animation_state = animation_view.view_state();
            animation_state["animation"]["properties"] = {{"changedOnly", false}};
            animation_view.restore_view_state(animation_state);
            auto stripped_animation = animation_after;
            stripped_animation.exports[0].native_data["syncMarkers"] = {
                {"coverage", "stripped"}, {"stripFlags", {{"global", 1}, {"class", 0}}}};
            animation_unknown.set_packages(&animation_before, &stripped_animation);
            animation_unknown.set_animation_preview(true);
            animation_unknown.animation_view().select_marker("Left");
            animation_empty.set_packages(&skeleton_before);
            animation_empty.set_animation_preview(true);
            Json saved = Json::object();
            for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
                cyui::setTheme(theme);
                for (const auto *locale : {"en", "fr"}) {
                    cyui::setLocale(locale);
                    for (int scene = 0; scene < 25; ++scene) {
                        const char *names[] = {"blueprint",
                                               "material",
                                               "parameters",
                                               "mesh",
                                               "texture",
                                               "no-nodes",
                                               "no-edges",
                                               "narrow-blueprint",
                                               "collisions",
                                               "collision-all",
                                               "collision-narrow",
                                               "collision-incomplete",
                                               "skeleton",
                                               "skeleton-narrow",
                                               "reference-pose",
                                               "reference-narrow",
                                               "reference-unavailable",
                                               "reference-added",
                                               "retarget",
                                               "retarget-narrow",
                                               "retarget-missing",
                                               "animation-metadata",
                                               "animation-narrow",
                                               "animation-unknown",
                                               "animation-empty"};
                        for (int n = 0; n < 3; ++n) {
                            check(window.pump(), "Hidden window pump");
                            if (window.resize_width && window.resize_height) {
                                renderer.resize(window.resize_width, window.resize_height);
                                window.resize_width = window.resize_height = 0;
                            }
                            renderer.new_frame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize((scene == 7 || scene == 10 || scene == 13 ||
                                                      scene == 15 || scene == 19 || scene == 22)
                                                         ? ImVec2{640, io.DisplaySize.y}
                                                         : io.DisplaySize);
                            ImGui::Begin("Visual navigation", nullptr,
                                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoSavedSettings);
                            switch (scene) {
                            case 21:
                            case 22:
                                animation_view.draw();
                                break;
                            case 23:
                                animation_unknown.draw();
                                break;
                            case 24:
                                animation_empty.draw();
                                break;
                            case 0:
                                blueprint.draw();
                                break;
                            case 1:
                                material.draw();
                                break;
                            case 2:
                                parameters.draw();
                                break;
                            case 3:
                                changed_mesh.draw();
                                break;
                            case 4:
                                texture.draw();
                                break;
                            case 5:
                                no_nodes.draw();
                                break;
                            case 6:
                                stable_mesh.draw();
                                break;
                            case 7:
                                blueprint.draw("Narrow blueprint");
                                break;
                            case 8:
                                collision.draw();
                                break;
                            case 9:
                            case 10:
                                collision_all.draw();
                                break;
                            case 11:
                                collision_incomplete.draw();
                                break;
                            case 18:
                            case 19:
                                retarget_view.draw();
                                break;
                            case 20:
                                retarget_missing.draw();
                                break;
                            case 14:
                            case 15:
                                reference_pose_view.draw();
                                break;
                            case 16:
                                reference_unavailable.draw();
                                break;
                            case 17:
                                reference_added.draw();
                                break;
                            case 12:
                            case 13:
                                skeleton.draw();
                                break;
                            }
                            ImGui::End();
                            ImGui::Render();
                            check(ImGui::GetDrawData()->TotalVtxCount > 0, "Native draw data");
                            renderer.draw();
                            if (n == 2 && !capture_dir.empty()) {
                                write_png(capture_dir / (std::string(theme) + "-" + locale + "-" +
                                                         names[scene] + ".png"),
                                          renderer.capture());
                                ++captures;
                            }
                            renderer.present(false);
                            ++frames;
                        }
                        if (scene == 3)
                            check(changed_mesh.rendered_edges() == 2 &&
                                      changed_mesh.view_state()["target"][0].get<double>() < 2,
                                  "Native mesh renderer isolates changed geometry");
                        if (scene == 7) {
                            auto graph_state = blueprint.view_state();
                            const auto graph_id = graph_state["selected"].get<std::string>();
                            const auto &camera = graph_state["views"][graph_id];
                            const auto center_y =
                                (32. - camera["pan"][1].get<double>()) * camera["zoom"].get<double>();
                            check(center_y > 0 && center_y < io.DisplaySize.y * .4,
                                  "Narrow inspector keeps the selected node within its canvas");
                        }
                        if (scene == 13) {
                            const auto hierarchy = skeleton.view_state();
                            const auto &camera = hierarchy.at("canvas");
                            const auto x =
                                (355. - camera["pan"][0].get<double>()) * camera["zoom"].get<double>();
                            check(x > 100 && x < 540,
                                  "Narrow skeleton view keeps the restored selected bone visible");
                        }
                        if (scene == 18 || scene == 19)
                            check(retarget_view.rendered_pose_joints() == 6,
                                  "Selected retarget sources render independent poses");
                        if (scene == 20)
                            check(retarget_missing.rendered_pose_joints() == 3,
                                  "Deleted source does not display raw replacements");
                        if (scene == 14 || scene == 15)
                            check(reference_pose_view.rendered_pose_joints() == 6,
                                  "Both native pose panes render all ready joints");
                        if (scene == 16)
                            check(reference_unavailable.rendered_pose_joints() == 0,
                                  "Unavailable pose does not render invented joints");
                        if (scene == 4)
                            check(texture.view_state()["zoom"].get<double>() > 1,
                                  "Native texture focuses changed pixels");
                    }
                }
            }
            check(before.to_json() == original_before && after.to_json() == original_after,
                  "Visual navigation never modifies package snapshots");
            saved["blueprint"] = blueprint.view_state();
            saved["material"] = material.view_state();
            saved["parameters"] = parameters.view_state();
            saved["collisions"] = collision.view_state();
            saved["skeleton"] = skeleton.view_state();
            saved["retarget"] = retarget_view.view_state();
            saved["referencePose"] = reference_pose_view.view_state();
            saved["mesh"] = changed_mesh.view_state();
            saved["texture"] = texture.view_state();
            saved["frames"] = frames;
            saved["checks"] = checks;
            saved["captures"] = captures;
            if (!capture_dir.empty()) {
                std::ofstream out(capture_dir / "report.json", std::ios::binary);
                out << saved.dump(2);
                if (!out)
                    throw std::runtime_error("Cannot write native navigation report");
            }
        }
        ImGui::DestroyContext();
        std::cout << checks << " native navigation checks passed\n"
                  << frames << " hidden WARP frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
