#pragma once
#include <cyuediff/core.hpp>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace cyuediff::desktop {
struct LoadRequest {
    std::string before, after;
    fs::path project, engine_root, codec;
    bool previews = true;
};
struct Preview {
    std::string object_path;
    bool after = false;
    ImageRgba8 image;
};
struct Document {
    LoadRequest request;
    std::shared_ptr<const Package> before, after;
    std::optional<ProjectContext> project;
    std::optional<DiffResult> diff;
    std::vector<Preview> previews;
    std::shared_ptr<const MeshGeometry> before_mesh, after_mesh;
    std::vector<std::string> diagnostics;
    Json report() const;
};
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
    bool poll();
    bool busy() const {
        return busy_;
    }
    const std::string &error() const {
        return error_;
    }
    const std::shared_ptr<const Document> &document() const {
        return document_;
    }

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
    std::mutex mutex_;
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
