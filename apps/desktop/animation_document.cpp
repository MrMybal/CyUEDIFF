#include "animation_document.hpp"
namespace cyuediff::desktop {
namespace {
void open_skeleton(Document &document, const LoadRequest &request, const ReadOptions &read,
                   const std::string &name, bool after) {
    auto &skeleton = after ? document.after_skeleton : document.before_skeleton;
#ifdef CYUEDIFF_HAS_GIT
    if (document.git) {
        GitOptions options;
        options.control = request.control;
        options.lfs_objects = request.git->lfs_objects;
        auto repository = GitRepository::open(request.git->repository, options);
        const auto &animation = after ? document.git->after : document.git->before;
        auto &revision = after ? document.after_skeleton_revision : document.before_skeleton_revision;
        const auto &context = after ? document.after_git_project : document.before_git_project;
        auto path = repository.root() / fs::u8path(name);
        if (context && name.front() == '/') {
            const auto location = context->locate(name);
            path = location.storage == "git" ? repository.root() / fs::u8path(location.path)
                                             : fs::u8path(location.path);
            path += ".uasset";
        }
        // Protect the selected source even when an absent/corrupt LFS payload prevents materialization.
        document.protected_files.push_back(path);
        for (const auto *extension : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
            auto companion = path;
            companion.replace_extension(extension);
            document.protected_files.push_back(companion);
        }
        revision =
            std::make_shared<const GitRevision>(context ? repository.materialize(*context, name)
                                                        : repository.materialize(animation.commit, name));
        // The revision owns all materialized payloads; a cache entry alone cannot retain them.
        skeleton = std::make_shared<const Package>(revision->open(read));
        return;
    }
#endif
    const auto &context = after && document.after_project ? document.after_project : document.project;
    const auto path = context && name.front() == '/' ? context->resolve(name) : fs::u8path(name);
    document.protected_files.push_back(path);
    for (const auto *extension : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
        auto companion = path;
        companion.replace_extension(extension);
        document.protected_files.push_back(companion);
    }
    skeleton = request.cache ? request.cache->open(path, read)
                             : std::make_shared<const Package>(Package::open(path, read));
}
void prepare_side(Document &document, const LoadRequest &request, const ReadOptions &read, bool after) {
    const auto &input = *request.animation_pose;
    const auto &animation = after ? document.after : document.before;
    if (!animation)
        return;
    const auto &selection = after ? input.after : input.before;
    const auto &name = after ? input.after_skeleton : input.before_skeleton;
    auto &skeleton = after ? document.after_skeleton : document.before_skeleton;
    auto &pose = after ? document.after_animation_pose : document.before_animation_pose;
    try {
        request.control.checkpoint("Opening explicit animation Skeleton", after ? 1 : 0,
                                   document.after ? 2 : 1);
        if (name.empty())
            throw Error("desktop-animation-pose", "Choose an explicit Skeleton for this side");
        open_skeleton(document, request, read, name, after);
        AnimationPoseOptions options;
        options.control = request.control;
        pose = animation_pose(*animation, *skeleton, selection.frame, selection.animation_object,
                              selection.skeleton_object, options);
    } catch (const std::exception &e) {
        const auto *error = dynamic_cast<const Error *>(&e);
        if (error && error->code == "cancelled")
            throw;
        const auto message = std::string(after ? "After: " : "Before: ") +
                             (error ? error->code + ": " : std::string{}) + e.what();
        document.animation_pose_diagnostics.push_back(message);
        document.diagnostics.push_back("Animation pose: " + message);
        // A failed requested side is unknown, rather than an absent asset/addition.
        pose = AnimationPose{};
        pose->animation_object = selection.animation_object;
        pose->skeleton_object = selection.skeleton_object;
        pose->frame = selection.frame;
        pose->display_pose.object_path = selection.skeleton_object;
        pose->display_pose.diagnostics.push_back(message);
    }
}
} // namespace
void prepare_animation_poses(Document &document, const LoadRequest &request, const ReadOptions &read) {
    if (!request.animation_pose)
        return;
    prepare_side(document, request, read, false);
    prepare_side(document, request, read, true);

    if ((document.before_animation_pose && document.after_animation_pose) ||
        (request.git && (document.before_animation_pose || document.after_animation_pose))) {
        try {
            AnimationPoseDiffOptions options;
            options.pose.control = request.control;
            // Only an absent animation package certifies an absent hierarchy. Failed bindings already
            // have an explicit incomplete placeholder and must never use this empty, known side.
            AnimationPose absent;
            absent.hierarchy_complete = true;
            absent.display_pose.complete = true;
            document.animation_pose_comparison = compare_animation_poses(
                document.before_animation_pose ? *document.before_animation_pose : absent,
                document.after_animation_pose ? *document.after_animation_pose : absent, options);
        } catch (const std::exception &e) {
            const auto *error = dynamic_cast<const Error *>(&e);
            if (error && error->code == "cancelled")
                throw;
            document.animation_pose_diagnostics.push_back(e.what());
            document.diagnostics.push_back(std::string("Animation pose: ") + e.what());
            document.animation_pose_comparison = {{"schema", "cyuediff.animation-pose-diff/1"},
                                                  {"complete", false},
                                                  {"status", "incomplete"},
                                                  {"diagnostics", {e.what()}}};
        }
    }
}
Json animation_pose_report(const Document &document) {
    const bool requested = document.request.animation_pose.has_value();
    bool complete = requested && (document.before || document.after);
    if (document.before)
        complete = complete && document.before_animation_pose &&
                   document.before_animation_pose->display_pose.complete;
    if (document.after)
        complete =
            complete && document.after_animation_pose && document.after_animation_pose->display_pose.complete;
    if (!document.animation_pose_comparison.is_null())
        complete = complete && document.animation_pose_comparison.value("complete", false);
    auto source = [](const std::shared_ptr<const Package> &p) -> Json {
        return p ? Json(p->source.u8string()) : Json(nullptr);
    };
    auto binding = [&](bool after) -> Json {
        if (!requested)
            return nullptr;
        const auto &input = *document.request.animation_pose;
        Json result = {{"input", after ? input.after_skeleton : input.before_skeleton},
                       {"storage", "local"},
                       {"commit", nullptr},
                       {"revision", nullptr}};
#ifdef CYUEDIFF_HAS_GIT
        if (document.git) {
            result["storage"] = "git";
            result["commit"] = (after ? document.git->after : document.git->before).commit;
            const auto &revision =
                after ? document.after_skeleton_revision : document.before_skeleton_revision;
            if (revision) {
                result["storage"] = revision->storage;
                result["revision"] = revision->to_json();
            }
        }
#endif
        return result;
    };
    return {
        {"beforeBinding", binding(false)},
        {"afterBinding", binding(true)},
        {"requested", requested},
        {"complete", complete},
        {"status", !requested  ? "disabled"
                   : !complete ? "incomplete"
                   : document.animation_pose_comparison.is_null()
                       ? "ready"
                       : document.animation_pose_comparison.at("status").get<std::string>()},
        {"beforeSkeleton", source(document.before_skeleton)},
        {"afterSkeleton", source(document.after_skeleton)},
        {"before",
         document.before_animation_pose ? document.before_animation_pose->to_json() : Json(nullptr)},
        {"after", document.after_animation_pose ? document.after_animation_pose->to_json() : Json(nullptr)},
        {"comparison", document.animation_pose_comparison},
        {"diagnostics", document.animation_pose_diagnostics}};
}
} // namespace cyuediff::desktop
