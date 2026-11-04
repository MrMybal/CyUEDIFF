#include "history.hpp"
#include <algorithm>
namespace cyuediff::desktop {
GitSource HistoryDocument::select(const std::string &commit, const GitSource &current, bool after) const {
    const auto found = std::find_if(page.commits.begin(), page.commits.end(),
                                    [&](const auto &row) { return row.id == commit; });
    if (found == page.commits.end())
        throw Error("history-selection", "Commit is not in this history page");
    auto selected = current;
    selected.repository = repository;
    (after ? selected.after_revision : selected.before_revision) = found->id;
    return selected;
}
Json HistoryDocument::report() const {
    auto result = page.to_json();
    result["repository"] = repository.u8string();
    return result;
}
std::shared_ptr<const HistoryDocument> load_history(const HistoryRequest &request,
                                                    const std::function<bool()> &cancelled) {
    GitOptions options;
    options.control.cancelled = cancelled;
    auto repo = GitRepository::open(request.repository, options);
    auto result = std::make_shared<HistoryDocument>();
    result->request = request;
    result->repository = repo.root();
    result->common_directory = repo.common_directory();
    result->page = repo.history(request.revision, request.options);
    return result;
}
HistorySession::HistorySession(Loader loader) : loader_(std::move(loader)) {
    if (!loader_)
        throw Error("history-loader", "Missing history page loader");
    worker_ = std::thread([this] { run(); });
}
HistorySession::~HistorySession() {
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
void HistorySession::request(HistoryRequest request) {
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
void HistorySession::cancel() {
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
bool HistorySession::poll() {
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
void HistorySession::run() {
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
                throw Error("history-loader", "Loader returned no history page");
        } catch (const Error &e) {
            result.error = e.code + ": " + e.what();
        } catch (const std::exception &e) {
            result.error = e.what();
        } catch (...) {
            result.error = "History loader failed";
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
