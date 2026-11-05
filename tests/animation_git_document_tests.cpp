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
        for (const char *side : {"One", "Two"}) {
            fs::create_directories(root / side / "Plugins/RigPack/Content");
            fs::create_directories(root / side / "Content");
            text(root / side / "Game.uproject", R"({"FileVersion":3})");
            text(root / side / "Plugins/RigPack/RigPack.uplugin", R"({"CanContainContent":true})");
            save(root / side / "Content", "Clip", animation_model_file_fixture(), true);
        }
        save(root / "One/Plugins/RigPack/Content", "Rig", animation_skeleton_file_fixture(), true);
        save(root / "Two/Plugins/RigPack/Content", "Rig", animation_skeleton_file_fixture(2), true);
        auto commit = [&](const std::string &message) {
            git({"add", "Content", "One", "Two"});
            git({"commit", "--quiet", "-m", message});
            auto id = git({"rev-parse", "HEAD"});
            while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
                id.pop_back();
            return id;
        };
        const auto empty = commit("no main animation");
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
        auto addition = request;
        addition.git->before_revision = empty;
        auto added = load_document(addition);
        check(!added->before_animation_pose && added->after_animation_pose &&
                  added->report()["animationPose"]["complete"] &&
                  added->animation_pose_comparison["joints"]["root"]["kind"] == "added",
              "Added animation evaluates only its existing side");
        auto removal = request;
        removal.git->after_revision = empty;
        auto removed = load_document(removal);
        check(removed->before_animation_pose && !removed->after_animation_pose &&
                  removed->report()["animationPose"]["complete"] &&
                  removed->animation_pose_comparison["joints"]["root"]["kind"] == "removed",
              "Removed animation evaluates only its existing side");
        git({"update-ref", "refs/heads/selected-pose", before});
        auto moving = request;
        moving.git->before_revision = "selected-pose";
        bool moved = false;
        moving.control.progress = [&](const OperationProgress &p) {
            if (!moved && p.phase == "Opening explicit animation Skeleton") {
                moved = true;
                git({"update-ref", "refs/heads/selected-pose", after});
            }
        };
        auto pinned = load_document(moving);
        check(moved && pinned->before_skeleton_revision->commit == before &&
                  pinned->animation_pose_comparison["changedJoints"] == 1,
              "Branch movement after animation load cannot change Skeleton commit");
        added.reset();
        removed.reset();
        pinned.reset();
        auto mounted = request;
        mounted.project = "One/Game.uproject";
        mounted.after_project = "Two/Game.uproject";
        mounted.git->discover_engine = false;
        mounted.before = mounted.after = "/Game/Clip";
        mounted.animation_pose->before_skeleton = mounted.animation_pose->after_skeleton = "/RigPack/Rig";
        // Invalid checkout descriptors must not affect historical mounts.
        text(root / "One/Game.uproject", "broken checkout descriptor");
        text(root / "Two/Plugins/RigPack/RigPack.uplugin", "broken checkout plugin");
        const auto mounted_state = git({"status", "--porcelain=v1"});
        auto contextual = load_document(mounted);
        auto contextual_report = contextual->report()["animationPose"];
        check(contextual_report["complete"] && contextual_report["comparison"]["changedJoints"] == 1,
              "Independent historical project/plugin contexts bind source hierarchies");
        check(contextual_report["beforeBinding"]["revision"]["path"] ==
                      "One/Plugins/RigPack/Content/Rig.uasset" &&
                  contextual_report["afterBinding"]["revision"]["path"] ==
                      "Two/Plugins/RigPack/Content/Rig.uasset",
              "Each mounted Skeleton resolves through its own historical project");
        check(std::find(contextual->protected_files.begin(), contextual->protected_files.end(),
                        root / "Two/Plugins/RigPack/Content/Rig.uexp") != contextual->protected_files.end(),
              "Historical mounted Skeleton companions are protected");
        check(git({"status", "--porcelain=v1"}) == mounted_state,
              "Historical contexts never repair checkout files");
        text(root / "One/Game.uproject", R"({"FileVersion":3})");
        text(root / "Two/Plugins/RigPack/RigPack.uplugin", R"({"CanContainContent":true})");
        auto engine_binding = mounted;
        engine_binding.git->discover_engine = true;
        for (int index : {0, 1}) {
            const auto installation = root / ".git/fixture-engines" / (index ? "EngineB" : "EngineA");
            fs::create_directories(installation / "Engine/Build");
            fs::create_directories(installation / "Engine/Content");
            text(installation / "Engine/Build/Build.version",
                 R"({"MajorVersion":5,"MinorVersion":5,"PatchVersion":0})");
            save(installation / "Engine/Content", "Rig", animation_skeleton_file_fixture(index ? 2 : 0),
                 true);
            (index ? engine_binding.after_engine_root : engine_binding.engine_root) = installation;
        }
        engine_binding.animation_pose->before_skeleton = engine_binding.animation_pose->after_skeleton =
            "/Engine/Rig";
        auto engine_document = load_document(engine_binding);
        auto engine_report = engine_document->report()["animationPose"];
        check(engine_report["complete"] && engine_report["comparison"]["changedJoints"] == 1,
              "Each historical project uses its explicitly selected local engine Skeleton");
        check(engine_report["beforeBinding"]["storage"] == "engine-local" &&
                  engine_report["afterBinding"]["revision"]["files"][0]["storage"] == "engine-local",
              "Engine Skeleton provenance cannot claim historical Git engine bytes");
        check(std::find(engine_document->protected_directories.begin(),
                        engine_document->protected_directories.end(),
                        root / ".git/fixture-engines/EngineB/Engine") !=
                  engine_document->protected_directories.end(),
              "Selected after engine installation is protected from outputs");
        engine_document.reset();
        // Losing one local LFS object must only disable that side's pose.
        const auto object = objects / first_oid.substr(0, 2) / first_oid.substr(2, 2) / first_oid;
        const auto parked = root / "parked-lfs-object";
        fs::rename(object, parked);
        auto unavailable = load_document(request);
        auto unavailable_report = unavailable->report()["animationPose"];
        check(!unavailable_report["complete"] && unavailable->after_animation_pose->display_pose.complete &&
                  unavailable_report["beforeBinding"]["commit"] == before,
              "Missing LFS Skeleton object retains pinned intent and known opposite pose");
        check(std::find(unavailable->protected_files.begin(), unavailable->protected_files.end(), exports) !=
                  unavailable->protected_files.end(),
              "LFS failure still protects Skeleton source companions");
        fs::rename(parked, object);
        auto invalid_path = request;
        invalid_path.animation_pose->before_skeleton = "../Rig.uasset";
        check(!load_document(invalid_path)->before_animation_pose->display_pose.complete,
              "Repository traversal cannot fall back to local Skeleton files");
        bool cancelled = false, cancel = false;
        auto interrupted = request;
        interrupted.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Opening explicit animation Skeleton")
                cancel = true;
        };
        interrupted.control.cancelled = [&] { return cancel; };
        try {
            load_document(interrupted);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Historical Skeleton materialization propagates cancellation");
        contextual.reset();
        unavailable.reset();
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
