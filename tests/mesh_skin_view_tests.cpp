#include <cyuediff/mesh_skin_view.hpp>
#include <cyuediff/asset_view.hpp>
#include <iostream>
using namespace cyuediff;
int main() {
    unsigned checks = 0, frames = 0;
    auto check = [&](bool b, const char *s) {
        ++checks;
        if (!b)
            throw std::runtime_error(s);
    };
    try {
        auto a = std::make_shared<MeshSkin>();
        MeshBone root;
        root.name = "Root";
        a->bones.push_back(root);
        MeshBone child;
        child.id = 1;
        child.parent = 0;
        child.name = "Joint/01";
        child.translation[2] = 2;
        a->bones.push_back(child);
        a->profiles["default"][3] = {{0, 65535}};
        a->profiles["default"][9] = {{1, 65535}};
        auto b = std::make_shared<MeshSkin>(*a);
        b->bones[1].translation[2] = 3;
        b->profiles["default"][9] = {{0, 30000}, {1, 35535}};
        b->profiles["default"][17] = {{1, 65535}};
        MeshSkinView view;
        view.set_skins(a, b, true);
        check(view.report()["bones"] == 2 && view.report()["vertices"] == 3 &&
                  view.report()["modified"] == 2 && view.report()["added"] == 1,
              "Named bone and raw vertex weight identity diff");
        check(view.select_bone("Joint/01") && !view.details().rows.empty(),
              "Slash in a bone name remains an identity");
        check(view.select_vertex(9) && view.details().rows.size() == 4,
              "Focused weight details avoid flattening unrelated vertices");
        auto state = view.view_state();
        view.set_changed_only(true);
        check(view.visible_count() == 3, "Changed skin navigation");
        check(view.next_change() && view.next_change(true), "Changed entry wraparound");
        view.set_filter("35535");
        check(view.visible_count() == 1 && view.select_vertex(9), "Stored weight values searchable");
        auto filtered = view.view_state();
        MeshSkinView restored;
        restored.set_skins(a, b, true);
        restored.restore_view_state(filtered);
        check(restored.view_state() == filtered, "Filter, selection and typed inspector state round trip");
        restored.set_filter("missing");
        check(restored.visible_count() == 0 && !restored.select_bone("Root"),
              "Unavailable filtered selection never silently selects another bone");
        restored.restore_view_state(state);
        check(restored.report()["selected"] == "vertex/9", "Explicit restored vertex identity");
        MeshSkinView unknown;
        unknown.set_skins(a);
        check(!unknown.report()["comparing"] && unknown.report()["removed"] == 0,
              "An unbound peer is an inspection when comparison is unknown");
        MeshSkinView removed;
        removed.set_skins(a, {}, true);
        check(removed.report()["removed"] == 4, "Known absent skin retains removal semantics");
        auto large = std::make_shared<MeshSkin>(*a);
        for (unsigned i = 0; i < 40000; ++i)
            large->profiles["default"][i] = {{0, 65535}};
        MeshSkinView focused;
        focused.set_skins(large);
        check(focused.visible_count() == 40002 && focused.select_vertex(39999) &&
                  focused.details().rows.size() == 3,
              "Large skin list retains last vertex without a global property-row truncation");
        const auto original = a->to_json();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        MeshSkinView::register_translations();
        for (const char *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const char *language : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                cyui::setLocale(language);
                check(std::string(language) != "fr" || cyui::tr("Bones and weights") != "Bones and weights",
                      "French skin labels translated");
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                MeshSkinView display;
                display.set_skins(a, b, true);
                check(display.select_vertex(9), "Test vertex selected");
                for (unsigned i = 0; i < 4; ++i) {
                    io.DisplaySize = {i & 1 ? 560.f : 1280.f, 900};
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0});
                    ImGui::SetNextWindowSize(io.DisplaySize);
                    ImGui::Begin("Skin review", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    display.draw();
                    ImGui::End();
                    ImGui::Render();
                    ++frames;
                    check(ImGui::GetDrawData()->TotalVtxCount > 0, "Bilingual skin frame rendered");
                }
            }
        io.DisplaySize = {1280, 900};
        for (unsigned i = 0; i < 4; ++i) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Large skin review", nullptr, ImGuiWindowFlags_NoSavedSettings);
            focused.draw();
            ImGui::End();
            ImGui::Render();
            ++frames;
        }
        check(focused.report()["selectedVisible"] && focused.report()["renderedRows"] < 100,
              "Restored last vertex scrolls into a clipped large list");
        check(a->to_json() == original, "Skin review leaves inputs immutable");
        ImGui::DestroyContext();
        std::cout << checks << " skin UI checks, " << frames << " frames passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Skin view after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
