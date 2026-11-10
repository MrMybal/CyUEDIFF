#include <cyuediff/asset_view.hpp>
#include "skeleton_model.hpp"
#include "collision_fixture.hpp"
#include <iostream>
using namespace cyuediff;
namespace {
unsigned checks = 0;
void check(bool v, const char *why) {
    ++checks;
    if (!v)
        throw std::runtime_error(why);
}
Json prop(Json v, const char *t = "IntProperty") { return {{"type", {{"name", t}}}, {"value", v}}; }
Json unknown() {
    return {{"type", {{"name", "IntProperty"}}},
            {"opaque", {{"bytes", 4}, {"sha256", std::string(64, 'a')}}}};
}
Package graph() {
    Package p;
    ObjectExport g;
    g.path = "Graph";
    g.class_name = "EdGraph";
    g.coverage = "properties";
    g.properties = {{"GraphGuid", prop(std::string(32, '1'), "StructProperty")},
                    {"Nodes", prop(Json::array({"Graph.Node"}), "ArrayProperty")}};
    p.exports.push_back(g);
    ObjectExport n;
    n.path = "Graph.Node";
    n.class_name = "K2Node_CallFunction";
    n.outer_index = 1;
    n.coverage = "properties";
    n.native_coverage = "pins";
    n.properties = {{"NodeGuid", prop(std::string(32, '2'), "StructProperty")}, {"NodePosX", prop(1)}};
    n.native_data = {{"pins", Json::object()}};
    p.exports.push_back(n);
    return p;
}
Package material() {
    Package p;
    ObjectExport e;
    e.path = "Material";
    e.class_name = "/Script/Engine.Material";
    e.coverage = "properties";
    e.properties = {{"Expressions", prop(Json::array(), "ArrayProperty")}, {"Roughness", prop(1)}};
    p.exports.push_back(e);
    return p;
}
const PropertyRow &row(const PropertyTable &t, const std::string &path) {
    for (const auto &r : t.rows)
        if (r.path == path)
            return r;
    throw std::runtime_error("Missing property " + path);
}
} // namespace
int main() {
    try {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        auto before = std::make_shared<Package>();
        ObjectExport e;
        e.path = "Asset";
        e.coverage = "properties";
        e.properties = {{"Known", prop(1)}, {"Unavailable", prop(42)}};
        before->exports.push_back(e);
        auto after = std::make_shared<Package>(*before);
        after->exports[0].properties["Known"]["value"] = 2;
        after->exports[0].properties["Unavailable"] = unknown();
        AssetView view;
        view.set_packages(before, after);
        check(!view.text_comparison_available() && view.source_comparison_coverage().size() == 1,
              "Partial source comparison uses scoped known/unknown presentation");
        check(view.set_property_scope(""), "Root scoped properties available");
        auto &table = view.property_browser().page().table;
        check(row(table, "/Known").change == ChangeKind::modified &&
                  row(table, "/Unavailable").change == ChangeKind::unknown &&
                  row(table, "/Unavailable").before == "42" && !row(table, "/Unavailable").after_available,
              "The desktop retains a known sibling change and exact readable peer of an unavailable value");
        auto state = view.view_state();
        AssetView restored;
        restored.set_packages(before, after);
        restored.restore_view_state(state);
        check(restored.source_comparison_coverage() == view.source_comparison_coverage(),
              "Retained navigation rebinds current immutable source coverage");
        check(view.set_property_scope("/Unavailable/value"),
              "Direct unavailable value scope remains navigable");
        check(view.property_browser().page().table.rows.front().change == ChangeKind::unknown &&
                  !view.property_browser().page().table.rows.front().context_only,
              "An absent decoded value stays unknown at a direct scope");
        auto definition_before = std::make_shared<Package>(*before);
        definition_before->exports[0].properties["Unavailable"] =
            prop(Json{{"decoder", "declared/1"}, {"data", {{"counter", 42}}}}, "StructProperty");
        definition_before->decoder_definitions["declared/1"] = std::string(64, 'a');
        auto definition_after = std::make_shared<Package>(*definition_before);
        definition_after->decoder_definitions["declared/1"] = std::string(64, 'b');
        definition_after->exports[0].properties["Known"]["value"] = 2;
        AssetView definition_view;
        definition_view.set_packages(definition_before, definition_after);
        check(!definition_view.text_comparison_available() &&
                  definition_view.source_comparison_coverage()[0]["reason"] ==
                      "native-struct-definition-incompatible",
              "Changed same-ID definitions disable misleading whole-text equality");
        check(definition_view.set_property_scope(""),
              "Definition mismatch keeps sibling properties inspectable");
        check(row(definition_view.property_browser().page().table, "/Known").change == ChangeKind::modified,
              "A decoder definition mismatch does not hide independent known changes");
        check(definition_view.set_property_scope("/Unavailable/value/data"),
              "Readable native values remain available for local inspection");
        const auto &definition_row =
            row(definition_view.property_browser().page().table, "/Unavailable/value/data/counter");
        check(definition_row.change == ChangeKind::unknown && definition_row.before_available &&
                  definition_row.after_available && definition_row.before == "42" &&
                  definition_row.after == "42",
              "Readable equal values have Unknown correspondence under different definitions");
        auto trusted_definition = std::make_shared<Package>(*definition_after);
        trusted_definition->decoder_definitions = definition_before->decoder_definitions;
        trusted_definition->exports[0].properties["Unavailable"]["value"]["data"]["counter"] = 44;
        definition_view.set_packages(definition_before, trusted_definition);
        definition_view.set_property_scope("/Unavailable/value/data");
        check(
            row(definition_view.property_browser().page().table, "/Unavailable/value/data/counter").change ==
                ChangeKind::modified,
            "Matching definitions restore known native field changes");
        definition_view.set_packages(definition_before, definition_after);
        definition_view.set_property_scope("/Unavailable/value/data");
        auto ga = graph(), gb = ga;
        gb.exports[1].properties["NodePosX"]["value"] = 2;
        BlueprintView graph_view;
        graph_view.set_package(ga, &gb);
        check(graph_view.comparison_available() && graph_view.next_changed_node(),
              "Known graph still exposes actual changed nodes");
        gb.exports[1].properties["NodePosX"] = unknown();
        graph_view.set_package(ga, &gb);
        check(!graph_view.comparison_available() && !graph_view.next_changed_node() &&
                  graph_view.select_node(std::string(32, '1'), std::string(32, '2')),
              "Unread graph component remains inspectable without false visual changes");
        auto ma = material(), mb = ma;
        mb.exports[0].properties["Roughness"]["value"] = 2;
        MaterialView materials;
        materials.set_package(ma, &mb);
        check(materials.comparison_available(), "Known material graph remains comparable");
        mb.exports[0].coverage = "opaque";
        mb.exports[0].properties = Json::object();
        materials.set_package(ma, &mb);
        check(!materials.comparison_available() && !materials.empty(),
              "A wholly unread material peer preserves local inspection");
        auto sa = test::skeleton_model(), sb = sa;
        sb.exports[0].native_data = Json::object();
        sb.exports[0].native_coverage = "opaque";
        SkeletonView skeleton;
        skeleton.set_packages(&sa, &sb);
        check(!skeleton.comparison_available() && !skeleton.next_changed_bone() &&
                  skeleton.select_bone("arm"),
              "Native skeleton decoder failure cannot remove every stored bone");
        auto ca = test::collision_fixture(), cb = ca;
        cb.exports[0].properties["AggGeom"] = unknown();
        CollisionView collisions;
        collisions.set_packages(&ca, &cb);
        check(collisions.report()["changes"]["removed"] == 0 &&
                  collisions.report()["changes"]["unknown"].get<unsigned>() > 0,
              "Collision source opacity never colors readable shapes as removed");
        auto mesh = std::make_shared<MeshGeometry>();
        view.set_mesh_geometry(mesh);
        check(!view.mesh_comparison_available(), "A pending geometry peer is not a deleted mesh");
        view.set_mesh_geometry(mesh, true);
        check(view.mesh_comparison_available(), "Prepared geometry pairs become comparable");
        view.set_mesh_geometry(nullptr, true);
        check(!view.mesh_comparison_available(),
              "Failed replacement geometry clears stale comparison readiness");
        unsigned frames = 0;
        for (const auto *language : {"en", "fr"}) {
            cyui::setLocale(language);
            check(cyui::tr("Unavailable") ==
                      std::string(std::string(language) == "fr" ? "Indisponible" : "Unavailable"),
                  "Availability label is translated independently from stored strings");
            unsigned char *pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            for (auto size : {ImVec2(1280, 800), ImVec2(640, 520)})
                for (int scene = 0; scene < 6; ++scene) {
                    io.DisplaySize = size;
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0});
                    ImGui::SetNextWindowSize(size);
                    ImGui::Begin("Source availability", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    switch (scene) {
                    case 0:
                        view.draw("Asset", false);
                        break;
                    case 1:
                        graph_view.draw();
                        break;
                    case 2:
                        materials.draw();
                        break;
                    case 3:
                        skeleton.draw();
                        break;
                    case 4:
                        collisions.draw();
                        break;
                    default:
                        definition_view.draw("Definition", false);
                    }
                    ImGui::End();
                    ImGui::Render();
                    check(ImGui::GetDrawData()->TotalVtxCount > 0,
                          "Partial source views render in both languages and window sizes");
                    ++frames;
                }
        }
        view.set_packages(before, before);
        check(view.text_comparison_available() && view.source_comparison_coverage().empty(),
              "A new readable document clears unknown scopes and restores whole-document text");
        view.set_packages(before);
        check(view.source_comparison_coverage().empty(),
              "Single snapshot inspection has no stale paired source coverage");
        ImGui::DestroyContext();
        std::cout << checks << " source view checks, " << frames << " frames passed\n";
        return 0;
    } catch (const std::exception &e) {
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
