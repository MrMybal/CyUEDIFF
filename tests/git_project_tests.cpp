#include "../src/git_process.hpp"
#include "package_fixture.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const Error &e) {
        check(e.code == code, ("Expected " + code + ", got " + e.code + ": " + e.what()).c_str());
        return;
    }
    throw std::runtime_error("Expected " + code);
}
void text(const fs::path &file, const std::string &value) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << value;
    if (!out)
        throw std::runtime_error("Cannot write fixture");
}
bool mounted(const GitProjectContext &context, const std::string &name) {
    return std::any_of(context.mounts.begin(), context.mounts.end(),
                       [&](const auto &m) { return m.name == name; });
}
void make_lfs(const fs::path &file, const fs::path &objects) {
    auto oid = sha256_file(file);
    auto size = fs::file_size(file);
    auto folder = objects / oid.substr(0, 2) / oid.substr(2, 2);
    fs::create_directories(folder);
    fs::copy_file(file, folder / oid, fs::copy_options::overwrite_existing);
    text(file, "version https://git-lfs.github.com/spec/v1\noid sha256:" + oid + "\nsize " +
                   std::to_string(size) + "\n");
}
void engine(const fs::path &root, int minor, int value) {
    text(root / "Engine/Build/Build.version",
         Json{{"MajorVersion", 5}, {"MinorVersion", minor}, {"PatchVersion", 2}, {"Changelist", minor * 100}}
             .dump());
    fs::create_directories(root / "Engine/Content");
    save(root / "Engine/Content", "Shared", fixture(1012, value), true);
    for (const auto *name : {"Foo", "EngineOnly", "CodeOnly"}) {
        auto folder = root / "Engine/Plugins" / name;
        text(folder / (std::string(name) + ".uplugin"),
             R"({"CanContainContent":true,"Modules":[{"Name":"Fixture"},]})");
        fs::create_directories(folder / "Content");
        save(folder / "Content", "Item", fixture(1012, value + 1));
    }
}
} // namespace
int main(int argc, char **argv) {
    auto parent = fs::weakly_canonical(fs::current_path());
    auto root = parent / (".test-tmp-project-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        auto repo_path = root / "repo";
        fs::create_directory(repo_path);
        auto temp = root / "temporary";
        fs::create_directory(temp);
        GitOptions options;
        options.temporary_root = temp;
        if (argc == 3 && std::string(argv[1]) == "--git")
            options.executable = fs::u8path(argv[2]);
        detail::GitWorkspace helper(temp);
        auto git = [&](const std::vector<std::string> &args) {
            return detail::git_text(options, repo_path, args, helper);
        };
        git({"init", "--quiet"});
        git({"config", "user.name", "CyUEDiff"});
        git({"config", "user.email", "tests@invalid.example"});
        git({"config", "core.hooksPath", "no-hooks"});
        git({"config", "commit.gpgSign", "false"});
        git({"config", "core.autocrlf", "false"});
        auto commit = [&](const std::string &message) {
            git({"add", "--all"});
            git({"commit", "--quiet", "-m", message});
            auto id = git({"rev-parse", "HEAD"});
            while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
                id.pop_back();
            return id;
        };
        const std::string project_path = "Games/Game/Game.uproject";
        auto project_file = repo_path / fs::u8path(project_path);
        auto game = project_file.parent_path();
        Json descriptor = {{"FileVersion", 3},
                           {"EngineAssociation", "5.3"},
                           {"AdditionalPluginDirectories", Json::array({"../SharedPlugins"})},
                           {"Plugins", Json::array({{{"Name", "Foo"}, {"Enabled", true}},
                                                    {{"Name", "Off"}, {"Enabled", false}}})}};
        text(project_file, "// historical descriptor comment\n" + descriptor.dump());
        fs::create_directories(game / "Content");
        auto asset = save(game / "Content", "Asset", fixture(1012, 1), true);
        auto exports = asset;
        exports.replace_extension(".uexp");
        auto objects = repo_path / ".git/lfs/objects";
        make_lfs(exports, objects);
        for (const auto *name : {"Foo", "Off", "CodeOnly"}) {
            auto folder = game / "Plugins/Category" / name;
            text(folder / (std::string(name) + ".uplugin"),
                 Json{{"CanContainContent", std::string(name) != "CodeOnly"}}.dump());
            fs::create_directories(folder / "Content");
            save(folder / "Content", "Item", fixture(1012, 7));
        }
        auto foo_descriptor = game / "Plugins/Category/Foo/Foo.uplugin";
        make_lfs(foo_descriptor, objects);
        text(game / "Plugins/Category/Foo/Nested/Hidden.uplugin", R"({"CanContainContent":true})");
        auto shared = repo_path / "Games/SharedPlugins/Common";
        text(shared / "Common.uplugin", R"({"CanContainContent":true})");
        fs::create_directories(shared / "Content");
        save(shared / "Content", "Thing", fixture(1012, 8));
        auto map = save(game / "Content", "World", fixture(1012, 3));
        auto map_target = map;
        map_target.replace_extension(".umap");
        fs::rename(map, map_target);
        auto before = commit("before");
        descriptor["EngineAssociation"] = "5.5";
        descriptor["Plugins"][0]["Enabled"] = false;
        text(project_file, descriptor.dump());
        make_lfs(project_file, objects);
        auto after = commit("after context only");
        auto engine_a = root / "Engine 5.3", engine_b = root / "Engine 5.5";
        engine(engine_a, 3, 10);
        engine(engine_b, 5, 20);
        auto repo = GitRepository::open(repo_path, options);
        GitProjectOptions a_options;
        a_options.engine_root = engine_a;
        GitProjectOptions b_options;
        b_options.engine_root = engine_b;
        auto a = repo.project(before, project_path, a_options),
             b = repo.project(after, project_path, b_options);
        check(a.complete && b.complete, "Complete historical descriptor catalogues");
        check(a.engine_association == "5.3" && b.engine_association == "5.5",
              "Historical engine associations differ");
        check(a.engine.minor == 3 && b.engine.minor == 5, "Independent local engine selections");
        check(a.engine_build["Changelist"] == 300 && b.engine_build["Changelist"] == 500,
              "Engine build metadata retained");
        check(mounted(a, "/Game") && mounted(a, "/Engine") && mounted(a, "/EngineOnly"),
              "Game and selected engine mounts");
        check(mounted(a, "/Foo") && !mounted(b, "/Foo"), "Explicitly disabled plugin excluded per revision");
        check(!mounted(a, "/Off") && !mounted(a, "/CodeOnly") && !mounted(a, "/Hidden"),
              "Disabled, code-only and nested plugins excluded");
        check(mounted(a, "/Common"), "Additional plugin directory inside repository");
        check(a.locate("/Foo/Item").storage == "git", "Project plugin shadows engine plugin");
        check(a.locate("/Game/Asset").path == "Games/Game/Content/Asset", "Virtual Game mapping");
        check(a.locate("/Common/Thing").path == "Games/SharedPlugins/Common/Content/Thing",
              "Additional plugin mapping");
        check(repo.materialize(a, "/Game/Asset").open().exports[0].properties["Value"]["value"] == 1,
              "Virtual package with LFS exports");
        check(repo.materialize(a, "/Foo/Item").open().exports[0].properties["Value"]["value"] == 7,
              "Project plugin package");
        check(repo.materialize(a, "/Game/World").repository_path == "Games/Game/Content/World.umap",
              "Map extension resolution");
        check(repo.materialize(a, "Games/Game/Content/Asset.uasset").present,
              "Repository path remains explicit");
        rejects([&] { a.locate("/Game/../Bad"); }, "package-path");
        rejects([&] { a.locate("/Game/Asset.Object"); }, "package-path");
        rejects([&] { a.locate("/Unknown/Asset"); }, "git-project-mount");
        rejects([&] { b.locate("/Foo/Item"); }, "git-project-mount");
        auto context_diff = compare_git(repo, a, b, "/Game/Asset");
        check(context_diff.diff.status() == "identical" && context_diff.status() == "changed",
              "Context-only change is visible with identical assets");
        check(!context_diff.context_changes.empty() && context_diff.to_json().contains("projectContexts"),
              "Context comparison output");
        check(compare_git(repo, a, a, "/Game/Asset").status() == "identical", "Context self comparison");
        auto engine_revision = repo.materialize(a, "/Engine/Shared");
        check(engine_revision.storage == "engine-local" && engine_revision.files.size() == 2,
              "Local engine provenance and companions");
        check(engine_revision.open().exports[0].properties["Value"]["value"] == 10,
              "Selected engine content");
        check(engine_revision.to_json()["files"][0]["storage"] == "engine-local" &&
                  engine_revision.to_json()["files"][0].contains("sha256"),
              "Engine source hashes recorded");
        save(engine_a / "Engine/Content", "Shared", fixture(1012, 99), true);
        check(engine_revision.open().exports[0].properties["Value"]["value"] == 10,
              "Engine revision copy remains stable after source changes");
        check(compare_git(repo, a, b, "/EngineOnly/Item").status() == "changed",
              "Engine plugin comparison uses each selected installation");
        check(!repo.materialize(a, "/Engine/Missing").present, "Absent local engine package");
        auto frozen_path = engine_revision.asset_path();
        engine_revision = GitRevision{};
        check(!fs::exists(frozen_path), "Engine temporary group cleanup");
        bool lfs_project = false, lfs_plugin = false;
        for (const auto &d : b.documents)
            if (d.file.path == project_path)
                lfs_project = !d.file.lfs_oid.empty();
        for (const auto &d : a.documents)
            if (d.file.path.find("Foo.uplugin") != std::string::npos && !d.file.lfs_oid.empty())
                lfs_plugin = true;
        check(lfs_project && lfs_plugin, "Project and plugin descriptors support local LFS");
        auto index_sha = sha256_file(repo_path / ".git/index");
        text(project_file, "dirty descriptor must not be read");
        text(foo_descriptor, "dirty plugin descriptor");
        auto dirty_sha = sha256_file(project_file);
        check(repo.project(before, project_path, a_options).engine_association == "5.3",
              "Dirty project descriptor ignored");
        check(sha256_file(project_file) == dirty_sha && sha256_file(repo_path / ".git/index") == index_sha,
              "Descriptor checkout and index unchanged");
        GitProjectOptions no_engine;
        no_engine.discover_engine = false;
        auto local_only = repo.project(before, project_path, no_engine);
        check(local_only.complete && local_only.engine.root.empty() && !mounted(local_only, "/Engine"),
              "Explicit descriptor-only context");
        check(local_only.to_json()["pluginActivationEvaluated"] == false,
              "Target/plugin activation is not claimed");
        check(EngineInfo::resolve("5.3", engine_a).minor == 3, "Shared engine resolver API");
        auto limited = no_engine;
        limited.max_plugins = 1;
        rejects([&] { repo.project(before, project_path, limited); }, "git-project-budget");
        limited = no_engine;
        limited.max_descriptor_bytes = 32;
        rejects([&] { repo.project(before, project_path, limited); }, "git-budget");
        limited = no_engine;
        limited.max_tree_entries = 1;
        rejects([&] { repo.project(before, project_path, limited); }, "git-project-budget");
        limited = no_engine;
        limited.max_tree_bytes = 32;
        rejects([&] { repo.project(before, project_path, limited); }, "git-output");
        limited = no_engine;
        limited.max_descriptor_total_bytes = 1;
        rejects([&] { repo.project(before, project_path, limited); }, "git-budget");
        limited = no_engine;
        limited.max_plugins = 0;
        rejects([&] { repo.project(before, project_path, limited); }, "git-project-options");
        rejects([&] { repo.project(before, "Missing.uproject", no_engine); }, "git-project-missing");
        rejects([&] { repo.project(before, "../Game.uproject", no_engine); }, "git-path");
        rejects([&] { repo.project(before, "Games/Game/Game.json", no_engine); }, "git-project-path");
        // Restore descriptors and verify a JSON-only reordering does not create a context diff.
        Json before_descriptor = a.documents[0].descriptor;
        text(foo_descriptor, R"({"CanContainContent":true})");
        auto reordered = before_descriptor;
        std::swap(reordered["Plugins"][0], reordered["Plugins"][1]);
        text(project_file, reordered.dump(2));
        auto reorder_commit = commit("reordered references");
        check(compare_git(repo, local_only, repo.project(reorder_commit, project_path, no_engine),
                          "/Game/Asset")
                      .status() == "identical",
              "Plugin reference order and descriptor whitespace normalized");
        // Unsafe external plugin directories are recorded as unresolved, never read from the checkout.
        auto outside = before_descriptor;
        outside["AdditionalPluginDirectories"] = Json::array({"../../../external"});
        text(project_file, outside.dump());
        auto outside_commit = commit("external plugin root");
        auto incomplete = repo.project(outside_commit, project_path, no_engine);
        check(!incomplete.complete, "External historical plugin search root incomplete");
        check(compare_git(repo, incomplete, incomplete, "/Game/Asset").status() == "incomplete",
              "Incomplete context cannot establish equality");
        text(project_file, before_descriptor.dump());
        text(game / "Plugins/Other/Foo/Foo.uplugin", R"({"CanContainContent":true})");
        auto ambiguous = commit("duplicate plugin");
        auto duplicate = repo.project(ambiguous, project_path, no_engine);
        check(!duplicate.complete && !mounted(duplicate, "/Foo"),
              "Duplicate plugin mounts are not chosen arbitrarily");
        fs::remove(game / "Plugins/Other/Foo/Foo.uplugin");
        text(foo_descriptor, "invalid plugin JSON");
        auto opaque = commit("invalid plugin");
        auto malformed = repo.project(opaque, project_path, no_engine);
        check(!malformed.complete && !mounted(malformed, "/Foo"), "Malformed plugin retained as unavailable");
        text(project_file, "{bad}");
        auto invalid = commit("invalid project");
        rejects([&] { repo.project(invalid, project_path, no_engine); }, "git-project-format");
        text(project_file, before_descriptor.dump() + " trailing");
        auto trailing = commit("trailing JSON");
        rejects([&] { repo.project(trailing, project_path, no_engine); }, "git-project-format");
        text(project_file, before_descriptor.dump());
        text(foo_descriptor, R"({"CanContainContent":"yes"})");
        auto wrong_type = commit("wrong plugin type");
        check(!repo.project(wrong_type, project_path, no_engine).complete,
              "Malformed plugin content flag incomplete");
        text(foo_descriptor, R"({"CanContainContent":true})");
        auto ambiguous_asset = save(game / "Content", "World", fixture(1012, 4));
        auto both = commit("ambiguous extension");
        auto both_context = repo.project(both, project_path, no_engine);
        rejects([&] { repo.materialize(both_context, "/Game/World"); }, "git-project-ambiguous");
        auto bad_root = a_options;
        bad_root.engine_root = root / "missing engine";
        rejects([&] { repo.project(before, project_path, bad_root); }, "io");
        // Read the historical descriptors through a linked worktree and a bare repository.
        auto linked = root / "linked";
        git({"worktree", "add", "--quiet", "--detach", "--no-checkout", linked.u8string(), before});
        check(GitRepository::open(linked, options)
                      .project(before, project_path, no_engine)
                      .locate("/Game/Asset")
                      .storage == "git",
              "Historical context through linked worktree");
        auto bare = root / "bare.git";
        git({"clone", "--quiet", "--bare", repo_path.u8string(), bare.u8string()});
        auto shared_options = options;
        shared_options.lfs_objects = objects;
        check(GitRepository::open(bare, shared_options).project(before, project_path, no_engine).complete,
              "Historical context through bare repository and shared LFS");
        std::cout << checks << " historical project checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << "Historical project test failure: " << e.what()
                  << "\nPreserved fixture: " << root.u8string() << '\n';
        return 1;
    }
    auto absolute = fs::weakly_canonical(root);
    if (absolute.parent_path() == parent &&
        absolute.filename().u8string().rfind(".test-tmp-project-", 0) == 0)
        fs::remove_all(absolute);
}
