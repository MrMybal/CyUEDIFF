#include "../apps/desktop/container_view.hpp"
#include "container_fixtures.hpp"
#include "schema_fixture.hpp"
#include "schema_native_fixture.hpp"
#include <chrono>
#include <array>
#include <algorithm>
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
        auto changed = payload;
        std::uint32_t header = 0;
        std::memcpy(&header, changed.data.data() + 4, 4);
        changed.data[header] = 'X';
        changed.save(root / "changed.ucas");
        zen_toc(changed).save(root / "changed.utoc");
        source.after_path = root / "changed.utoc";
        source.after_profile = ZenProfile::unreal_5_3;
        auto pair = load_container_document(source, {});
        source.after_profile = ZenProfile::unspecified;
        auto incomplete = load_container_document(source, {});
        source.after_profile = ZenProfile::unreal_5_3;
        source.path = root / "test.pak";
        source.entry = "path:Content/A.bin";
        auto removed = load_container_document(source, {});
        source.entry = "id:000000000000000000000001";
        auto added = load_container_document(source, {});
        source.path = root / "test.utoc";
        source.entry.clear();
        source.after_path = root / "test.utoc";
        source.compare_payloads = true;
        auto same = load_container_document(source, {});
        schema_fixture().save(root / "test.usmap");
        const Json context = {
            {"format", "cyuediff.zen-schema/1"},
            {"profile", "ue5.3"},
            {"usmap", "test.usmap"},
            {"sha256", sha256_file(root / "test.usmap")},
            {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "DemoAsset"}, {"offset", 0}}}}}};
        std::ofstream(root / "schema.json", std::ios::binary) << context.dump();
        auto mapped_payload = schema_zen_fixture();
        mapped_payload.save(root / "mapped.ucas");
        zen_toc(mapped_payload).save(root / "mapped.utoc");
        mapped_payload = schema_zen_fixture(99);
        mapped_payload.save(root / "mapped-after.ucas");
        zen_toc(mapped_payload).save(root / "mapped-after.utoc");
        auto scripts = script_fixture();
        scripts.save(root / "global.ucas");
        zen_toc(scripts, 5).save(root / "global.utoc");
        source = {};
        source.path = root / "mapped.utoc";
        source.entry = "000000000000000000000001";
        source.profile = ZenProfile::unreal_5_3;
        source.schema = root / "schema.json";
        source.script_objects = root / "global.utoc";
        auto mapped = load_container_document(source, {});
        source.after_path = root / "mapped-after.utoc";
        source.after_profile = ZenProfile::unreal_5_3;
        source.after_script_objects = root / "global.utoc";
        source.after_schema = root / "schema.json";
        auto mapped_pair = load_container_document(source, {});
        source.after_schema.clear();
        auto mapped_partial = load_container_document(source, {});
        auto invalid = context;
        invalid["bindings"]["/Script/Test.DemoAsset"]["offset"] = 99999;
        std::ofstream(root / "bad.json", std::ios::binary) << invalid.dump();
        source.schema = root / "bad.json";
        auto mapped_failure = load_container_document(source, {});
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
        view.set_document(incomplete);
        if (view.comparing_objects() || view.visible_object_count() != 2)
            throw std::runtime_error("An undecoded peer must not imply removed objects");
        view.set_document(pair);
        if (view.visible_object_count() != 2 || view.selected_key() != "id:000000000000000000000001")
            throw std::runtime_error("Paired object/member identity");
        view.set_changed_only(true);
        if (view.visible_object_count() != 1 || !view.select_object(0) || view.select_object(1))
            throw std::runtime_error("Only modified object remains visible");
        view.set_document(same);
        if (view.visible_count() != 0)
            throw std::runtime_error("Decoded identical members can be hidden");
        view.set_changed_only(false);
        view.set_document(added);
        if (!view.select_member("id:000000000000000000000001") || !view.selected_member() ||
            view.visible_object_count() != 2)
            throw std::runtime_error("After-only member and cooked objects");
        view.set_document(mapped);
        if (!view.property_report()["available"] || view.property_report()["comparing"] ||
            view.property_report()["mode"] != "properties")
            throw std::runtime_error("Single-side mapped properties inspector");
        view.set_document(mapped_pair);
        const auto &rows = view.property_table().rows;
        if (!view.property_report()["comparing"] ||
            !std::any_of(rows.begin(), rows.end(), [](const PropertyRow &row) {
                return row.path.find("DemoAsset.Health") != std::string::npos && row.before == "42" &&
                       row.after == "99" && row.change == ChangeKind::modified;
            }))
            throw std::runtime_error("Named typed property difference");
        view.set_document(mapped_partial);
        if (view.property_report()["comparing"] || !view.property_report()["available"] ||
            std::any_of(view.property_table().rows.begin(), view.property_table().rows.end(),
                        [](const PropertyRow &row) { return row.change == ChangeKind::removed; }))
            throw std::runtime_error("Unknown peer properties must not be removed values");
        view.set_document(mapped_failure);
        if (view.property_report()["available"] || view.property_report()["beforeStatus"] != "opaque")
            throw std::runtime_error("Failed property regions retain explicit opaque status");
        view.set_document(mapped);
        if (!view.select_tab(4))
            throw std::runtime_error("Schema provenance tab");
        view.set_document(index);
        if (!view.property_table().rows.empty() || view.property_report()["available"] ||
            view.property_report()["visibleProperties"] != 0)
            throw std::runtime_error("An index request clears the previous property inspector");
        view.set_document(mapped_pair);
        const auto original_view = view.view_state();
        auto configured = original_view;
        configured["filter"] = "0000";
        configured["objectFilter"] = "Example";
        configured["values"] = {{"filter", "Health"}, {"changedOnly", false}, {"selected", nullptr}};
        view.restore_view_state(configured);
        const auto saved_view = view.view_state();
        ContainerView restored_view;
        restored_view.set_document(mapped_pair);
        restored_view.restore_view_state(saved_view);
        if (restored_view.view_state() != saved_view)
            throw std::runtime_error("Container presentation state round trip");
        auto stale_view = saved_view;
        stale_view["object"] = "missing";
        stale_view["member"] = "missing";
        restored_view.restore_view_state(stale_view);
        if (!restored_view.view_state()["object"].is_null() ||
            !restored_view.view_state()["member"].is_null() ||
            restored_view.property_report()["available"] != false)
            throw std::runtime_error("Stale container identities cannot select unrelated rows");
        auto invalid_view = saved_view;
        invalid_view["values"]["selected"] = 42;
        const auto retained_view = view.view_state();
        bool invalid_rejected = false;
        try {
            view.restore_view_state(invalid_view);
        } catch (const Error &) {
            invalid_rejected = true;
        }
        if (!invalid_rejected || view.view_state() != retained_view)
            throw std::runtime_error("Container restore validation precedes mutations");
        view.restore_view_state(original_view);
        const auto nativeRoot = root / "native";
        write_native_fixtures(nativeRoot);
        for (const auto &test : std::vector<std::array<std::string, 4>>{
                 {"date-time-i64", "ticks", "9007199254740992", "9007199254740993"},
                 {"timespan-i64", "ticks", "9007199254740992", "9007199254740993"},
                 {"box3-f64", "isValid", "1", "0"},
                 {"sphere-f64", "radius", "1000000000003.125", "1000000000004.125"},
                 {"box-sphere-f64", "sphereRadius", "1000000000006.125", "1000000000007.125"}}) {
            ContainerSource nativeSource;
            nativeSource.path = nativeRoot / (test[0] + ".utoc");
            nativeSource.after_path = nativeRoot / ("changed-" + test[0] + ".utoc");
            nativeSource.entry = "000000000000000000000001";
            nativeSource.profile = nativeSource.after_profile = ZenProfile::unreal_5_5;
            nativeSource.schema = nativeSource.after_schema = nativeRoot / (test[0] + ".json");
            nativeSource.script_objects = nativeSource.after_script_objects = nativeRoot / "global.utoc";
            view.set_document(load_container_document(nativeSource, {}));
            const auto path = "/~1Script~1Test.Collections.Lookup/" + test[1];
            view.restore_view_state({{"values", {{"filter", test[1]}, {"selected", path}}}});
            const auto &rows = view.property_table().rows;
            if (!std::any_of(rows.begin(), rows.end(), [&](const PropertyRow &row) {
                    return row.path == path && row.before == test[2] && row.after == test[3] &&
                           row.change == ChangeKind::modified;
                }))
                throw std::runtime_error("Exact native temporal/bounds desktop preview");
        }
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 900);
        io.DeltaTime = 1.f / 60;
        io.IniFilename = nullptr;
        ContainerView::register_translations();
        cyui::setLocale("fr");
        if (cyui::tr("After container") != "Conteneur apr\u00e8s" ||
            cyui::tr("Hide unchanged") != "Masquer les inchang\u00e9s" ||
            cyui::tr("Decoded bytes") != "Octets d\u00e9cod\u00e9s")
            throw std::runtime_error("UTF-8 French container translations");
        if (cyui::tr("Decoded properties") != "Propri\u00e9t\u00e9s d\u00e9cod\u00e9es" ||
            cyui::tr("Schema context") != "Contexte de sch\u00e9ma" ||
            cyui::tr("Before schema") != "Sch\u00e9ma avant")
            throw std::runtime_error("French mapped property and provenance translations");
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
                for (const auto &scene :
                     std::vector<std::pair<std::shared_ptr<const ContainerDocument>, int>>{
                         {index, 0},
                         {member, 1},
                         {zen, 2},
                         {pair, 2},
                         {removed, 1},
                         {added, 2},
                         {same, 0},
                         {incomplete, 2},
                         {pair, 3},
                         {pair, 0},
                         {pair, 1},
                         {mapped, 2},
                         {mapped_pair, 2},
                         {mapped_partial, 2},
                         {mapped_failure, 2},
                         {mapped_pair, 4}}) {
                    const auto &document = scene.first;
                    view.set_document(document);
                    if (!view.select_tab(scene.second) || view.select_tab(99))
                        throw std::runtime_error("Explicit container tab selection");
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
                    if (view.active_tab() != scene.second)
                        throw std::runtime_error("Requested inspection tab");
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
