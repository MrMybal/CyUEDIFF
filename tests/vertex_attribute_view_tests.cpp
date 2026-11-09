#include <cyuediff/vertex_attribute_view.hpp>
#include <cyuediff/mesh_view.hpp>
#include <cyuediff/asset_view.hpp>
#include <cyuediff/view_state.hpp>
#include <cstring>
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
        const auto model = [](unsigned n, bool changed = false) {
            std::vector<std::uint8_t> bytes;
            for (unsigned i = 0; i < n; ++i)
                for (unsigned c = 0; c < 2; ++c) {
                    const float value =
                        c ? (-float(i) / 32.f + (changed && i == 0 ? .5f : 0.f)) : float(i) / 16.f;
                    std::uint32_t bits;
                    std::memcpy(&bits, &value, 4);
                    for (unsigned b = 0; b < 4; ++b)
                        bytes.push_back(static_cast<std::uint8_t>(bits >> (b * 8)));
                }
            auto m = std::make_shared<MeshVertexAttributes>();
            m->channels["CyValue"] = std::make_shared<MeshVertexAttribute>(
                "CyValue", VertexAttributeFormat::float32, 2, std::move(bytes));
            return m;
        };
        auto a = model(3), b = model(3, true);
        const auto original = a->to_json(true);
        VertexAttributeView view;
        view.set_attributes(a, b);
        check(view.report()["comparing"] && view.report()["modified"] == 1 && view.report()["channels"] == 1,
              "Named channel and exact changed vertex inventory");
        check(view.select_vertex(0) && !view.details().rows.empty() && view.details().rows.size() < 20,
              "Focused vector values without flattening unrelated vertices");
        view.set_changed_only(true);
        check(view.visible_count() == 1 && view.next_change() && view.next_change(true),
              "Changed-only vertex navigation wraps");
        auto state = view.view_state();
        VertexAttributeView restored;
        restored.set_attributes(a, b);
        restored.restore_view_state(state);
        check(restored.view_state() == state, "Channel/vertex/filter/property state round trip");
        restored.set_filter("missing");
        check(restored.visible_count() == 0 && restored.report()["selected"].is_null(),
              "Filtered selection clears");
        auto stale = state;
        stale["channel"] = "Missing";
        restored.restore_view_state(stale);
        check(restored.report()["channel"] == "Missing" && restored.visible_count() == 0 &&
                  restored.report()["selected"].is_null(),
              "Missing restored channel keeps identity without retargeting");
        restored.restore_view_state(state);
        auto empty = std::make_shared<MeshVertexAttributes>();
        VertexAttributeView removed;
        removed.set_attributes(a, empty);
        check(removed.report()["removed"] == 3, "Known empty channel scope proves removed values");
        VertexAttributeView unknown;
        unknown.set_attributes(a);
        check(!unknown.report()["comparing"] && unknown.report()["removed"] == 0,
              "Unknown peer remains inspection");
        unknown.set_attributes(a, b, false);
        check(!unknown.report()["comparing"] && unknown.report()["modified"] == 0,
              "Explicit unpaired contexts remain inspection");
        VertexAttributeView after_only;
        after_only.set_attributes({}, b);
        check(after_only.select_vertex(0) && !after_only.details().rows.empty() &&
                  !after_only.report()["comparing"],
              "After-only inspection retains focused values");
        MeshGeometry x;
        x.vertices = {{0, {0, 0, 0}}, {1, {1, 0, 0}}, {2, {0, 1, 0}}};
        for (unsigned i = 0; i < 3; ++i) {
            MeshInstance v;
            v.id = v.vertex = i;
            x.instances.push_back(v);
        }
        x.triangles.push_back({0, 0, 0, {0, 1, 2}});
        x.vertex_attributes = a;
        auto y = x;
        y.vertex_attributes = b;
        MeshView wire;
        wire.set_mesh(std::make_shared<MeshGeometry>(x), std::make_shared<MeshGeometry>(y));
        check(wire.changes().modified == 2 && wire.changes().unchanged == 1,
              "Changed stored attribute marks incident edges");
        y.vertex_attributes.reset();
        wire.set_mesh(std::make_shared<MeshGeometry>(x), std::make_shared<MeshGeometry>(y));
        check(wire.changes().modified == 0, "Unknown peer attribute data does not paint removals");
        try {
            view_state::validate(Json{{"meshSkin", true}, {"meshAttributes", true}}, "container");
            throw std::runtime_error("Conflicting modes accepted");
        } catch (const Error &) {
            check(true, "Conflicting modes refused");
        }
        auto large = model(40000);
        VertexAttributeView focused;
        focused.set_attributes(large);
        check(focused.select_vertex(39999) && focused.details().rows.size() < 20,
              "Last large vertex uses bounded focused property table");
        focused.set_filter("39999");
        check(focused.visible_count() == 1, "Cached vertex/value search selects final row");
        focused.set_filter("");
        focused.select_vertex(39999);
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        VertexAttributeView::register_translations();
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            for (const auto *locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                cyui::setLocale(locale);
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                if (std::string(locale) == "fr")
                    check(cyui::tr("Vertex attributes") == "Attributs des sommets",
                          "French attribute labels");
                for (unsigned i = 0; i < 4; ++i) {
                    io.DisplaySize = {i & 1 ? 560.f : 1280.f, 900};
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0});
                    ImGui::SetNextWindowSize(io.DisplaySize);
                    ImGui::Begin("Attribute view", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    restored.draw();
                    ImGui::End();
                    ImGui::Render();
                    ++frames;
                    check(ImGui::GetDrawData()->TotalVtxCount > 0, "Wide/narrow bilingual attribute frame");
                }
            }
        }
        io.DisplaySize = {1280, 900};
        for (unsigned i = 0; i < 5; ++i) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Large attribute list");
            focused.draw();
            ImGui::End();
            ImGui::Render();
            ++frames;
        }
        check(focused.report()["selectedVisible"] && focused.report()["rendered"] < 100,
              "Clipped last row restored into view");
        check(a->to_json(true) == original, "Inspection does not mutate caller attribute storage");
        ImGui::DestroyContext();
        std::cout << checks << " attribute UI checks, " << frames << " frames\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
