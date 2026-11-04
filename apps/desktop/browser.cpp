#include "browser.hpp"
namespace cyuediff::desktop {
Json BrowseDocument::report() const {
    Json rows = Json::array();
    for (const auto &asset : assets)
        rows.push_back({{"path", asset.path},
                        {"label", asset.label},
                        {"extension", asset.extension},
                        {"before", asset.before},
                        {"after", asset.after}});
    return {{"before", before.to_json()},
            {"after", request.git ? after.to_json() : Json(nullptr)},
            {"assets", rows},
            {"complete", before.complete && (!request.git || after.complete)}};
}
LoadRequest BrowseDocument::select(std::size_t index, bool after_side, bool compare) const {
    if (index >= assets.size())
        throw Error("browser-selection", "No asset selected");
    LoadRequest result;
    result.project = request.project;
    result.engine_root = request.engine_root;
    result.git = request.git;
    const auto &asset = assets[index];
    if (result.git) {
        result.git->before_revision = before.revision;
        result.git->after_revision = after.revision;
    }
    if (compare) {
        result.before = asset.path;
        if (!result.git)
            throw Error("browser-selection", "Revision comparison requires Git");
    } else {
        if ((after_side && request.git && !asset.after) || (!after_side && !asset.before))
            throw Error("browser-selection", "Asset is absent from the selected side");
        (after_side ? result.after : result.before) = asset.path;
    }
    return result;
}
std::shared_ptr<const BrowseDocument> load_browser(const BrowseRequest &request,
                                                   const std::function<bool()> &cancelled) {
    auto result = std::make_shared<BrowseDocument>();
    result->request = request;
    CatalogOptions options;
    options.include_engine = request.include_engine;
    options.cancelled = cancelled;
    if (request.git) {
#ifdef CYUEDIFF_HAS_GIT
        if (request.git->repository.empty() || request.git->before_revision.empty() ||
            request.git->after_revision.empty())
            throw Error("desktop-git", "Choose a repository and two Git revisions");
        auto repository = GitRepository::open(request.git->repository);
        result->before = repository.catalog(request.git->before_revision, options);
        if (!result->before.cancelled)
            result->after = repository.catalog(request.git->after_revision, options);
        for (const auto *catalog : {&result->before, &result->after})
            for (const auto &asset : catalog->assets)
                result->protected_files.push_back(repository.root() / fs::u8path(asset.path));
#else
        throw Error("desktop-git", "This desktop build does not include the Git adapter");
#endif
    } else {
        if (request.project.empty())
            throw Error("browser-project", "Choose a project before browsing content");
        auto context = ProjectContext::open(request.project, request.engine_root);
        result->before = catalog_assets(context, options);
        for (const auto &message : context.diagnostics)
            if (result->before.diagnostics.size() < 128)
                result->before.diagnostics.push_back(message);
        result->protected_files.push_back(context.project_file);
        for (const auto &mount : context.mounts)
            result->protected_directories.push_back(mount.directory);
        result->protected_directories.push_back(context.project_file.parent_path() / "Plugins");
        if (!context.engine.root.empty())
            result->protected_directories.push_back(context.engine.root / "Engine");
        for (const auto &asset : result->before.assets)
            result->protected_files.push_back(fs::u8path(asset.path));
    }
    result->assets = merge_catalogs(result->before, request.git ? &result->after : nullptr);
    return result;
}
BrowserSession::BrowserSession(Loader loader) : loader_(std::move(loader)) {
    if (!loader_)
        throw Error("browser-loader", "Missing catalogue loader");
    worker_ = std::thread([this] { run(); });
}
BrowserSession::~BrowserSession() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (cancelled_)
            cancelled_->store(true);
        pending_.reset();
    }
    wake_.notify_one();
    worker_.join();
}
void BrowserSession::request(BrowseRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_)
            cancelled_->store(true);
        cancelled_ = std::make_shared<std::atomic_bool>(false);
        pending_ = Work{++generation_, std::move(request), cancelled_};
        result_.reset();
    }
    busy_ = true;
    error_.clear();
    wake_.notify_one();
}
void BrowserSession::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_)
            cancelled_->store(true);
        ++generation_;
        pending_.reset();
        result_.reset();
    }
    busy_ = false;
    error_.clear();
}
bool BrowserSession::poll() {
    std::optional<Result> result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result.swap(result_);
    }
    if (!result || result->id != generation_)
        return false;
    busy_ = false;
    error_ = std::move(result->error);
    if (result->document)
        document_ = std::move(result->document);
    return true;
}
void BrowserSession::run() {
    for (;;) {
        Work work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (stopping_)
                return;
            work = std::move(*pending_);
            pending_.reset();
        }
        Result result{work.id, {}, {}};
        try {
            result.document =
                loader_(work.request, [cancelled = work.cancelled] { return cancelled->load(); });
            if (!result.document)
                throw Error("browser-loader", "Loader returned no catalogue");
        } catch (const Error &e) {
            result.error = e.code + ": " + e.what();
        } catch (const std::exception &e) {
            result.error = e.what();
        } catch (...) {
            result.error = "Catalogue loader failed";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
            if (work.id == generation_)
                result_ = std::move(result);
        }
    }
}
} // namespace cyuediff::desktop
