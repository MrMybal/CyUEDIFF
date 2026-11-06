#include <cyuediff/asset_view.hpp>
#include <cyuediff/mesh_morphs.hpp>
#include "mesh_morph_fixture.hpp"
#include "../src/mesh.hpp"
#include <iostream>
using namespace cyuediff;
namespace mf = test::morph_fixture;
int main() {
    try {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 720);
        io.DeltaTime = 1.f / 60;
        auto f = mf::fixture();
        auto mesh = std::make_shared<MeshGeometry>(
            detail::decode_mesh_description(f.source.bytes.data, test::skin_fixture::profile(), {}));
        auto other = std::make_shared<MeshGeometry>(*mesh);
        auto morphs = std::make_shared<MeshMorphs>(*mesh->morphs);
        other->morphs = morphs;
        MeshView preview;
        preview.set_mesh(mesh, other);
        if (preview.changes().unchanged != 3)
            return 1;
        morphs->targets.at("Smile").positions.at(0)[0] = 2;
        preview.set_mesh(mesh, other);
        if (preview.changes().modified != 2 || preview.changes().unchanged != 1)
            return 2;
        *morphs = *mesh->morphs;
        morphs->targets.at("Smile").normals.at(1)[1] = .5f;
        preview.set_mesh(mesh, other);
        if (preview.changes().modified != 2 || preview.changes().unchanged != 1 || !preview.focus_changes())
            return 3;
        *morphs = *mesh->morphs;
        morphs->targets.erase("Blink");
        preview.set_mesh(mesh, other);
        if (preview.changes().modified != 3)
            return 4;
        *morphs = *mesh->morphs;
        auto package = std::make_shared<Package>();
        AssetView view;
        view.set_packages(package, package);
        view.set_mesh_geometry(mesh);
        view.set_mesh_geometry(other, true);
        auto state = view.view_state();
        state["meshMorphInspection"] = true;
        state["meshMorphs"] = {
            {"filter", "Smile"}, {"selected", "/targets/Smile/positions/0/0"}, {"changedOnly", true}};
        view.restore_view_state(state);
        const auto kept = view.view_state();
        if (!kept["meshMorphInspection"] || kept["meshMorphs"]["filter"] != "Smile" ||
            !kept["meshMorphs"]["changedOnly"])
            return 5;
        auto invalid = kept;
        invalid["meshMorphInspection"] = "yes";
        try {
            view.restore_view_state(invalid);
            return 6;
        } catch (const Error &e) {
            if (e.code != "view-state")
                throw;
        }
        if (view.view_state() != kept)
            return 7;
        const auto original = mesh->to_json();
        unsigned frames = 0;
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            cyui::Config config;
            config.themeKey = theme;
            cyui::apply(config);
            cyui::loadFonts();
            unsigned char *pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            for (const auto *locale : {"en", "fr"})
                for (unsigned i = 0; i < 3; ++i) {
                    cyui::setLocale(locale);
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos(ImVec2(0, 0));
                    ImGui::SetNextWindowSize(io.DisplaySize);
                    ImGui::Begin("Source morph host");
                    view.select_tab(AssetTab::meshes);
                    view.draw();
                    ImGui::End();
                    ImGui::Render();
                    if (!ImGui::GetDrawData() || ImGui::GetDrawData()->CmdListsCount == 0)
                        return 8;
                    ++frames;
                }
        }
        if (mesh->to_json() != original)
            return 9;
        view.set_packages(package);
        if (view.view_state().contains("meshMorphs"))
            return 10;
        ImGui::DestroyContext();
        std::cout << frames << " morph inspection frames verified (five themes, English/French)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 20;
    }
}
