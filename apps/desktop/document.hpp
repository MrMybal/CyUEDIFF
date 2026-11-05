#pragma once
#include <cyuediff/cache.hpp>
#include <cyuediff/animation_pose.hpp>
#include <cyuediff/animation_socket_pose.hpp>
#include <cyuediff/hdr.hpp>
#include "container_document.hpp"
#ifdef CYUEDIFF_HAS_GIT
#include <cyuediff/git.hpp>
#endif
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace cyuediff::desktop {
struct GitSource {
    fs::path repository, lfs_objects;
    std::string before_revision = "HEAD~1", after_revision = "HEAD";
    bool discover_engine = true;
};
struct AnimationPoseRequest {
    std::string before_skeleton, after_skeleton;
    AnimationPoseSelection before, after;
};
struct LoadRequest {
    std::string before, after;
    fs::path project, engine_root, codec;
    bool previews = true;
    std::optional<GitSource> git;
    OperationControl control;
    // Empty fields inherit the before-side project/engine selection.
    fs::path after_project, after_engine_root;
    std::shared_ptr<PackageCache> cache;
    std::optional<ContainerSource> container;
    std::optional<AnimationPoseRequest> animation_pose;
};
struct Preview {
    std::string object_path;
    bool after = false;
    ImageRgba8 image;
    std::optional<ImageRgba32f> hdr;
};
struct TextureDifference {
    std::string before_object, after_object;
    ImageComparison comparison;
    std::optional<HdrImageComparison> hdr_comparison;
    std::string diagnostic;
};
struct Document {
    LoadRequest request;
    std::shared_ptr<const ContainerDocument> container;
    std::shared_ptr<const Package> before, after;
    std::shared_ptr<const Package> before_skeleton, after_skeleton;
    std::optional<AnimationPose> before_animation_pose, after_animation_pose;
    std::optional<AnimationSocketPose> before_animation_sockets, after_animation_sockets;
    Json animation_socket_comparison = nullptr;
    std::vector<std::string> animation_socket_diagnostics;
    Json animation_pose_comparison = nullptr;
    std::vector<std::string> animation_pose_diagnostics;
    std::optional<ProjectContext> project, after_project;
    std::optional<DiffResult> diff;
    std::vector<Preview> previews;
    std::vector<TextureDifference> texture_differences;
    std::shared_ptr<const MeshGeometry> before_mesh, after_mesh;
    std::vector<std::string> diagnostics;
    std::vector<fs::path> protected_files, protected_directories;
#ifdef CYUEDIFF_HAS_GIT
    // Own materialized files while packages and previews can still use them.
    std::shared_ptr<const GitComparison> git;
    std::shared_ptr<const GitRevision> before_skeleton_revision, after_skeleton_revision;
    std::optional<GitProjectContext> before_git_project, after_git_project;
#endif
    std::string status() const;
    Json report() const;
};
// Pair equal object paths, or the sole decoded texture on each side.
std::vector<TextureDifference> compare_previews(const std::vector<Preview> &);
std::shared_ptr<const Document> load_document(const LoadRequest &);
// One worker, one pending request. A newer request supersedes results from older requests.
// Only request/poll/accessors on the owner thread; the worker never calls ImGui or the GPU.
class DocumentSession {
  public:
    using Loader = std::function<std::shared_ptr<const Document>(const LoadRequest &)>;
    explicit DocumentSession(Loader loader = load_document);
    ~DocumentSession();
    DocumentSession(const DocumentSession &) = delete;
    DocumentSession &operator=(const DocumentSession &) = delete;
    void request(LoadRequest);
    void cancel();
    bool cancelled() const { return cancelled_; }
    OperationProgress progress() const;
    bool poll();
    bool busy() const { return busy_; }
    const std::string &error() const { return error_; }
    // Owner-thread transfer for a workspace; releases the worker's snapshot ownership.
    std::shared_ptr<const Document> take_document() { return std::move(document_); }
    const std::shared_ptr<const Document> &document() const { return document_; }

  private:
    struct Work {
        std::uint64_t id;
        LoadRequest request;
    };
    struct Result {
        std::uint64_t id;
        std::shared_ptr<const Document> document;
        std::string error;
    };
    Loader loader_;
    std::shared_ptr<PackageCache> cache_ = std::make_shared<PackageCache>();
    mutable std::mutex mutex_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    OperationProgress progress_;
    bool cancelled_ = false;
    std::condition_variable wake_;
    std::optional<Work> pending_;
    std::optional<Result> result_;
    bool stopping_ = false, busy_ = false;
    std::uint64_t generation_ = 0;
    std::shared_ptr<const Document> document_;
    std::string error_;
    std::thread worker_;
    void run();
};
} // namespace cyuediff::desktop
