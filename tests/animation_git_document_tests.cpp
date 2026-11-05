#include "../apps/desktop/document.hpp"
#include "../src/git_process.hpp"
#include "animation_file_fixture.hpp"
#include "animation_pose_file_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
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
        check(e.code == code, ("Expected " + code + ", got " + e.code).c_str());
        return;
    }
    throw std::runtime_error("Expected error: " + code);
}
void text(const fs::path &path, const std::string &value) {
    std::ofstream file(path, std::ios::binary);
    file << value;
    if (!file)
        throw std::runtime_error("Cannot write fixture");
}
std::string pointer(const fs::path &file, const fs::path &objects) {
    auto oid = sha256_file(file);
    auto size = fs::file_size(file);
    auto target = objects / oid.substr(0, 2) / oid.substr(2, 2) / oid;
    fs::create_directories(target.parent_path());
    fs::copy_file(file, target, fs::copy_options::overwrite_existing);
    text(file, "version https://git-lfs.github.com/spec/v1\noid sha256:" + oid + "\nsize " +
                   std::to_string(size) + "\n");
    return oid;
}
void settle(DocumentSession &session) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (session.busy() && std::chrono::steady_clock::now() < deadline) {
        session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!session.busy(), "Worker completed within deadline");
}
} // namespace
int main(int argc, char **argv) {
    const auto parent = fs::weakly_canonical(fs::current_path());
    const auto root = parent / (".test-tmp-animation-git-" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "Content");
    auto cleanup = [&] {
        auto target = fs::weakly_canonical(root);
        if (target.parent_path() == parent &&
            target.filename().u8string().rfind(".test-tmp-animation-git-", 0) == 0)
            fs::remove_all(target);
    };
    try {
        GitOptions options;
        if (argc == 3 && std::string(argv[1]) == "--git")
            options.executable = fs::u8path(argv[2]);
        options.temporary_root = root;
        detail::GitWorkspace helper(root);
        auto git = [&](const std::vector<std::string> &args) {
            return detail::git_text(options, root, args, helper);
        };
        git({"init", "--quiet"});
        for (const auto &p :
             std::vector<std::pair<std::string, std::string>>{{"user.name", "CyUEDiff tests"},
                                                              {"user.email", "tests@invalid.example"},
                                                              {"core.autocrlf", "false"},
                                                              {"core.hooksPath", "nonexistent-hooks"},
                                                              {"commit.gpgSign", "false"}})
            git({"config", p.first, p.second});
        auto commit = [&](const std::string &message) {
            git({"add", "Content"});
            git({"commit", "--quiet", "-m", message});
            auto id = git({"rev-parse", "HEAD"});
            while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
                id.pop_back();
            return id;
        };
        auto clip = save(root / "Content", "Clip", animation_model_file_fixture(), true);
        auto rig = save(root / "Content", "Rig", animation_skeleton_file_fixture(), true);
        auto exports = rig;
        exports.replace_extension(".uexp");
        const auto objects = root / ".git/lfs/objects";
        auto first_oid = pointer(exports, objects);
        auto before = commit("original hierarchy");
        save(root / "Content", "Rig", animation_skeleton_file_fixture(2), true);
        pointer(exports, objects);
        auto after = commit("reparent hierarchy only");
        // Poison both checkout files; historical packages must never read them.
        text(clip, "invalid working tree animation");
        text(rig, "invalid working tree Skeleton");
        const auto index_hash = sha256_file(root / ".git/index");
        const auto state = git({"status", "--porcelain=v1"});
        LoadRequest request;
        request.before = "Content/Clip.uasset";
        request.git = GitSource{root, {}, before, after, true};
        request.previews = false;
        request.animation_pose = AnimationPoseRequest{"Content/Rig.uasset", "Content/Rig.uasset", {1}, {1}};
        auto document = load_document(request);
        auto report = document->report()["animationPose"];
        check(document->status() == "identical" && report["complete"],
              "Skeleton-only edit does not rewrite animation diff");
        check(report["comparison"]["changedJoints"] == 1,
              "Historical hierarchy edit changes composed child origin");
        check(report["beforeBinding"]["commit"] == before && report["afterBinding"]["commit"] == after,
              "Each binding uses the immutable animation commit");
        check(report["beforeBinding"]["revision"]["files"][1]["lfsOid"] == first_oid,
              "Binding report retains verified historical LFS identity");
        auto materialized = document->before_skeleton->source;
        auto payload = materialized;
        payload.replace_extension(".uexp");
        check(fs::exists(materialized) && fs::exists(payload),
              "Skeleton materializations survive loader return");
        auto held = document;
        document.reset();
        check(fs::exists(payload), "Published snapshot retains revision payloads");
        held.reset();
        check(!fs::exists(payload), "Final snapshot release removes owned Skeleton payloads");
        auto missing = request;
        missing.animation_pose->after_skeleton = "Content/Missing.uasset";
        document = load_document(missing);
        report = document->report()["animationPose"];
        check(!report["complete"] && report["afterBinding"]["revision"]["present"] == false,
              "Missing historical Skeleton stays unavailable with provenance");
        check(report["comparison"]["joints"]["arm~/track"]["kind"] == "unavailable",
              "Failed binding cannot invent removed joints");
        check(document->before_animation_pose->display_pose.complete && document->before && document->after,
              "Partial binding failure preserves package inspection and known pose");
        check(sha256_file(root / ".git/index") == index_hash && git({"status", "--porcelain=v1"}) == state,
              "Historical binding leaves checkout and index untouched");
        document.reset();
        cleanup();
        std::cout << checks << " historical animation document checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        cleanup();
        std::cerr << e.what() << '\n';
        return 1;
    }
}

