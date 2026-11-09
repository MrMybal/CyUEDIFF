#include <cyuediff/asset_view.hpp>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool v, const char *m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
} // namespace
int main() {
    try {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 800};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        cyui::setLocale("fr");
        check(cyui::tr("Modified") == "Modifi\u00e9" && cyui::tr("After") == "Apr\u00e8s" &&
                  cyui::tr("Material") == "Mat\u00e9riau" && cyui::tr("Node") == "N\u0153ud",
              "French accents survive UTF-8 source edits");
        cyui::setLocale("en");
        Json values = {{"Health", {{"type", "IntProperty"}, {"value", 42}}},
                       {"Mana", {{"type", "IntProperty"}, {"value", 7}}}};
        auto changed = values;
        changed["Health"]["value"] = 99;
        PropertyView property;
        property.set_values(&values, &changed, true);
        property.set_filter("Health");
        property.set_changed_only(false);
        std::size_t target = 0;
        for (std::size_t i = 0; i < property.table().rows.size(); ++i)
            if (property.table().rows[i].label == "Health")
                target = i;
        check(property.select_row(target), "Select named property");
        const auto property_state = property.view_state();
        auto shifted = values;
        shifted["Earlier"] = 0;
        PropertyView restored;
        restored.set_values(&shifted, &changed, true);
        restored.restore_view_state(property_state);
        check(restored.selected_row() && restored.selected_row()->path == property.selected_row()->path &&
                  restored.view_state() == property_state,
              "Property restores by path after row insertion");
        auto removed = changed;
        removed.erase("Health");
        restored.set_values(&removed, nullptr, false);
        restored.restore_view_state(property_state);
        check(!restored.selected_row(), "Removed property is not replaced by an unrelated selection");
        property.restore_view_state({{"filter", "no-match"}, {"selected", property_state["selected"]}});
        check(!property.visible_count() && !property.selected_row(), "Hidden property selection clears");
        property.restore_view_state(property_state);
        auto package = std::make_shared<Package>();
        package->source = "Fixture.uasset";
        for (const auto *name : {"A", "B"}) {
            ObjectExport e;
            e.path = e.name = name;
            e.class_name = "DataAsset";
            e.properties = values;
            e.coverage = "properties";
            package->exports.push_back(e);
        }
        AssetView asset;
        asset.set_packages(package);
        asset.restore_view_state({{"selected", "B"},
                                  {"filter", "B"},
                                  {"rawProperties", true},
                                  {"comparison", {{"query", "Health"}, {"split", false}, {"scroll", 32}}}});
        auto state = asset.view_state();
        auto reordered = std::make_shared<Package>(*package);
        std::swap(reordered->exports[0], reordered->exports[1]);
        AssetView resumed;
        resumed.set_packages(reordered);
        resumed.restore_view_state(state);
        check(resumed.view_state() == state,
              "Asset object identity and inspector states survive export reordering");
        auto malformed = state;
        malformed["before"]["mesh"] = {{"distance", -1}};
        bool failed = false;
        try {
            resumed.restore_view_state(malformed);
        } catch (const Error &) {
            failed = true;
        }
        check(failed && resumed.view_state() == state,
              "Invalid nested state is rejected before changing the view");
        resumed.restore_view_state({{"selected", "missing"}});
        check(resumed.view_state()["selected"] == "B", "Missing object uses the current package default");
        AssetView added;
        added.set_packages({}, package, true);
        added.restore_view_state({{"propertiesAfter", false}, {"graphAfter", false}, {"meshAfter", false}});
        check(added.view_state()["propertiesAfter"] == true && added.view_state()["meshAfter"] == true,
              "Absent before side never becomes active");
        AssetView removed_view;
        removed_view.set_packages(package, {}, true);
        removed_view.restore_view_state({{"propertiesAfter", true}, {"textureAfter", true}});
        check(removed_view.view_state()["propertiesAfter"] == false &&
                  removed_view.view_state()["textureAfter"] == false,
              "Absent after side never becomes active");
        auto asymmetric_before = std::make_shared<Package>(*package);
        TextureSource first_texture;
        first_texture.object_path = "First";
        first_texture.width = 2;
        first_texture.height = 1;
        auto second_texture = first_texture;
        second_texture.object_path = "Second";
        asymmetric_before->textures = {first_texture, second_texture};
        auto asymmetric_after = std::make_shared<Package>(*asymmetric_before);
        asymmetric_after->exports.resize(1);
        asymmetric_after->textures.resize(1);
        AssetView asymmetric;
        asymmetric.set_packages(asymmetric_before, asymmetric_after);
        asymmetric.restore_view_state({{"selected", "B"}, {"textureSelected", "Second"}});
        asymmetric.restore_view_state({{"propertiesAfter", true}, {"textureAfter", true}});
        check(asymmetric.view_state()["selected"] == "A",
              "Partial side restore clamps a selection beyond the peer export count");
        check(asymmetric.view_state()["textureSelected"] == "First",
              "Partial texture side restore clamps a selection beyond the peer source count");
        TextureSource source;
        source.object_path = "Texture";
        source.width = 2;
        source.height = 1;
        source.format = "TSF_BGRA8";
        TextureView texture;
        texture.set_source(source);
        texture.set_preview({(ImTextureID)42, 2, 1});
        Json image_state = {{"pan", {-70.0, 25.0}}, {"zoom", 2.0}, {"difference", true}};
        texture.restore_view_state(image_state);
        check(texture.view_state()["zoom"] == 2 && texture.view_state()["difference"] == false,
              "Image transform restores without an unavailable difference binding");
        ImageComparison diff;
        diff.comparable = diff.same_size = true;
        diff.before_width = diff.after_width = 2;
        diff.before_height = diff.after_height = 1;
        check(texture.set_difference({(ImTextureID)43, 2, 1}, diff), "Difference binding available");
        texture.restore_view_state(image_state);
        check(texture.view_state() == image_state, "Image transform and difference switch round trip");
        ImageComparison focused_pixels = diff;
        focused_pixels.changed_pixels = 1;
        focused_pixels.changed_bounds = {1, 0, 1, 1};
        check(texture.set_difference({(ImTextureID)43, 2, 1}, focused_pixels) && texture.focus_changes(),
              "Changed pixel focus uses prepared bounds");
        TextureView no_pixels;
        no_pixels.set_source(source);
        no_pixels.set_preview({(ImTextureID)42, 2, 1});
        no_pixels.set_difference({(ImTextureID)43, 2, 1}, diff);
        check(!no_pixels.focus_changes(), "Identical image cannot focus changes");
        auto invalid_pixels = focused_pixels;
        invalid_pixels.changed_bounds = {2, 0, 1, 1};
        no_pixels.set_difference({(ImTextureID)43, 2, 1}, invalid_pixels);
        check(!no_pixels.focus_changes(), "Out-of-range host-provided bounds are not applied");
        HdrImageComparison hdr_pixels;
        hdr_pixels.comparable = hdr_pixels.same_size = true;
        hdr_pixels.before_width = hdr_pixels.after_width = 2;
        hdr_pixels.before_height = hdr_pixels.after_height = 1;
        hdr_pixels.changed_pixels = 1;
        hdr_pixels.changed_bounds = {1, 0, 1, 1};
        hdr_pixels.mean_absolute_error = .125;
        hdr_pixels.max_sample_delta = .5;
        check(no_pixels.set_difference({(ImTextureID)43, 2, 1}, hdr_pixels) && no_pixels.focus_changes(),
              "HDR bridge retains bounds for navigation");
        auto mesh = std::make_shared<MeshGeometry>();
        mesh->vertices = {{0, {0, 0, 0}}, {1, {1, 0, 0}}, {2, {0, 1, 0}}};
        for (std::uint32_t i = 0; i < 3; ++i) {
            MeshInstance e;
            e.id = e.vertex = i;
            mesh->instances.push_back(e);
        }
        mesh->triangles = {{0, 0, 0, {0, 1, 2}}};
        MeshView mesh_view;
        mesh_view.set_mesh(mesh);
        Json camera = {
            {"target", {0, 0, 0}}, {"up", {0, 0, 1}}, {"yaw", .7}, {"pitch", .35}, {"distance", 5}};
        mesh_view.restore_view_state(camera);
        check(mesh_view.view_state() == camera, "Orbit camera round trip without a fit overwriting it");
        MeshView absent_mesh;
        absent_mesh.restore_view_state(camera);
        check(absent_mesh.view_state().empty(), "Missing geometry cannot receive a camera");
        auto large_mesh = std::make_shared<MeshGeometry>(*mesh);
        for (std::uint32_t i = 0; i < 3; ++i) {
            auto vertex = mesh->vertices[i];
            vertex.id += 3;
            vertex.position[0] += 1000;
            large_mesh->vertices.push_back(vertex);
            MeshInstance instance;
            instance.id = instance.vertex = i + 3;
            large_mesh->instances.push_back(instance);
        }
        large_mesh->triangles.push_back({1, 0, 0, {3, 4, 5}});
        auto peer_mesh = std::make_shared<MeshGeometry>(*large_mesh);
        peer_mesh->vertices[0].position[2] = .25;
        MeshView focused_mesh;
        focused_mesh.set_mesh(large_mesh, peer_mesh);
        check(focused_mesh.visible_edge_count() == 6, "Full mesh contains both disconnected triangles");
        focused_mesh.set_changed_only(true);
        check(focused_mesh.visible_edge_count() == 2 && focused_mesh.focus_changes(),
              "Changed-only mesh targets the affected triangle");
        check(focused_mesh.set_camera_view(MeshCameraView::right), "Select standard mesh orientation");
        Json changed_camera = focused_mesh.view_state();
        check(changed_camera["changedOnly"] == true && changed_camera["pitch"] == 0,
              "Changed mesh display state and camera can be saved");
        MeshView resumed_mesh;
        resumed_mesh.set_mesh(large_mesh, peer_mesh);
        resumed_mesh.restore_view_state(changed_camera);
        check(resumed_mesh.view_state() == changed_camera && resumed_mesh.visible_edge_count() == 2,
              "Changed-only camera round trip");
        MeshView stable_mesh;
        stable_mesh.set_mesh(mesh, mesh);
        check(!stable_mesh.focus_changes(), "Unchanged mesh has no focus target");
        stable_mesh.set_changed_only(true);
        check(stable_mesh.visible_edge_count() == 0, "Unchanged comparison has an empty filtered view");
        check(!absent_mesh.focus_changes() && !absent_mesh.set_camera_view(MeshCameraView::top),
              "Missing mesh refuses camera actions");
        Package graphs;
        ObjectExport graph;
        graph.coverage = "properties"; // This fixture supplies a complete decoded tag inventory.
        graph.path = graph.name = "Graph";
        graph.properties = {{"GraphGuid", {{"value", std::string(32, '1')}}},
                            {"Nodes", {{"value", Json::array({"Graph.A"})}}}};
        graphs.exports.push_back(graph);
        ObjectExport node;
        node.path = "Graph.A";
        node.name = "A";
        node.outer_index = 1;
        node.class_name = "K2Node";
        node.native_coverage = "pins";
        node.coverage = "properties";
        node.properties = {{"NodeGuid", {{"value", std::string(32, 'a')}}}};
        node.native_data = {{"kind", "edgraph-node"}, {"pins", Json::object()}};
        graphs.exports.push_back(node);
        BlueprintView blueprint;
        blueprint.set_package(graphs);
        check(!blueprint.empty(), "Graph fixture prepared");
        auto graph_key = blueprint.view_state()["selected"].get<std::string>();
        Json graph_state = {{"selected", graph_key},
                            {"views", {{graph_key, {{"pan", {150, 80}}, {"zoom", .8}}}}}};
        graph_state["layout"] = {{"diagnostics", true}};
        blueprint.restore_view_state(graph_state);
        check(blueprint.view_state() == graph_state, "Blueprint camera uses graph identity");
        check(blueprint.select_node(graph_key, std::string(32, 'a')), "Select Blueprint by graph/node GUID");
        graph_state = blueprint.view_state();
        blueprint.restore_view_state(graph_state);
        check(graph_state["inspectors"][graph_key]["selected"] == std::string(32, 'a'),
              "Blueprint selection is saved independently of numeric node IDs");
        check(!blueprint.select_node(graph_key, "missing"), "Missing node selection is refused");
        auto changed_graphs = graphs;
        changed_graphs.exports.insert(changed_graphs.exports.begin(), ObjectExport{});
        changed_graphs.exports[2].outer_index = 2;
        BlueprintView second_graph;
        second_graph.set_package(changed_graphs);
        second_graph.restore_view_state(graph_state);
        check(second_graph.view_state() == graph_state, "Graph state survives export index movement");
        auto peer_graphs = graphs;
        peer_graphs.exports[1].properties["Power"] = {{"type", "IntProperty"}, {"value", 17}};
        BlueprintView before_graph, after_graph;
        before_graph.set_package(graphs, &peer_graphs);
        after_graph.set_package(peer_graphs, &graphs, true);
        check(before_graph.next_changed_node() && after_graph.next_changed_node(),
              "Both comparison sides navigate changed Blueprint nodes");
        check(before_graph.view_state()["inspectors"][graph_key]["selected"] ==
                  after_graph.view_state()["inspectors"][graph_key]["selected"],
              "Before and after use the same stable changed-node identity");
        BlueprintView unchanged_graph;
        unchanged_graph.set_package(graphs, &graphs);
        check(!unchanged_graph.next_changed_node(), "Unchanged graph has no changed-node target");
        Package materials;
        ObjectExport material;
        material.coverage = "properties";
        material.path = material.name = "M";
        material.class_name = "/Script/Engine.Material";
        material.properties = {{"Expressions", {{"value", Json::array({"M.Node"})}}}};
        materials.exports.push_back(material);
        ObjectExport expression;
        expression.coverage = "properties";
        expression.path = "M.Node";
        expression.class_name = "/Script/Engine.MaterialExpressionScalarParameter";
        expression.outer_index = 1;
        expression.properties = {{"MaterialExpressionGuid", {{"value", std::string(32, 'e')}}}};
        materials.exports.push_back(expression);
        MaterialView material_view;
        material_view.set_package(materials);
        check(!material_view.empty(), "Material fixture prepared");
        Json material_state = {{"selected", "M"}, {"views", {{"M", {{"pan", {40, 60}}, {"zoom", 1.2}}}}}};
        material_state["layout"] = {{"diagnostics", true}};
        material_view.restore_view_state(material_state);
        check(material_view.view_state() == material_state, "Material graph camera round trip");
        auto material_peer = materials;
        material_peer.exports[1].properties["DefaultValue"] = {{"type", {{"name", "FloatProperty"}}},
                                                               {"value", 0.75}};
        MaterialView compared_material;
        compared_material.set_package(materials, &material_peer);
        check(compared_material.next_changed_node(), "Navigate changed material expression");
        auto selected_expression = compared_material.view_state()["inspectors"]["M"]["selected"];
        check(material_view.select_node("M", selected_expression.get<std::string>()),
              "Select material expression by snapshot identity");
        material_state = material_view.view_state();
        material_view.restore_view_state(material_state);
        MaterialView restored_material;
        restored_material.set_package(materials);
        restored_material.restore_view_state(material_state);
        check(restored_material.view_state() == material_state, "Material expression selection round trip");
        ObjectExport mi;
        mi.path = mi.name = "MI";
        mi.class_name = "/Script/Engine.MaterialInstanceConstant";
        mi.coverage = "properties";
        mi.properties = {
            {"ScalarParameterValues",
             {{"value",
               Json::array(
                   {{{"ParameterName", {{"value", "Power"}}}, {"ParameterValue", {{"value", 0.5}}}},
                    {{"ParameterName", {{"value", "Stable"}}}, {"ParameterValue", {{"value", 1.0}}}}})}}}};
        auto instances = materials;
        instances.exports.push_back(mi);
        auto peer_instances = instances;
        peer_instances.exports.back()
            .properties["ScalarParameterValues"]["value"][0]["ParameterValue"]["value"] = 0.9;
        MaterialView parameter_view;
        parameter_view.set_package(instances, &peer_instances);
        check(parameter_view.select_object("MI") && parameter_view.visible_parameter_count() == 2,
              "Instance override union prepared");
        check(parameter_view.set_parameter_filter("Power", true) &&
                  parameter_view.visible_parameter_count() == 1,
              "Instance filters select changed named override");
        auto parameter_state = parameter_view.view_state();
        MaterialView resumed_parameters;
        resumed_parameters.set_package(instances, &peer_instances);
        resumed_parameters.restore_view_state(parameter_state);
        check(resumed_parameters.visible_parameter_count() == 1 &&
                  resumed_parameters.view_state() == parameter_state,
              "Per-instance filter persists by object path");
        check(parameter_view.set_parameter_filter("Stable", true) &&
                  parameter_view.visible_parameter_count() == 0,
              "Unchanged override is hidden when requested");
        check(parameter_view.set_parameter_filter("missing") && parameter_view.visible_parameter_count() == 0,
              "Missing parameter produces an explicit empty state");
        const auto original_package = package->to_json();
        int frames = 0;
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
                        ImGui::Begin("Restored views");
                        switch (scene) {
                        case 0:
                            property.draw();
                            break;
                        case 1:
                            resumed.draw("Asset", false);
                            break;
                        case 2:
                            blueprint.draw();
                            break;
                        case 3:
                            material_view.draw();
                            break;
                        case 4:
                            texture.draw();
                            break;
                        case 5:
                            mesh_view.draw();
                            break;
                        case 6:
                            focused_mesh.draw("Changed mesh");
                            break;
                        case 7:
                            stable_mesh.draw("Unchanged mesh");
                            break;
                        }
                        ImGui::End();
                        ImGui::Render();
                        check(ImGui::GetDrawData()->TotalVtxCount > 0, "Restored view draw data");
                        ++frames;
                    }
        }
        check(blueprint.view_state()["views"] == graph_state["views"] &&
                  blueprint.view_state()["inspectors"][graph_key]["selected"] == std::string(32, 'a') &&
                  material_view.view_state()["views"] == material_state["views"] &&
                  material_view.view_state()["inspectors"]["M"]["selected"] == selected_expression &&
                  mesh_view.view_state() == camera,
              "Drawing restored views does not auto-fit over saved navigation");
        check(focused_mesh.view_state()["target"][0].get<double>() < 2 &&
                  focused_mesh.view_state()["distance"].get<double>() < 10,
              "Fit changed geometry excludes distant unchanged geometry");
        check(focused_mesh.view_state()["pitch"] == 0,
              "Fitting changes retains the selected camera orientation");
        check(package->to_json() == original_package, "Restoring views never modifies package data");
        ImGui::DestroyContext();
        std::cout << checks << " view state UI checks passed\n"
                  << frames << " restored view UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
