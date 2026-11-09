#include "../ui/json_text.hpp"
#include <cyuediff/asset_view.hpp>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool x, const char *message) {
    ++checks;
    if (!x)
        throw std::runtime_error(message);
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
        before->source = "OriginalPropertyBrowserFixture.uasset";
        ObjectExport target;
        target.name = target.path = "Target";
        target.class_name = "DataAsset";
        target.coverage = "properties";
        target.native_coverage = "extension";
        target.properties = {{"items", {{"type", "ArrayProperty"}, {"value", Json::array()}}}};
        for (int i = 0; i < 40001; ++i)
            target.properties["items"]["value"].push_back({{"length", 1.25}, {"particle", i}});
        target.native_data = {{"channels", target.properties.at("items")}};
        before->exports.push_back(std::move(target));
        auto after = std::make_shared<Package>(*before);
        after->exports[0].properties["items"]["value"][40000]["length"] = 2.5;
        after->exports[0].native_data["channels"]["value"][40000]["length"] = 2.5;
        AssetView view;
        view.set_packages(before, after);
        check(view.view_state().at("browseProperties") == true,
              "Large loose objects open paged inspection automatically");
        check(view.set_property_scope("/items/value/40000"),
              "Loose property scopes use owned package values");
        auto state = view.view_state();
        state["propertyBrowser"]["properties"]["selected"] = "/items/value/40000/length";
        view.restore_view_state(state);
        check(view.property_browser().properties().selected_row()->after == "2.5",
              "Loose last-field values stay exact");
        auto inserted = std::make_shared<Package>(*before);
        ObjectExport earlier;
        earlier.name = earlier.path = "Earlier";
        earlier.coverage = "properties";
        inserted->exports.insert(inserted->exports.begin(), earlier);
        AssetView restored;
        restored.set_packages(inserted, after);
        restored.restore_view_state(state);
        check(restored.property_browser().page().scope == "/items/value/40000" &&
                  restored.property_browser().properties().selected_row()->after == "2.5",
              "Scope follows named objects after export reordering");
        check(view.set_property_scope("/channels/value/40000", 0, 1),
              "Native data has an owned browser root");
        auto native_state = view.view_state();
        native_state["propertyBrowser"]["properties"]["selected"] = "/channels/value/40000/length";
        restored.restore_view_state(native_state);
        check(restored.property_browser().properties().selected_row()->after == "2.5" &&
                  restored.view_state().at("browserRoot") == 1,
              "Native scope/root restore together");
        auto partial = std::make_shared<Package>(*after);
        partial->exports[0].coverage = "partial-properties";
        partial->exports[0].properties = Json::object();
        restored.set_packages(before, partial);
        check(restored.set_property_scope("/items/value/40000"),
              "Known source can be inspected with an unread peer");
        for (const auto &row : restored.property_browser().page().table.rows)
            check(row.change == ChangeKind::unchanged, "Partial peers cannot invent removals");
        auto stale = state;
        stale["selected"] = "Missing";
        restored.set_packages(inserted, after);
        restored.restore_view_state(stale);
        check(restored.property_browser().page().scope.empty(),
              "Stale source identity selects no unrelated child");
        auto invalid = state;
        invalid["browserRoot"] = 3;
        const auto retained = restored.view_state();
        try {
            restored.restore_view_state(invalid);
            throw std::runtime_error("Expected invalid root");
        } catch (const Error &e) {
            check(e.code == "view-state" && restored.view_state() == retained,
                  "Invalid root mutates no presentation state");
        }
        int frames = 0;
        for (const auto &theme : cyui::builtInThemes())
            for (const auto *locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme.key.c_str();
                cyui::apply(config);
                cyui::setLocale(locale);
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                for (const auto size : {ImVec2(1280, 800), ImVec2(640, 520)}) {
                    io.DisplaySize = size;
                    for (const auto &scene : {state, native_state}) {
                        view.restore_view_state(scene);
                        for (int i = 0; i < 3; ++i) {
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(size);
                            ImGui::Begin("Loose source browser", nullptr, ImGuiWindowFlags_NoSavedSettings);
                            view.draw("Source", false);
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0 &&
                                      view.property_browser().properties().selected_row()->after == "2.5",
                                  "Source/native browser draws exact values across locales/themes/sizes");
                        }
                    }
                }
            }
        auto limited = std::make_shared<Package>(*after);
        limited->source_read_limit_failed = true;
        const auto previous = view.view_state();
        bool refused = false;
        try {
            view.set_packages(before, limited);
        } catch (const Error &error) {
            refused = error.code == "source-read-limits";
        }
        check(refused && view.view_state() == previous,
              "An unavailable budget peer cannot create property removals or change the existing UI state");
        AssetView single_limited;
        single_limited.set_packages(limited);
        Json escaped = {
            {"name", "caf\u00e9"}, {"quoted", "\"\n"}, {"integer", 9007199254740993ll}, {"real", 1.25}};
        check(detail::presentation_json(escaped) == escaped.dump(2),
              "Bounded display serialization preserves exact ordinary JSON text");
        auto many_lines = Json::array();
        for (std::size_t i = 0; i < detail::presentation_json_lines; ++i)
            many_lines.push_back(0);
        check(detail::presentation_json(many_lines).empty(),
              "Line-limited JSON never becomes a truncated value");
        auto huge = std::make_shared<Package>(*before);
        huge->exports[0].properties["Large"] = {{"type", "StrProperty"},
                                                {"value", std::string(4 * 1024 * 1024, 'x')}};
        AssetView huge_view;
        huge_view.set_packages(huge, huge);
        check(!huge_view.text_comparison_available() &&
                  huge_view.view_state()["browseProperties"].get<bool>(),
              "A large valid package keeps scoped inspection without constructing an oversized text diff");
        ImGui::DestroyContext();
        std::cout << checks << " loose asset browser checks, " << frames << " frames passed\n";
    } catch (const std::exception &e) {
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
