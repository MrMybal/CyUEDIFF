#include "../apps/desktop/workspace_file.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::desktop;
namespace {
int checks = 0;
void check(bool v, const char *m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
void write(const fs::path &p, const std::string &s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
    if (!f)
        throw std::runtime_error("write fixture");
}
std::string bytes(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Expected rejection");
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-workspace-file-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const auto path = root / fs::u8path("espace \u00e9.json");
        WorkspaceFile value;
        auto &s = value.documents[0].settings;
        s.before = "Content/Before.uasset";
        s.after = "Content/After.uasset";
        s.project = "Project/Game.uproject";
        s.after_project = "Other/Game.uproject";
        s.engine = "Engines/Before";
        s.after_engine = "Engines/After";
        s.codec = "NEVER_AUTO_LOAD.dll";
        s.previews = false;
        s.include_engine = true;
        SavedDocument git;
        git.settings.source = 1;
        git.settings.repository = "Repo";
        git.settings.lfs_objects = "Objects";
        git.settings.project = "Game.uproject";
        git.settings.before = "Content/A.uasset";
        git.settings.after = "Content/B.uasset";
        git.settings.before_revision = "refs/tags/v1";
        git.settings.after_revision = "HEAD";
        git.settings.discover_engine = false;
        SavedDocument cooked;
        cooked.settings.source = 2;
        cooked.settings.container_path = "Before.utoc";
        cooked.settings.after_container_path = "After.utoc";
        cooked.settings.container_entry = "000000000000000000000001";
        cooked.settings.script_objects = "Before/global.utoc";
        cooked.settings.after_script_objects = "After/global.utoc";
        cooked.settings.container_schema = "Before/schema.json";
        cooked.settings.after_container_schema = "After/schema.json";
        cooked.settings.cooked_profile = 1;
        cooked.settings.after_cooked_profile = 2;
        cooked.settings.compare_container_payloads = true;
        cooked.container_tab = 4;
        cooked.cooked_view = "properties";
        value.documents[0].view_state = {
            {"asset",
             {{"filter", "Health"},
              {"selected", "TestAsset"},
              {"before",
               {{"properties",
                 {{"TestAsset", {{"filter", "Value"}, {"selected", "/Value"}, {"changedOnly", true}}}}}}}}}};
        value.documents.push_back(git);
        value.documents.push_back(cooked);
        value.active = 2;
        check(value.to_json().dump().find("NEVER_AUTO_LOAD") == std::string::npos, "Codec path is excluded");
        save_workspace(path, value);
        auto read = read_workspace(path);
        check(read.active == 2 && read.documents.size() == 3, "Active index and document order round trip");
        check(read.documents[0].settings.before ==
                  fs::absolute("Content/Before.uasset").lexically_normal().u8string(),
              "Loose paths anchored to save cwd");
        check(read.documents[0].settings.after_project == fs::absolute("Other/Game.uproject").u8string() &&
                  read.documents[0].settings.after_engine == fs::absolute("Engines/After").u8string(),
              "Independent after context retained");
        check(!read.documents[0].settings.previews && read.documents[0].settings.include_engine &&
                  read.documents[0].settings.codec.empty(),
              "Display options retained and codec empty");
        check(read.documents[1].settings.before == "Content/A.uasset" &&
                  read.documents[1].settings.project == "Game.uproject" &&
                  read.documents[1].settings.before_revision == "refs/tags/v1" &&
                  !read.documents[1].settings.discover_engine,
              "Git paths and revisions preserve meaning");
        check(read.documents[1].settings.repository == fs::absolute("Repo").u8string() &&
                  read.documents[1].settings.lfs_objects == fs::absolute("Objects").u8string(),
              "Git physical roots anchored");
        check(read.documents[2].settings.container_entry == cooked.settings.container_entry &&
                  read.documents[2].settings.after_cooked_profile == 2 &&
                  read.documents[2].container_tab == 4 && read.documents[2].cooked_view == "properties" &&
                  read.documents[2].settings.compare_container_payloads,
              "Container identities, contexts and view round trip");
        check(read.documents[0].view_state == value.documents[0].view_state, "Presentation data round trip");
        auto &pose_settings = value.documents[0].settings;
        pose_settings.pose_enabled = true;
        pose_settings.pose_independent_frames = true;
        pose_settings.pose_before_skeleton = "Rigs/Before.uasset";
        pose_settings.pose_after_skeleton = "/Game/Rigs/After";
        pose_settings.pose_before_object = "Before.Model";
        pose_settings.pose_after_object = "After.Model";
        pose_settings.pose_before_skeleton_object = "OldRig";
        pose_settings.pose_after_skeleton_object = "NewRig";
        pose_settings.pose_before_frame = 2;
        pose_settings.pose_after_frame = 7;
        auto pose_json = value.to_json();
        auto pose_copy = WorkspaceFile::from_json(pose_json).documents[0].settings;
        const auto pose_request = pose_copy.request().animation_pose;
        check(pose_request && pose_request->before.frame == 2 && pose_request->after.frame == 7 &&
                  pose_request->after.skeleton_object == "NewRig",
              "Pose contexts and independent frames round trip");
        pose_copy.pose_independent_frames = false;
        check(pose_copy.request().animation_pose->after.frame == 2, "Shared source frame request");
        pose_copy.pose_enabled = false;
        check(!pose_copy.request().animation_pose, "Binding disabled independently of saved fields");
        auto legacy_three = pose_json;
        legacy_three["schema"] = "cyuediff.workspace/3";
        for (auto &d : legacy_three["documents"])
            for (const char *key :
                 {"pose_enabled", "pose_independent_frames", "pose_before_frame", "pose_after_frame",
                  "pose_before_skeleton", "pose_after_skeleton", "pose_before_object", "pose_after_object",
                  "pose_before_skeleton_object", "pose_after_skeleton_object"})
                d["settings"].erase(key);
        const auto upgraded = WorkspaceFile::from_json(legacy_three);
        check(!upgraded.documents[0].settings.pose_enabled &&
                  upgraded.to_json()["schema"] == "cyuediff.workspace/4",
              "Actual version three settings upgrade with binding disabled");
        for (const Json bad_frame : std::vector<Json>{-1, 1.5, "2", std::uint64_t(INT32_MAX) + 1}) {
            auto invalid = pose_json;
            invalid["documents"][0]["settings"]["pose_before_frame"] = bad_frame;
            rejects([&] { WorkspaceFile::from_json(invalid); });
        }
        auto invalid_pose = pose_json;
        invalid_pose["documents"][0]["settings"].erase("pose_after_frame");
        rejects([&] { WorkspaceFile::from_json(invalid_pose); });
        pose_settings.pose_enabled = false;
        auto version_two = value.to_json();
        version_two["schema"] = "cyuediff.workspace/2";
        check(WorkspaceFile::from_json(version_two).to_json()["schema"] == "cyuediff.workspace/4",
              "Version two presentation files upgrade on explicit save");
        auto with_layout = value.to_json();
        with_layout["documents"][0]["viewState"]["layout"] = {{"sidebarWidth", 420}, {"project", true}};
        with_layout["documents"][0]["viewState"]["asset"]["layout"] = {{"objectColumns", {1, 3}}};
        check(WorkspaceFile::from_json(with_layout).to_json() == with_layout,
              "Version three layout round trip");
        auto bad_layout = with_layout;
        bad_layout["documents"][0]["viewState"]["layout"]["sidebarWidth"] = -1;
        rejects([&] { WorkspaceFile::from_json(bad_layout); });
        auto legacy = value.to_json();
        legacy["schema"] = "cyuediff.workspace/1";
        for (auto &d : legacy["documents"])
            d.erase("viewState");
        write(path, legacy.dump());
        auto migrated = read_workspace(path);
        check(migrated.documents[0].view_state.empty() &&
                  migrated.to_json()["schema"] == "cyuediff.workspace/4",
              "Version-one inputs migrate without invented view state");
        save_workspace(path, read);
        auto first = bytes(path);
        save_workspace(path, read);
        check(bytes(path) == first, "Repeated save is stable and atomic");
        auto relative = value.to_json();
        relative["documents"][0]["settings"]["before"] = "/Game/Example";
        write(path, relative.dump());
        auto mounted = read_workspace(path);
        check(mounted.documents[0].settings.before == "/Game/Example", "Mounted package stays mounted");
        check(mounted.documents[0].settings.project ==
                  (root / "Project/Game.uproject").lexically_normal().u8string(),
              "Relative stored paths resolve against file parent");
        auto invalid = [&](const std::string &bad) {
            write(path, bad);
            rejects([&] { read_workspace(path); });
            rejects([&] { save_workspace(path, value); });
            check(bytes(path) == bad, "Invalid input preserved on save");
        };
        invalid("");
        invalid("{");
        invalid("[]");
        invalid(std::string(1024 * 1024 + 1, ' '));
        invalid(std::string(26, '[') + "0" + std::string(26, ']'));
        invalid("{\"active\":0," + relative.dump().substr(1));
        auto bad = relative;
        bad["schema"] = "cyuediff.workspace/99";
        invalid(bad.dump());
        bad = relative;
        bad["extra"] = true;
        invalid(bad.dump());
        bad = relative;
        bad["active"] = 3;
        invalid(bad.dump());
        bad = relative;
        bad["active"] = -1;
        invalid(bad.dump());
        bad = relative;
        bad["documents"] = Json::array();
        invalid(bad.dump());
        bad = relative;
        bad["documents"] = std::vector<Json>(9, relative["documents"][0]);
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["view"] = "unknown";
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["settings"]["codec"] = "evil.dll";
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["settings"]["before"] = std::string("abc\0def", 7);
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["settings"]["project"] = "C:relative.uproject";
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["settings"]["previews"] = 1;
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["settings"]["before"] = std::string(32769, 'x');
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["settings"]["source"] = 3;
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["containerTab"] = -2;
        invalid(bad.dump());
        bad = relative;
        bad["active"] = std::numeric_limits<std::uint64_t>::max();
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["settings"]["source"] = -1;
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["settings"]["cooked_profile"] = 3;
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["cookedView"] = "unsupported";
        invalid(bad.dump());
        invalid("{\"bad\":\"" + std::string(1, char(0xff)) + "\"}");
        bad = relative;
        bad["documents"][0]["viewState"] = {{"unexpected", true}};
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["viewState"] = {{"asset", {{"filter", std::string(4097, 'x')}}}};
        invalid(bad.dump());
        bad = relative;
        bad["documents"][0]["viewState"] = {
            {"asset", {{"before", {{"blueprint", {{"views", {{"Graph", {{"zoom", 0}}}}}}}}}}}};
        invalid(bad.dump());
        rejects([&] { read_workspace(root / "missing.json"); });
        check(!fs::exists(root / "missing.json"), "Read does not create missing files");
        auto empty = WorkspaceFile{};
        empty.documents.clear();
        rejects([&] { save_workspace(root / "absent/x.json", empty); });
        check(!fs::exists(root / "absent"), "Invalid save creates no directories");
        auto large = value;
        large.documents.resize(8, cooked);
        for (auto &d : large.documents) {
            d.settings.before = std::string(32768, 'x');
            d.settings.after = d.settings.before;
            d.settings.project = d.settings.before;
            d.settings.after_project = d.settings.before;
            d.settings.container_entry = d.settings.before;
        }
        rejects([&] { save_workspace(root / "large/x.json", large); });
        check(!fs::exists(root / "large"), "Write budget checked before changes");
        rejects([&] { save_workspace(root / "unrelated.txt", value); });
        fs::create_directory(root / "directory.json");
        rejects([&] { save_workspace(root / "directory.json", value); });
        for (const auto &entry : fs::directory_iterator(root))
            check(entry.path().filename().u8string().find(".tmp-") == std::string::npos,
                  "No temporary file leaked");
        fs::remove_all(root);
        std::cout << checks << " workspace file checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
