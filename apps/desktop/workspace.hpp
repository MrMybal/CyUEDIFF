#pragma once
#include "document.hpp"
#include <deque>
namespace cyuediff::desktop {
using DocumentId = std::uint64_t;
struct WorkspaceDocument {
    DocumentId id = 0;
    LoadRequest request;
    std::shared_ptr<const Document> document;
    std::string error;
    OperationProgress progress;
    bool has_request = false, busy = false, cancelled = false;
    std::uint64_t revision = 0;
};
struct ClosedDocument {
    DocumentId id = 0;
    LoadRequest request;
    bool has_request = false;
};
// Owner-thread model: one decoder worker and at most one queued request per document.
// Switching tabs never decodes; successful snapshots and failures belong to their stable ID.
class DocumentWorkspace {
    DocumentSession worker_;
    std::vector<WorkspaceDocument> documents_;
    std::vector<ClosedDocument> closed_;
    std::deque<DocumentId> queue_;
    std::map<DocumentId, LoadRequest> pending_;
    DocumentId active_ = 0, running_ = 0, next_ = 1;
    std::size_t maximum_;
    WorkspaceDocument &require(DocumentId);
    void start_next();
    void remember(const WorkspaceDocument &);

  public:
    explicit DocumentWorkspace(DocumentSession::Loader = load_document, std::size_t maximum = 8);
    void clear();
    DocumentId create();
    DocumentId open(LoadRequest);
    bool select(DocumentId);
    bool close(DocumentId);
    bool close_others(DocumentId);
    DocumentId duplicate(DocumentId, bool reload = true);
    DocumentId reopen(bool reload = true);
    const std::vector<ClosedDocument> &closed() const {
        return closed_;
    }
    void request(LoadRequest);
    void request(DocumentId, LoadRequest);
    void cancel();
    void cancel(DocumentId);
    bool poll();
    bool busy() const;
    bool active_busy() const;
    bool cancelled() const;
    OperationProgress progress() const;
    const std::string &error() const;
    const std::shared_ptr<const Document> &document() const;
    const WorkspaceDocument *find(DocumentId) const;
    const std::vector<WorkspaceDocument> &documents() const {
        return documents_;
    }
    DocumentId active_id() const {
        return active_;
    }
    std::size_t maximum() const {
        return maximum_;
    }
};
} // namespace cyuediff::desktop
