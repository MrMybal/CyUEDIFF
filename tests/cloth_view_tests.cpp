#include <cyuediff/cloth_view.hpp>
#include <cyuediff/mesh_view.hpp>
#include <cyuediff/asset_view.hpp>
#include <iostream>
using namespace cyuediff;
int main() {
    unsigned checks = 0, frames = 0;
    const auto check = [&](bool v, const char *m) {
        ++checks;
        if (!v)
            throw std::runtime_error(m);
    };
    try {
        const auto model = [](unsigned vertices, bool changed = false) {
            auto s = std::make_shared<ClothSection>();
            s->vertices = vertices;
            s->asset_index = s->asset_lod = 0;
            s->asset_guid = {1, 2, 3, 4};
            s->lod_biases.resize(2);
            for (auto &bias : s->lod_biases)
                for (unsigned i = 0; i < vertices; ++i) {
                    ClothMapping m;
                    m.position = {.25f, .5f, .25f, 0};
                    m.normal = m.tangent = {1, 0, 0, 1};
                    m.source_indices = {0, 1, 2, 0};
                    m.weight = 1;
                    bias.push_back(m);
                }
            if (changed)
                s->lod_biases[0][0].position[3] = .5f;
            auto m = std::make_shared<MeshCloth>();
            m->sections.push_back(s);
            m->buffer_stored = true;
            m->index_mapping.push_back({0, 0, vertices});
            for (const auto &bias : s->lod_biases)
                m->buffer.insert(m->buffer.end(), bias.begin(), bias.end());
            m->validate(vertices);
            return m;
        };
        auto a = model(3), b = model(3, true);
        const auto original = a->to_json(true);
        ClothView view;
        view.set_cloths(a, b, true);
        check(view.report()["mappings"] == 2 && view.report()["modified"] == 1,
              "Section/LOD bias inventory and exact changed vertex");
        check(view.select_vertex(0) && !view.details().rows.empty() && view.details().rows.size() < 100,
              "Focused binding and influence values without a global flatten");
        view.set_changed_only(true);
        check(view.visible_count() == 1 && view.next_change() && view.next_change(true),
              "Changed cloth vertex filtering/navigation");
        auto state = view.view_state();
        ClothView restored;
        restored.set_cloths(a, b, true);
        restored.restore_view_state(state);
        check(restored.view_state() == state, "Section/LOD bias/vertex/filter/details round trip");
        check(view.select_mapping(0, 1) && view.visible_count() == 0,
              "Unchanged alternate bias filtered without fake changes");
        view.set_changed_only(false);
        view.set_filter("missing");
        check(view.visible_count() == 0 && view.report()["selected"].is_null(), "Hidden selection clears");
        ClothView removed;
        removed.set_cloths(a, {}, true);
        check(removed.report()["removed"] == 3, "Known absent mapping scope retains removals");
        ClothView unknown;
        unknown.set_cloths(a);
        check(!unknown.report()["comparing"] && unknown.report()["removed"] == 0,
              "Unknown peer is inspection");
        auto stale = state;
        stale["section"] = 99;
        restored.restore_view_state(stale);
        check(restored.report()["section"] == 99 && restored.visible_count() == 0 &&
                  restored.report()["selected"].is_null(),
              "Unavailable restored mapping does not retarget another section");
        restored.restore_view_state(state);
        MeshGeometry x;
        x.vertices = {{0, {0, 0, 0}}, {1, {1, 0, 0}}, {2, {0, 1, 0}}};
        for (unsigned i = 0; i < 3; ++i) {
            MeshInstance v;
            v.id = v.vertex = i;
            x.instances.push_back(v);
        }
        x.triangles.push_back({0, 0, 0, {0, 1, 2}});
        x.cloth = a;
        auto y = x;
        y.cloth = b;
        MeshView wire;
        wire.set_mesh(std::make_shared<MeshGeometry>(x), std::make_shared<MeshGeometry>(y));
        check(wire.changes().modified == 2 && wire.changes().unchanged == 1,
              "Changed mapping vertex marks incident edges");
        auto padding = model(3);
        auto section = std::make_shared<ClothSection>(*padding->sections[0]);
        section->lod_biases[0][0].padding = 123;
        padding->sections[0] = section;
        padding->buffer[0].padding = 123;
        y.cloth = padding;
        wire.set_mesh(std::make_shared<MeshGeometry>(x), std::make_shared<MeshGeometry>(y));
        check(wire.changes().modified == 0, "Alignment bytes do not highlight geometry");
        auto large = model(40000);
        ClothView focused;
        focused.set_cloths(large);
        check(focused.select_vertex(39999) && focused.details().rows.size() < 100,
              "Large list selects last vertex with bounded focused details");
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        ClothView::register_translations();
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const auto *language : {"en", "fr"}) {
                cyui::Config c;
                c.themeKey = theme;
                cyui::apply(c);
                cyui::loadFonts();
                cyui::setLocale(language);
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                check(std::string(language) != "fr" || cyui::tr("Cloth mappings") != "Cloth mappings",
                      "French cloth labels translated");
                for (unsigned i = 0; i < 4; ++i) {
                    io.DisplaySize = {i & 1 ? 560.f : 1280.f, 900};
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0});
                    ImGui::SetNextWindowSize(io.DisplaySize);
                    ImGui::Begin("Cloth view", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    restored.draw();
                    ImGui::End();
                    ImGui::Render();
                    ++frames;
                    check(ImGui::GetDrawData()->TotalVtxCount > 0, "Wide/narrow bilingual cloth frame");
                }
            }
        io.DisplaySize = {1280, 900};
        for (unsigned i = 0; i < 5; ++i) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Large cloth list");
            focused.draw();
            ImGui::End();
            ImGui::Render();
            ++frames;
        }
        check(focused.report()["selectedVisible"] && focused.report()["rendered"] < 100,
              "Clipped large list restores last vertex into view");
        check(a->to_json(true) == original, "UI does not mutate original mappings");
        ImGui::DestroyContext();
        std::cout << checks << " cloth UI checks, " << frames << " frames\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
