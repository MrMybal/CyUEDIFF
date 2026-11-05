#include <cyuediff/asset_view.hpp>
#include "attachments_model.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0, frames = 0;
    try {
        auto check = [&](bool v, const char *msg) {
            ++checks;
            if (!v)
                throw std::runtime_error(msg);
        };
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 900};
        io.DeltaTime = 1.f / 60;
        AssetView::register_translations();
        auto before = virtual_bones_model(), after = before;
        after.exports[1].properties["RelativeLocation"]["value"][0] = 4.;
        after.exports[0].properties["VirtualBones"]["value"][0]["TargetBoneName"]["value"] = "tip";
        const auto original = before.to_json();
        SkeletonView view;
        view.set_packages(&before, &after);
        view.set_attachments_preview(true);
        auto &attachments = view.attachments_view();
        attachments.set_preview(true);
        check(attachments.visible_count() == 4 && attachments.select_socket("Rig.Grip"),
              "Exact socket/virtual union and socket selection");
        check(attachments.displayed_pose().joints[0].position[0] == 11., "Prepared raw socket origins");
        auto saved = view.view_state();
        saved["attachments"]["inspection"]["properties"]["filter"] = "location";
        saved["attachments"]["inspection"]["properties"]["selected"] = "/location/0";
        saved["attachments"]["camera"] = {
            {"target", {12., 1., 1.}}, {"up", {0., 0., 1.}}, {"yaw", .7}, {"pitch", .35}, {"distance", 20.}};
        view.restore_view_state(saved);
        SkeletonView restored;
        restored.set_packages(&before, &after);
        restored.restore_view_state(saved);
        check(restored.view_state() == saved,
              "Socket selection, typed properties, mode and camera round trip");
        auto bad = saved;
        bad["attachments"]["camera"]["distance"] = -1;
        bool rejected = false;
        try {
            restored.restore_view_state(bad);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && restored.view_state() == saved, "Invalid nested camera rejects atomically");
        bad = saved;
        bad["posePreview"] = true;
        rejected = false;
        try {
            restored.restore_view_state(bad);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && restored.view_state() == saved, "Conflicting view modes reject before mutation");
        attachments.set_filter("VB~/Grip");
        check(attachments.visible_count() == 1 && attachments.select_virtual_bone("name:VB~/Grip"),
              "Escaped virtual identity and name search");
        attachments.set_filter("");
        attachments.set_changed_only(true);
        check(attachments.visible_count() == 2 && attachments.next_change(),
              "Confirmed socket/virtual change navigation");
        attachments.set_changed_only(false);
        check(attachments.select_sources("A~/pose", "") &&
                  attachments.displayed_pose().joints[0].position[0] == 16.,
              "Independent named-before/raw-after preview");
        check(view.view_state()["attachments"]["beforeSource"] == "A~/pose" &&
                  view.view_state()["attachments"]["afterSource"] == "",
              "Explicit raw-after state retained");
        check(!attachments.select_sources("Missing", "") && !attachments.displayed_pose().complete &&
                  attachments.displayed_pose().joints.empty(),
              "Missing source never falls back to raw origins");
        auto unavailable = before;
        unavailable.exports[0].properties.erase("Sockets");
        unavailable.exports[0].properties.erase("VirtualBones");
        unavailable.exports.resize(1);
        SkeletonView unknown;
        unknown.set_packages(&before, &unavailable);
        unknown.set_attachments_preview(true);
        check(unknown.attachments_view().select_socket("Rig.Grip") &&
                  !unknown.attachments_view().selected_known(),
              "Unknown opposite membership never claims deletion");
        SkeletonView added;
        added.set_packages(nullptr, &after, true);
        added.set_attachments_preview(true);
        added.attachments_view().set_preview(true);
        check(added.attachments_view().select_socket("Rig.Grip") && added.attachments_view().selected_known(),
              "Known absent object side keeps additions");
        auto omitted = before;
        omitted.exports[1].properties.erase("RelativeLocation");
        SkeletonView missing;
        missing.set_packages(&omitted);
        missing.set_attachments_preview(true);
        missing.attachments_view().set_preview(true);
        check(!missing.attachments_view().displayed_pose().complete &&
                  !missing.attachments_view().displayed_pose().joints[0].ready,
              "Omitted local origins unavailable");
        auto cyclic = before;
        cyclic.exports[0].properties["VirtualBones"]["value"][0]["SourceBoneName"]["value"] = "VB_tip";
        SkeletonView cycle;
        cycle.set_packages(&before, &cyclic);
        cycle.set_attachments_preview(true);
        cycle.attachments_view().select_virtual_bone("name:VB_tip");
        for (const auto *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const auto *locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                cyui::setLocale(locale);
                for (float width : {1200.f, 600.f})
                    for (int scene = 0; scene < 6; ++scene)
                        for (int frame = 0; frame < 2; ++frame) {
                            io.DisplaySize = {width, 900};
                            ImGui::NewFrame();
                            ImGui::SetNextWindowPos({0, 0});
                            ImGui::SetNextWindowSize(io.DisplaySize);
                            ImGui::Begin("Attachments inspection", nullptr, ImGuiWindowFlags_NoDecoration);
                            if (scene == 0)
                                restored.draw();
                            if (scene == 1)
                                view.draw();
                            if (scene == 2)
                                unknown.draw();
                            if (scene == 3)
                                added.draw();
                            if (scene == 4)
                                missing.draw();
                            if (scene == 5)
                                cycle.draw();
                            ImGui::End();
                            ImGui::Render();
                            ++frames;
                            check(ImGui::GetDrawData()->TotalVtxCount > 0,
                                  "Attachment views render across themes/locales/widths");
                        }
            }
        check(restored.attachments_view().rendered_origins() > 0 && original == before.to_json(),
              "Prepared viewport renders without changing inputs");
        ImGui::DestroyContext();
        std::cout << checks << " attachment view checks passed\n"
                  << frames << " attachment UI frames validated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
