#include "../apps/desktop/workspace.hpp"
#include "package_fixture.hpp"
#include <future>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &error) {
        check(error.code == code, "Expected workspace error");
        return;
    }
    throw std::runtime_error("Expected workspace failure");
}
void settle(DocumentWorkspace &workspace) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (workspace.busy() && std::chrono::steady_clock::now() < until) {
        workspace.poll();
        std::this_thread::yield();
    }
    check(!workspace.busy(), "Workspace settles within deadline");
}
std::shared_ptr<const Document> result(const LoadRequest &request) {
    auto document = std::make_shared<Document>();
    document->request = request;
    document->request.control = {};
    document->request.cache.reset();
    return document;
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
        const auto root = fs::u8path(argv[2]);
        fs::create_directories(root);
        cyuediff::test::save(root, "Before", cyuediff::test::fixture(1012, 1));
        cyuediff::test::save(root, "After", cyuediff::test::fixture(1012, 2));
        return 0;
    }
    try {
        rejects([] { DocumentWorkspace invalid(load_document, 0); }, "document-limit");
        rejects([] { DocumentWorkspace invalid(load_document, 33); }, "document-limit");
        std::atomic<bool> release{false}, reached{false};
        std::promise<void> began;
        auto started = began.get_future();
        std::mutex mutex;
        std::vector<std::string> calls;
        unsigned simultaneous = 0, peak = 0;
        DocumentWorkspace workspace(
            [&](const LoadRequest &request) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    calls.push_back(request.before);
                    peak = std::max(peak, ++simultaneous);
                }
                try {
                    if (request.before == "slow") {
                        began.set_value();
                        while (!release) {
                            request.control.checkpoint("Test document");
                            std::this_thread::yield();
                        }
                    }
                    request.control.check();
                    if (request.before == "failure")
                        throw Error("fixture", "Synthetic load failure");
                    auto value = result(request);
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        --simultaneous;
                    }
                    return value;
                } catch (...) {
                    std::lock_guard<std::mutex> lock(mutex);
                    --simultaneous;
                    throw;
                }
            },
            3);
        const auto first = workspace.active_id();
        check(first && workspace.documents().size() == 1 && !workspace.document(), "Initial empty document");
        LoadRequest slow{"slow"};
        slow.control.progress = [&](const auto &) { reached = true; };
        workspace.request(slow);
        check(started.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "First load starts");
        const auto second = workspace.open({"discarded"});
        workspace.request(second, {"second"});
        const auto third = workspace.open({"closed-before-load"});
        check(workspace.find(second)->busy && workspace.find(second)->progress.phase == "Queued document",
              "Independent queued state");
        check(!workspace.document() && workspace.active_busy(), "Active queued document is empty");
        rejects([&] { workspace.create(); }, "document-limit");
        check(workspace.select(first), "Select first document while second waits");
        check(workspace.close(third) && !workspace.find(third), "Closing queued document removes its work");
        check(workspace.find(first)->request.control.progress == nullptr &&
                  !workspace.find(first)->request.cache,
              "Saved request retains data only");
        release = true;
        settle(workspace);
        check(reached && peak == 1 && calls == std::vector<std::string>({"slow", "second"}),
              "One decoder and latest queued request only; external progress preserved");
        check(workspace.document()->request.before == "slow",
              "Inactive publication never replaces active document");
        check(workspace.select(second) && workspace.document()->request.before == "second",
              "Tab switch retains loaded snapshot");
        check(!workspace.select(999) && workspace.active_id() == second && !workspace.close(999),
              "Invalid selection is inert");
        const auto saved = workspace.document();
        workspace.request({"failure"});
        settle(workspace);
        check(workspace.document() == saved && workspace.error().find("fixture:") == 0,
              "Failure retains only this document's last snapshot");
        check(workspace.select(first) && workspace.error().empty(), "Errors remain local to a document");
        workspace.request(first, {"refreshed"});
        settle(workspace);
        check(workspace.find(first)->revision == 2 && workspace.document()->request.before == "refreshed",
              "Reload replaces the targeted snapshot");
        workspace.cancel(first);
        check(workspace.cancelled() && !workspace.busy() && workspace.document(),
              "Idle cancellation retains snapshot");
        const auto fourth = workspace.create();
        check(fourth > third, "Closed IDs are never reused");
        check(workspace.close(first) && workspace.active_id() == fourth,
              "Closing inactive tab preserves active ID");
        check(workspace.close(fourth) && workspace.active_id() == second,
              "Closing active tab selects an adjacent document");
        check(workspace.close(second) && !workspace.active_id() && !workspace.document() &&
                  workspace.error().empty(),
              "Closing the last document releases active state");
        workspace.request({"new"});
        settle(workspace);
        check(workspace.active_id() > fourth && workspace.document()->request.before == "new",
              "Request can recreate an empty workspace");
        rejects([&] { workspace.request(999, {"invalid"}); }, "desktop-document");
        std::weak_ptr<const Document> weak;
        {
            DocumentWorkspace owned(result);
            owned.request({"released"});
            settle(owned);
            weak = owned.document();
            check(owned.close(owned.active_id()) && weak.expired(),
                  "Closed snapshot is not pinned by the worker");
        }
        std::atomic<bool> in_flight{false};
        DocumentWorkspace cancelled([&](const auto &request) {
            if (request.before == "cancel") {
                in_flight = true;
                for (;;) {
                    request.control.check();
                    std::this_thread::yield();
                }
            }
            return result(request);
        });
        auto closed = cancelled.active_id();
        cancelled.request({"cancel"});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!in_flight && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        check(in_flight, "Cancellation fixture starts");
        auto remaining = cancelled.open({"survives"});
        cancelled.close(closed);
        settle(cancelled);
        check(cancelled.active_id() == remaining && cancelled.document()->request.before == "survives" &&
                  cancelled.error().empty(),
              "Closing running work discards its result and advances the queue");
        auto again = cancelled.open({"newer"});
        cancelled.cancel(again);
        settle(cancelled);
        check(cancelled.find(again)->cancelled && !cancelled.document(),
              "Cancelled queued request never publishes");
        DocumentWorkspace lifecycle(result, 3);
        const auto original = lifecycle.active_id();
        lifecycle.request({"original"});
        settle(lifecycle);
        const auto copy = lifecycle.duplicate(original);
        settle(lifecycle);
        check(copy != original && lifecycle.document() != lifecycle.find(original)->document &&
                  lifecycle.document()->request.before == "original",
              "Duplicate reloads context as an independent document");
        check(lifecycle.close_others(copy) && lifecycle.documents().size() == 1 &&
                  lifecycle.active_id() == copy,
              "Close others preserves the requested ID");
        auto restored = lifecycle.reopen();
        settle(lifecycle);
        check(restored > copy && lifecycle.document()->request.before == "original",
              "Reopen uses a new ID and reloads saved context");
        check(lifecycle.closed().empty(), "Reopened context leaves the closed stack");
        rejects([&] { lifecycle.reopen(); }, "desktop-document");
        auto empty = lifecycle.create();
        lifecycle.close(empty);
        auto history_count = lifecycle.closed().size();
        auto full = lifecycle.create();
        rejects([&] { lifecycle.reopen(); }, "document-limit");
        check(lifecycle.closed().size() == history_count,
              "A tab-limit failure does not consume reopen history");
        lifecycle.close_others(full);
        for (int j = 0; j < 20; ++j) {
            auto temporary = lifecycle.create();
            lifecycle.close(temporary);
        }
        check(lifecycle.closed().size() == 8, "Closed context history stays bounded without snapshots");
        check(!lifecycle.close_others(999), "Invalid close-others ID is inert");
        DocumentWorkspace reset(result);
        reset.request({"retained"});
        settle(reset);
        std::weak_ptr<const Document> released = reset.document();
        auto previous = reset.active_id();
        reset.open({"superseded"});
        reset.clear();
        check(reset.documents().empty() && reset.closed().empty() && !reset.busy() && released.expired(),
              "Clear releases documents and closes history without starting queued work");
        auto fresh = reset.create();
        check(fresh > previous, "Clear preserves non-reused document IDs");
        reset.request({"fresh"});
        settle(reset);
        check(reset.document()->request.before == "fresh",
              "Pre-clear results cannot publish in restored workspace");
        std::cout << checks << " document workspace checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
