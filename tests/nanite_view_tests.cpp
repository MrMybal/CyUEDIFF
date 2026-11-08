#include <cyuediff/cooked_mesh.hpp>
#include <cyuediff/nanite_view.hpp>
#include "cooked_nanite_fixture.hpp"
#include <chrono>
#include <thread>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0, frames = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void refused(F run) {
    try {
        run();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid Nanite state accepted");
}
CookedNaniteFixture many(unsigned count, bool changed) {
    auto f = cooked_nanite_fixture();
    auto &b = f.mesh.storage.package;
    b.data.resize(f.native);
    struct Node {
        std::array<int, 4> values{{-1, -1, -1, -1}};
        std::array<bool, 4> leaf{};
    };
    std::vector<Node> nodes;
    std::vector<std::pair<unsigned, unsigned>> targets(count);
    std::function<unsigned(unsigned, unsigned)> build = [&](unsigned first, unsigned n) {
        const auto index = unsigned(nodes.size());
        nodes.push_back({});
        if (n <= 4) {
            for (unsigned c = 0; c < n; ++c) {
                nodes[index].values[c] = int(first + c);
                nodes[index].leaf[c] = true;
                targets[first + c] = {index, c};
            }
        } else {
            unsigned step = (n + 3) / 4;
            for (unsigned c = 0, at = first; at < first + n; ++c, at += step) {
                auto child = build(at, std::min(step, first + n - at));
                nodes[index].values[c] = int(child);
            }
        }
        return index;
    };
    build(0, count);
    std::vector<std::vector<std::uint8_t>> pages;
    for (unsigned p = 0; p < count; ++p) {
        auto page = nanite_fixed_page(false);
        nanite_patch(page, 8, p);
        nanite_patch(page, 12, (targets[p].first << 6) | targets[p].second);
        nanite_patch(page, 20, p ? (p << 3) | 1 : 0);
        if (changed && p + 1 == count)
            nanite_patch(page, 24 + 88, 1);
        pages.push_back(std::move(page));
    }
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(0);
    b.i(0);
    b.i(0);
    b.i(int(pages[0].size()));
    b.data.insert(b.data.end(), pages[0].begin(), pages[0].end());
    b.i(int(count));
    for (unsigned p = 0; p < count; ++p) {
        b.i(p ? int((p - 1) * pages[p].size()) : 0);
        b.i(int(pages[p].size()));
        b.i(int(pages[p].size() - 24));
        b.i(0);
        b.put<std::uint16_t>(0);
        b.put<std::uint8_t>(1);
        b.put<std::uint8_t>(0);
    }
    b.i(int(nodes.size()));
    for (const auto &node : nodes)
        for (unsigned c = 0; c < 4; ++c) {
            for (float v : {0.f, 0.f, 0.f, 3.f, -1.f, -1.f, 0.f})
                b.put(v);
            b.i(0);
            for (float v : {1.f, 1.f, 0.f})
                b.put(v);
            const auto value = node.values[c];
            b.put<std::uint32_t>(value < 0 || node.leaf[c] ? 0xffffffffu : static_cast<unsigned>(value));
            b.put<std::uint32_t>(value < 0      ? 0
                                 : node.leaf[c] ? (value ? ((unsigned(value) << 12) | (1u << 9) | 1u) : 1u)
                                                : 0xffffffffu);
        }
    b.i(1);
    b.i(0);
    b.i(0);
    b.i(0);
    b.i(1);
    b.i(0);
    b.i(1);
    b.i(int(count));
    b.i(int(3 * count));
    b.put<std::uint16_t>(1);
    b.put<std::uint16_t>(0);
    b.i(int(count));
    b.put<std::uint8_t>(0xca);
    f.mesh.storage.bulk.data.clear();
    f.mesh.storage.bulk.zeros(3);
    for (unsigned p = 1; p < count; ++p)
        f.mesh.storage.bulk.data.insert(f.mesh.storage.bulk.data.end(), pages[p].begin(), pages[p].end());
    const std::int64_t size = std::int64_t((count - 1) * pages[0].size()), offset = 3;
    std::memcpy(b.data.data() + f.mesh.storage.bulk_row, &offset, 8);
    std::memcpy(b.data.data() + f.mesh.storage.bulk_row + 16, &size, 8);
    b.patch(int(f.mesh.storage.bulk_row + 24), 0x10501);
    nanite_export_size(f);
    return f;
}
std::shared_ptr<const CookedNaniteResource> load(const fs::path &stem, const CookedNaniteFixture &f,
                                                 bool attributes = true) {
    texture_container(stem, f.mesh.storage);
    CookedMeshOptions o;
    o.profile = CookedMeshProfile::unreal_5_3_windows;
    o.keep_mobile_min_lod_setting = false;
    o.geometry = false;
    o.nanite.profile = NaniteProfile::unreal_5_3_windows;
    o.nanite.attributes = attributes;
    return read_zen_static_mesh(Container::open(fs::path(stem.string() + ".utoc")), 0, mesh_context(), {}, o)
        .nanite;
}
void ready(NaniteView &view) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (view.busy() && std::chrono::steady_clock::now() < end) {
        view.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!view.busy() && view.report()["error"] == "", "Latest Nanite generation prepared");
}
void draw(NaniteView &view, float width = 1200) {
    auto &io = ImGui::GetIO();
    io.DisplaySize = {width, 1000};
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Nanite test", nullptr, ImGuiWindowFlags_NoDecoration);
    view.draw();
    ImGui::End();
    ImGui::Render();
    ++frames;
    check(ImGui::GetDrawData()->TotalVtxCount > 0, "Nanite view emits actual CyUI draw geometry");
}
} // namespace
int main() {
    const auto root =
        fs::temp_directory_path() /
        ("cyuediff-nanite-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto base = load(root / "before", many(5000, false)),
             changed = load(root / "after", many(5000, true));
        auto empty = load(root / "empty", cooked_nanite_fixture(false, false, true));
        auto missing_fixture = cooked_nanite_fixture(true, true);
        missing_fixture.mesh.storage.bulk.data.clear();
        auto missing = load(root / "missing", missing_fixture);
        check(base->page_count() == 5000 && base->page_metadata(4999)["index"] == 4999,
              "Focused native page metadata does not require full catalog");
        refused([&] { base->page_metadata(5000); });
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        unsigned char *pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        NaniteView::register_translations();
        NaniteView view;
        view.set_resources(base, changed, true);
        ready(view);
        check(view.report()["comparison"]["modifiedAttributeTriangles"] == 1 &&
                  view.report()["clusters"] == 5000,
              "Large duplicate surface comparison prepared outside draw");
        for (const auto *locale : {"en", "fr"}) {
            cyui::setLocale(locale);
            for (const char *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                draw(view);
                draw(view);
                check(view.report()["renderedRows"].get<unsigned>() < 100 &&
                          view.report()["preparedEdges"] == 15000,
                      "Large cluster list clips and full wire is prepared once");
            }
        }
        check(view.next_change(), "Navigate to actual changed cluster");
        ready(view);
        draw(view);
        draw(view);
        auto report = view.report();
        check(report["state"]["page"] == 4999 && report["paired"] == true &&
                  report["selectedVisible"] == true,
              "Last clipped cluster scrolls into view with aligned corner inspector");
        check(report["details"]["corners"][0]["attributes"]["color"][0] == 0 &&
                  report["peerDetails"]["corners"][0]["attributes"]["color"][0] == 1,
              "Typed native before/after color values retained");
        view.set_cluster_preview(true);
        ready(view);
        draw(view);
        check(view.report()["sceneTriangles"] == 1 && view.report()["preparedEdges"] == 3,
              "Selected cluster scope is distinct from full leaf surface");
        view.set_inspector(3);
        view.select_node(0);
        ready(view);
        check(view.report()["details"]["children"].size() == 4,
              "Stored native hierarchy has focused typed inspection");
        view.set_inspector(1);
        view.select_vertex(2);
        ready(view);
        check(view.report()["details"]["position"] == Json({-2, 0, 0}),
              "Focused native vertex and attributes");
        auto saved = view.view_state();
        saved["layout"] = {{"columns", {.7, 1.3}}};
        saved["compactSurface"] = false;
        view.restore_view_state(saved);
        ready(view);
        draw(view, 500);
        check(view.view_state()["layout"] == saved["layout"],
              "Responsive panel choice and column proportions persist");
        NaniteView restored;
        restored.set_resources(base, changed, true);
        restored.restore_view_state(saved);
        ready(restored);
        check(restored.view_state() == saved,
              "Selection/camera/inspector/filter state round trip after binding");
        for (auto state : {Json{{"page", 16384}}, Json{{"cluster", 256}}, Json{{"vertex", 256}},
                           Json{{"triangle", 128}}, Json{{"node", 65536}}, Json{{"inspector", 4}},
                           Json{{"after", 1}}, Json{{"layout", {{"columns", {0, 1}}}}}})
            refused([&] { view.restore_view_state(state); });
        for (unsigned i = 0; i < 50; ++i)
            view.select_vertex(i % 3);
        ready(view);
        check(view.report()["state"]["vertex"] == 1,
              "Obsolete worker generations do not replace latest selection");
        auto opaque = load(root / "opaque", many(5000, false), false);
        view.set_resources(base, opaque, true);
        view.set_inspector(2);
        ready(view);
        const auto unknown = view.report();
        check(unknown["paired"] == true && !unknown["comparison"]["complete"].get<bool>() &&
                  unknown["inspectorRows"]["added"] == 0 && unknown["inspectorRows"]["removed"] == 0 &&
                  unknown["inspectorRows"]["modified"] == 0,
              "Unrequested peer channels cannot become inspector additions or removals");
        check(unknown["details"]["corners"][0]["attributes"].contains("color") &&
                  unknown["peerDetails"]["corners"][0]["attributes"]["ready"] == false,
              "Raw independent query evidence survives display masking");
        view.set_resources(base, missing, true);
        ready(view);
        check(view.report()["comparison"]["complete"] == false &&
                  view.report()["comparison"].value("removedTriangles", 0) == 0,
              "Unavailable streaming peer cannot become removed geometry");
        draw(view);
        view.set_resources(base, empty, true);
        ready(view);
        check(view.report()["comparison"]["removedTriangles"] == 5000,
              "Verified empty peer establishes full surface removal");
        view.set_after(true);
        ready(view);
        draw(view);
        check(view.report()["geometryKnown"] == true && view.report()["sceneTriangles"] == 0,
              "Known empty selection has no phantom edges");
        view.set_resources(nullptr, changed, true);
        ready(view);
        draw(view);
        check(view.report()["comparison"]["complete"] == false, "Missing resource remains unclassified");
        ImGui::DestroyContext();
        check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
              "Temporary cleanup confined to fixture root");
        fs::remove_all(root);
        std::cout << checks << " Nanite UI checks / " << frames << " CyUI frames\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << " fixtures " << root << '\n';
        return 1;
    }
}
