#include <cyuediff/mesh_skin.hpp>
#include <cyuediff/asset_view.hpp>
#include <cyuediff/catalog_view.hpp>
#include <iostream>
int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    if (cyui::locale() != "en")
        return 11;
    cyuediff::AssetView::register_translations();
    const auto locales = cyui::availableLocales();
    if (locales.size() < 2 || locales[0].code != "en" || locales[1].code != "fr")
        return 12;
    if (cyui::tr("Before") != "Before" || !cyui::setLocale("fr") || cyui::tr("Before") != "Avant")
        return 13;
    if (!cyui::setLocale("en") || cyui::tr("Before") != "Before")
        return 14;
    cyuediff::CatalogView::register_translations();
    auto catalogue_items = std::make_shared<std::vector<cyuediff::CatalogItem>>();
    catalogue_items->push_back({"Content/Sword.uasset", "/Game/Épée", ".uasset", true, true});
    catalogue_items->push_back({"Content/World.umap", "/Game/World", ".umap", false, true});
    for (int i = 0; i < 2500; ++i)
        catalogue_items->push_back({"Content/Item" + std::to_string(i) + ".uasset",
                                    "Item" + std::to_string(i), ".uasset", true, false});
    cyuediff::CatalogView catalog_view;
    catalog_view.set_items(catalogue_items, true);
    catalog_view.set_filter("epee");
    if (catalog_view.visible_count() != 1 || !catalog_view.select(0) || catalog_view.select(99999))
        return 15;
    catalog_view.set_filter("nothing-matches");
    if (catalog_view.visible_count() != 0 || catalog_view.selected() == 0)
        return 17;
    catalog_view.set_filter("");
    if (catalog_view.visible_count() != catalogue_items->size())
        return 16;
    cyuediff::PropertyView property_view;
    cyuediff::Json property_before = {
        {"/Script/Test.Asset.Health", {{"type", "IntProperty"}, {"value", 73}}}};
    auto property_after = property_before;
    property_after["/Script/Test.Asset.Health"]["value"] = 91;
    property_view.set_values(&property_before, &property_after, true);
    property_view.set_filter("91");
    if (property_view.visible_count() != 1 || !property_view.select_row(1) ||
        property_view.select_row(999999))
        return 18;
    property_view.set_filter("nothing-matches");
    if (property_view.visible_count() || property_view.selected_row())
        return 19;
    property_view.set_filter("");
    property_view.set_changed_only(false);
    property_view.select_row(1);
    cyuediff::PropertyView unicode_view;
    cyuediff::Json unicode_before = std::string("Caf\xc3\xa9 \xf0\x9f\x98\x80"),
                   unicode_after = std::string("Caf\xc3\xa9 \xf0\x9f\x98\x81");
    unicode_view.set_values(&unicode_before, &unicode_after, true);
    const auto *unicode_row = unicode_view.selected_row();
    if (!unicode_row || unicode_row->before == unicode_row->after ||
        cyuediff::Json::parse(unicode_row->before) != unicode_before)
        return 20;
    if (sizeof(ImWchar) == 2 && unicode_row->before.find("\\ud83d\\ude00") == std::string::npos)
        return 21;
    auto before = std::make_shared<cyuediff::Package>();
    before->source = "Sample.uasset";
    cyuediff::ObjectExport object;
    object.path = "Sample";
    object.class_name = "DataAsset";
    object.coverage = "properties";
    object.properties = {{"Value", {{"value", 1}}}};
    before->exports.push_back(object);
    auto after = std::make_shared<cyuediff::Package>(*before);
    after->exports[0].properties["Value"]["value"] = 2;
    cyuediff::ObjectExport graph;
    graph.name = graph.path = "Graph";
    graph.properties = {{"GraphGuid", {{"value", "11111111111111111111111111111111"}}},
                        {"Nodes", {{"value", cyuediff::Json::array({"Graph.A", "Graph.B"})}}}};
    before->exports.push_back(graph);
    for (int i = 0; i < 2; ++i) {
        cyuediff::ObjectExport node;
        node.name = i ? "B" : "A";
        node.path = "Graph." + node.name;
        node.class_name = "K2Node_CallFunction";
        node.outer_index = 2;
        node.native_coverage = "pins";
        auto node_id = std::string(32, i ? 'b' : 'a');
        auto pin_id = std::string(32, i ? '2' : '1');
        auto peer_id = std::string(32, i ? '1' : '2');
        node.properties = {{"NodeGuid", {{"value", node_id}}}, {"NodePosX", {{"value", i * 300}}}};
        cyuediff::Json pin = {
            {"name", "execute"},
            {"direction", i ? "input" : "output"},
            {"type", {{"category", "exec"}}},
            {"linkedTo", cyuediff::Json::array({{{"node", i ? "Graph.A" : "Graph.B"}, {"pin", peer_id}}})},
            {"subPins", cyuediff::Json::array()},
            {"parentPin", nullptr},
            {"passThrough", nullptr}};
        node.native_data = {{"kind", "edgraph-node"}, {"pins", {{pin_id, pin}}}};
        before->exports.push_back(node);
    }
    cyuediff::TextureSource texture;
    texture.object_path = "ExampleTexture";
    texture.width = 2;
    texture.height = 1;
    texture.format = "TSF_BGRA8";
    texture.compression = "TSCF_None";
    before->textures.push_back(texture);
    cyuediff::TextureView texture_view;
    texture_view.set_source(texture);
    if (!texture_view.set_preview({(ImTextureID)42, 2, 1}) || texture_view.set_preview({}))
        return 4;
    cyuediff::ObjectExport material;
    material.name = material.path = "M";
    material.class_name = "/Script/Engine.Material";
    material.coverage = "properties";
    material.properties = {{"Expressions", {{"value", cyuediff::Json::array({"M.Node"})}}}};
    material.properties["BaseColor"] = {
        {"type",
         {{"name", "StructProperty"},
          {"parameters", cyuediff::Json::array({{{"name", "ExpressionInput"}}})}}},
        {"value", {{"expression", "M.Node"}, {"outputIndex", 0}, {"inputName", "None"}}}};
    auto material_index = static_cast<int>(before->exports.size() + 1);
    before->exports.push_back(material);
    cyuediff::ObjectExport expression;
    expression.path = "M.Node";
    expression.class_name = "/Script/Engine.MaterialExpressionScalarParameter";
    expression.coverage = "properties";
    expression.outer_index = material_index;
    expression.properties = {{"MaterialExpressionGuid", {{"value", std::string(32, 'e')}}}};
    before->exports.push_back(expression);
    cyuediff::ObjectExport instance;
    instance.path = "MI";
    instance.class_name = "/Script/Engine.MaterialInstanceConstant";
    instance.coverage = "properties";
    instance.properties = {{"ScalarParameterValues",
                            {{"value", cyuediff::Json::array({{{"ParameterName", {{"value", "Power"}}},
                                                               {"ParameterValue", {{"value", 0.5}}}}})}}}};
    before->exports.push_back(instance);
    cyuediff::MaterialView material_view, instance_view;
    material_view.set_package(*before);
    instance_view.set_package(*before);
    if (material_view.empty() || !instance_view.select_object("MI") || instance_view.select_object("missing"))
        return 10;
    before->exports[2].native_data["pins"][std::string(32, '1')]["defaultText"] = {
        {"kind", "text-generator"},
        {"generator", "PluginText"},
        {"contents", {{"bytes", 4}, {"sha256", "before"}}}};
    auto unchanged = before->to_json();
    cyuediff::BlueprintView graph_view;
    graph_view.set_package(*before);
    if (graph_view.empty())
        return 2;
    cyuediff::AssetView addition, removal;
    addition.set_packages({}, after, true);
    removal.set_packages(before, {}, true);
    cyuediff::AssetView view;
    view.set_packages(before, after);
    if (!view.set_texture_preview("ExampleTexture", {(ImTextureID)42, 2, 1}) ||
        view.set_texture_preview("missing", {(ImTextureID)42, 2, 1}))
        return 5;
    if (!view.clear_texture_preview("ExampleTexture") || view.clear_texture_preview("missing"))
        return 6;
    view.set_texture_preview("ExampleTexture", {(ImTextureID)42, 2, 1});
    auto mesh = std::make_shared<cyuediff::MeshGeometry>();
    mesh->vertices = {{0, {0, 0, 0}}, {1, {1, 0, 0}}, {2, {0, 1, 0}}};
    for (std::uint32_t i = 0; i < 3; ++i) {
        cyuediff::MeshInstance instance;
        instance.id = instance.vertex = i;
        mesh->instances.push_back(instance);
    }
    mesh->triangles = {{0, 0, 0, {0, 1, 2}}};
    mesh->material_slots[0] = "Default";
    cyuediff::MeshView mesh_view;
    mesh_view.set_mesh(mesh);
    view.set_mesh_geometry(mesh);
    if (mesh_view.empty())
        return 7;
    {
        auto source_skin = std::make_shared<cyuediff::MeshSkin>();
        source_skin->bones = {{0, -1, "root"}, {2, 0, "arm"}};
        source_skin->profiles["Default"][0] = {{2, 65535}};
        auto before_skin = std::make_shared<cyuediff::MeshGeometry>(*mesh);
        auto after_skin = std::make_shared<cyuediff::MeshGeometry>(*mesh);
        before_skin->skin = source_skin;
        auto other_skin = std::make_shared<cyuediff::MeshSkin>(*source_skin);
        other_skin->profiles["Default"][0][0].raw_weight = 32767;
        after_skin->skin = other_skin;
        mesh_view.set_mesh(before_skin, after_skin);
        if (mesh_view.changes().modified != 2 || mesh_view.changes().unchanged != 1) return 61;
        *other_skin = *source_skin;
        other_skin->bones[0].translation[0] = 2;
        mesh_view.set_mesh(before_skin, after_skin);
        if (mesh_view.changes().modified != 2) return 62; // Weighted child is affected by its parent's pose.
        view.set_mesh_geometry(before_skin);
        view.set_mesh_geometry(after_skin, true);
        auto state = view.view_state();
        state["meshSkinInspection"] = true;
        state["meshSkin"]["filter"] = "arm";
        view.restore_view_state(state);
        if (!view.view_state()["meshSkinInspection"] || view.view_state()["meshSkin"]["filter"] != "arm") return 63;
        view.set_mesh_geometry({});
        view.set_mesh_geometry({}, true);
    }
    auto changed_mesh = std::make_shared<cyuediff::MeshGeometry>(*mesh);
    changed_mesh->vertices[0].position[2] = 0.5;
    mesh_view.set_mesh(mesh, changed_mesh);
    if (mesh_view.changes().modified != 2 || mesh_view.changes().unchanged != 1)
        return 18;
    changed_mesh->instances[0].uvs = {{{0.25, 0.5}}};
    mesh_view.set_mesh(mesh, changed_mesh);
    if (mesh_view.changes().modified != 3)
        return 19;
    mesh_view.set_mesh(mesh, {}, false, true);
    if (mesh_view.changes().removed != 3)
        return 20;
    mesh_view.set_mesh(mesh, changed_mesh);
    view.set_mesh_geometry(changed_mesh, true);
    auto before_graph_change = std::make_shared<cyuediff::Package>(*before);
    before_graph_change->exports[2].native_data["pins"][std::string(32, '1')]["defaultText"] = {
        {"kind", "text-generator"},
        {"generator", "PluginText"},
        {"contents", {{"bytes", 4}, {"sha256", "after"}}}};
    const auto first_graph = before->blueprint_snapshot()["graphs"].begin().value();
    const auto second_graph = before_graph_change->blueprint_snapshot()["graphs"].begin().value();
    if (cyuediff::graph_changes(&first_graph, &second_graph).nodes.at(std::string(32, 'a')) !=
        cyuediff::ChangeKind::modified)
        return 24;
    graph_view.set_package(*before, before_graph_change.get());
    material_view.set_package(*before, before_graph_change.get());
    instance_view.set_package(*before, before_graph_change.get());
    if (!instance_view.select_object("MI"))
        return 21;
    auto image_difference = cyuediff::compare_images({2, 1, {0, 0, 0, 255, 0, 0, 0, 255}},
                                                     {2, 1, {0, 0, 0, 255, 255, 0, 0, 255}});
    if (!view.set_texture_difference("ExampleTexture", "ExampleTexture", {(ImTextureID)43, 2, 1},
                                     image_difference))
        return 22;
    cyuediff::TextureView hdr_view;
    auto hdr_source = texture;
    hdr_source.object_path = "HDR";
    hdr_source.format = "TSF_RGBA32F";
    hdr_source.width = 1;
    hdr_source.height = 1;
    hdr_view.set_source(hdr_source);
    hdr_view.set_preview({(ImTextureID)44, 1, 1});
    auto hdr_difference = cyuediff::compare_hdr_images({1, 1, {65504, 65504, 65504, 1}},
                                                       {1, 1, {65505, 65504, 65504, 1}}, 0, true);
    if (!hdr_view.set_difference({(ImTextureID)45, 1, 1}, hdr_difference))
        return 23;
    auto mesh_unchanged = mesh->to_json();
    int frames = 0;
    const cyuediff::AssetTab tabs[] = {cyuediff::AssetTab::properties, cyuediff::AssetTab::blueprint,
                                       cyuediff::AssetTab::materials,  cyuediff::AssetTab::textures,
                                       cyuediff::AssetTab::meshes,     cyuediff::AssetTab::comparison};
    for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
        cyui::Config config;
        config.themeKey = theme;
        cyui::apply(config);
        cyui::loadFonts();
        unsigned char *pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        for (const auto *language : {"en", "fr"})
            for (int frame = 0; frame < 3; ++frame) {
                cyui::setLocale(language);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Host");
                view.select_tab(tabs[frames % 6]);
                view.draw();
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Graph host");
                graph_view.draw("##graph-test");
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Texture host");
                texture_view.draw();
                hdr_view.draw("##hdr-test");
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Mesh host");
                mesh_view.draw();
                if (frame == 2 && mesh_view.rendered_edges() != 3)
                    return 9;
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Material host");
                material_view.draw();
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Instance host");
                instance_view.draw();
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Addition host");
                addition.select_tab(tabs[frames % 6]);
                addition.draw();
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Removal host");
                removal.select_tab(tabs[frames % 6]);
                removal.draw();
                ImGui::End();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Catalogue host");
                catalog_view.draw();
                ImGui::End();
                ImGui::Begin("Typed properties host");
                property_view.draw();
                ImGui::End();
                cyui::renderNotifications();
                ImGui::Render();
                if (frame == 2 && (!ImGui::GetDrawData() || ImGui::GetDrawData()->CmdListsCount == 0))
                    return 1;
                ++frames;
            }
    }
    if (mesh->to_json() != mesh_unchanged)
        return 8;
    if (before->to_json() != unchanged)
        return 3;
    ImGui::DestroyContext();
    std::cout << frames << " UI frames validated (five themes, English/French)\n";
}
