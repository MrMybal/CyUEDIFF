#include "../apps/desktop/history.hpp"
#include <chrono>
#include <future>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void settle(HistorySession &session) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (session.busy() && std::chrono::steady_clock::now() < end) {
        session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!session.busy(), "History worker settled");
}
} // namespace
int main() {
    try {
        HistoryDocument document;
        document.repository = "repository";
        document.page.head = std::string(40, 'a');
        document.page.commits.push_back({std::string(40, 'a'), "Author", "2026-01-01", "Subject", {}});
        GitSource original;
        original.repository = "other";
        original.before_revision = "old";
        original.after_revision = "new";
        original.lfs_objects = "objects";
        original.discover_engine = false;
        auto before = document.select(document.page.head, original, false);
        check(before.before_revision == document.page.head && before.after_revision == "new" &&
                  before.repository == "repository" && before.lfs_objects == "objects" &&
                  !before.discover_engine,
              "Selection pins one side and retains other loading options");
        auto after = document.select(document.page.head, before, true);
        check(after.before_revision == after.after_revision && original.before_revision == "old",
              "After selection and immutable input");
        bool rejected = false;
        try {
            document.select("missing", original, false);
        } catch (const Error &e) {
            rejected = e.code == "history-selection";
        }
        check(rejected, "Unlisted commits cannot be selected");
        check(document.report()["head"] == document.page.head, "History report uses pinned page");
        HistorySession session(
            [](const auto &request, const auto &) -> std::shared_ptr<const HistoryDocument> {
                if (request.revision == "fail")
                    throw Error("history-test", "Unavailable");
                auto result = std::make_shared<HistoryDocument>();
                result->request = request;
                return result;
            });
        session.request({});
        settle(session);
        auto previous = session.document();
        check(previous && session.error().empty(), "Successful immutable page published");
        HistoryRequest request;
        request.revision = "fail";
        session.request(request);
        settle(session);
        check(session.document() == previous && session.error().find("history-test") == 0,
              "Failure retains previous page with error");
        session.cancel();
        check(!session.busy() && session.error().empty() && !session.poll(),
              "Cancel clears pending publication");
        std::promise<void> started, release;
        auto began = started.get_future();
        auto gate = release.get_future().share();
        bool observed = false;
        std::vector<std::string> calls;
        HistorySession latest(
            [&](const auto &work, const auto &cancel) -> std::shared_ptr<const HistoryDocument> {
                calls.push_back(work.revision);
                if (work.revision == "first") {
                    started.set_value();
                    gate.wait();
                    observed = cancel();
                }
                auto result = std::make_shared<HistoryDocument>();
                result->request = work;
                return result;
            });
        request.revision = "first";
        latest.request(request);
        if (began.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            release.set_value();
            throw std::runtime_error("Worker did not start");
        }
        request.revision = "second";
        latest.request(request);
        request.revision = "third";
        latest.request(request);
        release.set_value();
        settle(latest);
        check(observed && calls == std::vector<std::string>({"first", "third"}) &&
                  latest.document()->request.revision == "third",
              "Superseded work observes cancellation; only newest pending page runs and publishes");
        HistorySession empty(
            [](const auto &, const auto &) -> std::shared_ptr<const HistoryDocument> { return {}; });
        empty.request({});
        settle(empty);
        check(!empty.document() && empty.error().find("history-loader") == 0, "Null loader results rejected");
        bool shutdown = false;
        std::promise<void> shutdown_started;
        auto shutdown_began = shutdown_started.get_future();
        {
            HistorySession active(
                [&](const auto &, const auto &cancel) -> std::shared_ptr<const HistoryDocument> {
                    shutdown_started.set_value();
                    while (!cancel())
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    shutdown = true;
                    return std::make_shared<HistoryDocument>();
                });
            active.request({});
            check(shutdown_began.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                  "Shutdown worker started");
        }
        check(shutdown, "Shutdown signals cancellation and joins active worker");
        std::cout << checks << " history model checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
