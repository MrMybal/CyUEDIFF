#pragma once
#include "document.hpp"
#include <cyuediff/catalog.hpp>
#include <atomic>
namespace cyuediff::desktop {
struct BrowseRequest {
    fs::path project, engine_root;
    bool include_engine = false;
    std::optional<GitSource> git;
};
struct BrowseDocument {
    BrowseRequest request;
    AssetCatalog before, after;
    std::vector<CatalogItem> assets;
    std::vector<fs::path> protected_files, protected_directories;
    Json report() const;
    // A Git selection pins the catalogue's resolved commits, even if a branch moves later.
    LoadRequest select(std::size_t index, bool after_side = false, bool compare = false) const;
};
std::shared_ptr<const BrowseDocument> load_browser(const BrowseRequest &,
                                                   const std::function<bool()> &cancelled);
class BrowserSession {
  public:
    using Loader = std::function<std::shared_ptr<const BrowseDocument>(const BrowseRequest &,
                                                                       const std::function<bool()> &)>;
    explicit BrowserSession(Loader loader = load_browser);
    ~BrowserSession();
    BrowserSession(const BrowserSession &) = delete;
    BrowserSession &operator=(const BrowserSession &) = delete;
    void request(BrowseRequest);
    void cancel();
    bool poll();
    bool busy() const {
        return busy_;
    }
    const std::string &error() const {
        return error_;
    }
    const std::shared_ptr<const BrowseDocument> &document() const {
        return document_;
    }

  private:
    struct Work {
        std::uint64_t id;
        BrowseRequest request;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    struct Result {
        std::uint64_t id;
        std::shared_ptr<const BrowseDocument> document;
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
    std::shared_ptr<const BrowseDocument> document_;
    std::thread worker_;
    void run();
};
} // namespace cyuediff::desktop
