#include "workspace.hpp"
#include <algorithm>
#include <limits>
namespace cyuediff::desktop {
DocumentWorkspace::DocumentWorkspace(DocumentSession::Loader loader, std::size_t maximum)
    : worker_(std::move(loader)), maximum_(maximum) {
    if (!maximum || maximum > 32)
        throw Error("document-limit", "Document limit must be between 1 and 32");
    create();
}
const WorkspaceDocument *DocumentWorkspace::find(DocumentId id) const {
    const auto found =
        std::find_if(documents_.begin(), documents_.end(), [=](const auto &entry) { return entry.id == id; });
    return found == documents_.end() ? nullptr : &*found;
}
WorkspaceDocument &DocumentWorkspace::require(DocumentId id) {
    auto *entry = find(id);
    if (!entry)
        throw Error("desktop-document", "Unknown document tab");
    return const_cast<WorkspaceDocument &>(*entry);
}
void DocumentWorkspace::clear() {
    worker_.cancel();
    running_ = active_ = 0;
    queue_.clear();
    pending_.clear();
    documents_.clear();
    closed_.clear();
}
DocumentId DocumentWorkspace::create() {
    if (documents_.size() >= maximum_ || next_ == std::numeric_limits<DocumentId>::max())
        throw Error("document-limit", "Too many open documents");
    documents_.push_back({next_++});
    active_ = documents_.back().id;
    return active_;
}
DocumentId DocumentWorkspace::open(LoadRequest request) {
    auto id = create();
    this->request(id, std::move(request));
    return id;
}
bool DocumentWorkspace::select(DocumentId id) {
    if (!find(id))
        return false;
    active_ = id;
    return true;
}
bool DocumentWorkspace::close(DocumentId id) {
    const auto found =
        std::find_if(documents_.begin(), documents_.end(), [=](const auto &entry) { return entry.id == id; });
    if (found == documents_.end())
        return false;
    if (running_ == id) {
        worker_.cancel();
        running_ = 0;
    }
    queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
    pending_.erase(id);
    remember(*found);
    auto index = static_cast<std::size_t>(found - documents_.begin());
    documents_.erase(found);
    if (active_ == id)
        active_ = documents_.empty() ? 0 : documents_[std::min(index, documents_.size() - 1)].id;
    start_next();
    return true;
}
void DocumentWorkspace::remember(const WorkspaceDocument &entry) {
    if (closed_.size() == 8)
        closed_.erase(closed_.begin());
    closed_.push_back({entry.id, entry.request, entry.has_request});
}
bool DocumentWorkspace::close_others(DocumentId id) {
    if (!find(id))
        return false;
    if (running_ && running_ != id) {
        worker_.cancel();
        running_ = 0;
    }
    for (const auto &entry : documents_)
        if (entry.id != id) {
            remember(entry);
            pending_.erase(entry.id);
        }
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [=](auto queued) { return queued != id; }),
                 queue_.end());
    documents_.erase(std::remove_if(documents_.begin(), documents_.end(),
                                    [=](const auto &entry) { return entry.id != id; }),
                     documents_.end());
    active_ = id;
    start_next();
    return true;
}
DocumentId DocumentWorkspace::duplicate(DocumentId id, bool reload) {
    const auto source = require(id);
    const auto created = create();
    auto &entry = require(created);
    entry.request = source.request;
    entry.has_request = source.has_request;
    if (reload && entry.has_request)
        request(created, entry.request);
    return created;
}
DocumentId DocumentWorkspace::reopen(bool reload) {
    if (closed_.empty())
        throw Error("desktop-document", "No closed document to reopen");
    const auto source = closed_.back();
    const auto created = create();
    closed_.pop_back();
    auto &entry = require(created);
    entry.request = source.request;
    entry.has_request = source.has_request;
    if (reload && entry.has_request)
        request(created, entry.request);
    return created;
}
void DocumentWorkspace::request(LoadRequest request) {
    if (!active_)
        create();
    this->request(active_, std::move(request));
}
void DocumentWorkspace::request(DocumentId id, LoadRequest request) {
    auto &entry = require(id);
    if (running_ == id) {
        worker_.cancel();
        running_ = 0;
    }
    queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
    entry.request = request;
    // Retained context is data only; callbacks/cache ownership stay with the active worker request.
    entry.request.control = {};
    entry.request.cache.reset();
    entry.has_request = entry.busy = true;
    entry.cancelled = false;
    entry.error.clear();
    entry.progress = {"Queued document", 0, 0};
    pending_[id] = std::move(request);
    queue_.push_back(id);
    start_next();
}
void DocumentWorkspace::start_next() {
    if (running_ || queue_.empty())
        return;
    auto id = queue_.front();
    queue_.pop_front();
    auto &entry = require(id);
    auto request = std::move(pending_.at(id));
    pending_.erase(id);
    worker_.request(std::move(request));
    running_ = id;
    entry.progress = worker_.progress();
}
void DocumentWorkspace::cancel() {
    if (active_)
        cancel(active_);
}
void DocumentWorkspace::cancel(DocumentId id) {
    auto &entry = require(id);
    if (running_ == id) {
        worker_.cancel();
        running_ = 0;
    }
    queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
    pending_.erase(id);
    entry.busy = false;
    entry.cancelled = true;
    entry.error.clear();
    entry.progress = {};
    start_next();
}
bool DocumentWorkspace::poll() {
    if (!running_)
        return false;
    auto &entry = require(running_);
    entry.progress = worker_.progress();
    if (!worker_.poll())
        return false;
    entry.busy = false;
    entry.error = worker_.error();
    if (auto result = worker_.take_document()) {
        entry.document = std::move(result);
        ++entry.revision;
    }
    running_ = 0;
    start_next();
    return true;
}
bool DocumentWorkspace::busy() const {
    return running_ || !queue_.empty();
}
bool DocumentWorkspace::active_busy() const {
    const auto *entry = find(active_);
    return entry && entry->busy;
}
bool DocumentWorkspace::cancelled() const {
    const auto *entry = find(active_);
    return entry && entry->cancelled;
}
OperationProgress DocumentWorkspace::progress() const {
    const auto *entry = find(active_);
    return entry ? entry->progress : OperationProgress{};
}
const std::string &DocumentWorkspace::error() const {
    static const std::string empty;
    const auto *entry = find(active_);
    return entry ? entry->error : empty;
}
const std::shared_ptr<const Document> &DocumentWorkspace::document() const {
    static const std::shared_ptr<const Document> empty;
    const auto *entry = find(active_);
    return entry ? entry->document : empty;
}
} // namespace cyuediff::desktop
