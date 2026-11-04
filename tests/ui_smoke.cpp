#include <cyuediff/asset_view.hpp>
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
    auto unchanged = before->to_json();
    cyuediff::BlueprintView graph_view;
    graph_view.set_package(*before);
    if (graph_view.empty())
        return 2;
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
