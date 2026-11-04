#include "../apps/desktop/container_view.hpp"
#include "container_fixtures.hpp"
#include "zen_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
using namespace cyuediff::test;
int main() {
    const auto parent = fs::weakly_canonical(fs::current_path());
    auto root = parent / (".test-tmp-container-ui-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(root))
        return 1;
    auto cleanup = [&] {
        auto resolved = fs::weakly_canonical(root);
        if (resolved.parent_path() == parent &&
            resolved.filename().u8string().rfind(".test-tmp-container-ui-", 0) == 0)
            fs::remove_all(resolved);
    };
    try {
        Bytes pak;
        pak.data = pak_plain();
        pak.save(root / "test.pak");
        auto payload = zen_fixture();
        payload.save(root / "test.ucas");
        zen_toc(payload).save(root / "test.utoc");
        ContainerSource source;
        source.path = root / "test.pak";
        auto index = load_container_document(source, {});
        source.entry = "Content/A.bin";
        auto member = load_container_document(source, {});
        source.path = root / "test.utoc";
        source.entry = "000000000000000000000001";
        source.profile = ZenProfile::unreal_5_3;
        auto zen = load_container_document(source, {});
        ContainerView view;
        view.set_document(index);
        if (view.visible_count() != 3 || !view.select_member("Content/B.bin"))
            throw std::runtime_error("Member selection");
        view.set_filter("A.bin");
        if (view.visible_count() != 1 || view.selected_member())
            throw std::runtime_error("Filtered selection cleared");
        if (view.select_member("missing") || !view.select_member("Content/A.bin"))
            throw std::runtime_error("Visible selection");
        view.set_packages_only(true);
        if (view.visible_count())
            throw std::runtime_error("Pak is not a Zen package");
        view.set_filter("");
        view.set_packages_only(false);
        view.set_document(zen);
        view.set_packages_only(true);
        if (view.visible_count() != 1 || !view.select_object(1) || view.select_object(999))
            throw std::runtime_error("Cooked object selection");
        view.set_object_filter("missing");
        if (view.selected_object() != -1 || view.select_object(0))
            throw std::runtime_error("Filtered object selection");
        view.set_object_filter("");
        view.set_packages_only(false);
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 900);
        io.DeltaTime = 1.f / 60;
        io.IniFilename = nullptr;
        ContainerView::register_translations();
        int frames = 0;
        for (const auto &theme : cyui::builtInThemes())
            for (auto locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme.key.c_str();
                cyui::apply(config);
                cyui::setLocale(locale);
                unsigned char *pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                int scene = 0;
                for (const auto &document : {index, member, zen}) {
                    view.set_document(document);
                    for (int frame = 0; frame < 2; ++frame) {
                        ImGui::NewFrame();
                        ImGui::SetNextWindowSize(ImVec2(1100, 800));
                        ImGui::Begin("Containers");
                        view.draw(frame == 0);
                        ImGui::End();
                        ImGui::Render();
                        if (ImGui::GetDrawData()->TotalVtxCount <= 0)
                            throw std::runtime_error("Container drawing");
                        ++frames;
                    }
                    if (view.active_tab() != scene)
                        throw std::runtime_error("Requested inspection tab");
                    ++scene;
                }
            }
        ImGui::DestroyContext();
        cleanup();
        std::cout << frames << " container UI frames validated (five themes, English/French)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        cleanup();
        return 1;
    }
}
