#include <cyuediff/render_morph_view.hpp>
#include <cyuediff/mesh_view.hpp>
#include <cyuediff/asset_view.hpp>
#include "compressed_morph_fixture.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    unsigned checks = 0, frames = 0;
    const auto check = [&](bool v, const char *m) {
        ++checks;
        if (!v)
            throw std::runtime_error(m);
    };
    try {
        CompressedMorphOptions o;
        o.profile = MorphCompressionProfile::unreal_5_3_windows;
        const auto bind = [&](const std::vector<std::vector<MorphFixtureDelta>> &t) {
            return std::make_shared<MeshRenderMorphs>(
                std::make_shared<CompressedMorphBuffer>(
                    CompressedMorphBuffer::parse(compressed_morph_fixture(t).data, 50000, o)),
                std::vector<std::string>{"Smile/1", "Empty"});
        };
        std::vector<std::vector<MorphFixtureDelta>> t = {{{0, {8, 0, 0}, {}}, {2, {0, 0, 0}, {0, 16, 0}}},
                                                         {}};
        auto a = bind(t);
        t[0][0].position[0] = 9;
        auto b = bind(t);
        RenderMorphView view;
        view.set_models(a, b, true);
        check(view.select_target("Smile/1") && view.report()["modified"] == 1 &&
                  view.report()["vertices"] == 2,
              "Sparse named target comparison");
        check(view.select_vertex(0) && view.details().rows.size() < 20 &&
                  std::any_of(view.details().rows.begin(), view.details().rows.end(),
                              [](const auto &r) {
                                  return r.path == "/0/position/0" && r.change == ChangeKind::modified;
                              }),
              "Focused decoded position and normal deltas");
        view.set_changed_only(true);
        check(view.visible_count() == 1 && view.next_change() && view.next_change(true),
              "Changed vertex filtering and navigation");
        const auto state = view.view_state();
        RenderMorphView restored;
        restored.set_models(a, b, true);
        restored.restore_view_state(state);
        check(restored.view_state() == state, "Target, sparse vertex, filter and details persist");
        check(view.select_target("Empty") && view.visible_count() == 0 && view.report()["target"] == "Empty",
              "Quantized-empty target remains inspectable");
        view.select_target("Smile/1");
        view.set_filter("missing");
        check(view.visible_count() == 0 && view.report()["selected"].is_null(),
              "Filter clears hidden selection");
        auto unknown = std::make_shared<MeshRenderMorphs>(
            std::make_shared<CompressedMorphBuffer>(
                CompressedMorphBuffer::parse(compressed_morph_fixture(t).data, 50000, o)),
            std::vector<std::string>{}, "Unknown schema");
        RenderMorphView u;
        u.set_models(unknown, b, true);
        check(!u.report()["ready"] && !u.report()["comparing"] && u.report()["removed"] == 0,
              "Unknown binding stays inspection without false removals");
        MeshGeometry ma;
        ma.vertices = {{0, {0, 0, 0}}, {1, {1, 0, 0}}, {2, {0, 1, 0}}};
        for (unsigned i = 0; i < 3; ++i) {
            MeshInstance in;
            in.id = in.vertex = i;
            ma.instances.push_back(in);
        }
        ma.triangles.push_back({0, 0, 0, {0, 1, 2}});
        ma.render_morphs = a;
        auto mb = ma;
        mb.render_morphs = b;
        MeshView wire;
        wire.set_mesh(std::make_shared<MeshGeometry>(ma), std::make_shared<MeshGeometry>(mb));
        check(wire.changes().modified == 2 && wire.changes().unchanged == 1,
              "Changed stored morph vertex highlights incident edges");
        mb.render_morphs = unknown;
        wire.set_mesh(std::make_shared<MeshGeometry>(ma), std::make_shared<MeshGeometry>(mb));
        check(wire.changes().modified == 0, "Unknown peer morphs do not fabricate wire changes");
        std::vector<MorphFixtureDelta> many;
        for (unsigned i = 0; i < 40000; ++i)
            many.push_back({i, {4, 0, 0}, {}});
        RenderMorphView large;
        large.set_models(bind({many, {}}));
        large.select_target("Smile/1");
        check(large.select_vertex(39999) && large.details().rows.size() < 20 && !large.details().rows.empty(),
              "Large sparse list retains focused last vertex");
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        RenderMorphView::register_translations();
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const auto *locale : {"en", "fr"}) {
                cyui::Config c;
                c.themeKey = theme;
                cyui::apply(c);
                cyui::loadFonts();
                cyui::setLocale(locale);
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                check(std::string(locale) != "fr" ||
                          cyui::tr("Render morph targets") != "Render morph targets",
                      "French morph interface translated");
                for (unsigned i = 0; i < 4; ++i) {
                    io.DisplaySize = {i & 1 ? 560.f : 1280.f, 900};
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0});
                    ImGui::SetNextWindowSize(io.DisplaySize);
                    ImGui::Begin("Render morphs", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    restored.draw();
                    ImGui::End();
                    ImGui::Render();
                    ++frames;
                    check(ImGui::GetDrawData()->TotalVtxCount > 0, "Responsive bilingual morph frame");
                }
            }
        io.DisplaySize = {1280, 900};
        for (unsigned i = 0; i < 5; ++i) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Large morph list");
            large.draw();
            ImGui::End();
            ImGui::Render();
            ++frames;
        }
        check(large.report()["selectedVisible"] && large.report()["rendered"] < 100,
              "Clipped list focuses restored last vertex");
        ImGui::DestroyContext();
        std::cout << checks << " render morph UI checks, " << frames << " frames\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
