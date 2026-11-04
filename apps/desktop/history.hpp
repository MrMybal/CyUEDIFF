#pragma once
#include "document.hpp"
namespace cyuediff::desktop {
struct HistoryRequest {
    fs::path repository;
    std::string revision = "HEAD";
    GitHistoryOptions options;
};
struct HistoryDocument {
    HistoryRequest request;
    fs::path repository, common_directory;
    GitHistory page;
    GitSource select(const std::string &commit, const GitSource &current, bool after) const;
    Json report() const;
};
std::shared_ptr<const HistoryDocument> load_history(const HistoryRequest &, const std::function<bool()> &);
class HistorySession {
  public:
    using Loader = std::function<std::shared_ptr<const HistoryDocument>(const HistoryRequest &,
                                                                        const std::function<bool()> &)>;
    explicit HistorySession(Loader loader = load_history);
    ~HistorySession();
    HistorySession(const HistorySession &) = delete;
    HistorySession &operator=(const HistorySession &) = delete;
    void request(HistoryRequest);
    void cancel();
    bool poll();
    bool busy() const {
        return busy_;
    }
    const std::string &error() const {
        return error_;
    }
    const std::shared_ptr<const HistoryDocument> &document() const {
        return document_;
    }

  private:
    struct Work {
        std::uint64_t id;
        HistoryRequest request;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    struct Result {
        std::uint64_t id;
        std::shared_ptr<const HistoryDocument> document;
        std::string error;
    };
    Loader loader_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Work> pending_;
    std::optional<Result> result_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    std::uint64_t generation_ = 0;
    bool stopping_ = false, busy_ = false;
    std::string error_;
    std::shared_ptr<const HistoryDocument> document_;
    std::thread worker_;
    void run();
};
} // namespace cyuediff::desktop
