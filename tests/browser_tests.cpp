#include "../apps/desktop/browser.hpp"
#include "package_fixture.hpp"
#include <chrono>
#include <future>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const Error &e) {
        check(e.code == code, "Expected error code");
        return;
    }
    throw std::runtime_error("Expected error");
}
void settle(BrowserSession &session) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (session.busy() && std::chrono::steady_clock::now() < end) {
        session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!session.busy(), "Catalogue worker settled");
}
} // namespace
int main() {
    auto parent = fs::weakly_canonical(fs::current_path());
    auto root = parent / (".test-tmp-browser-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    auto cleanup = [&] {
        auto path = fs::weakly_canonical(root);
        if (path.parent_path() == parent && path.filename().u8string().rfind(".test-tmp-browser-", 0) == 0)
            fs::remove_all(path);
    };
    try {
        fs::create_directories(root / "Content");
        auto asset = save(root / "Content", "Sample é", fixture(1012));
        std::ofstream(root / "Game.uproject") << R"({"FileVersion":3,"EngineAssociation":"{unresolved}"})";
        fs::create_directories(root / "Plugins/Empty");
        std::ofstream(root / "Plugins/Empty/Empty.uplugin") << R"({"CanContainContent":true})";
        const auto context = ProjectContext::open(root / "Game.uproject");
        check(std::any_of(context.mounts.begin(), context.mounts.end(),
                          [](const auto &mount) { return mount.name == "/Empty" && mount.allow_missing; }),
              "Project context preserves the namespace of content-capable plugins without installed content");
        BrowseRequest request;
        request.project = root / "Game.uproject";
        auto document = load_browser(request, [] { return false; });
        check(document->assets.size() == 1 && document->before.complete &&
                  document->assets[0].label == "/Game/Sample é",
              "Project browser lists mounted metadata");
        auto selected = document->select(0);
        selected.previews = false;
        check(fs::equivalent(load_document(selected)->before->source, asset),
              "Browser selection opens the exact package");
        check(document->select(0, true).after == asset.u8string(), "Local asset can fill after path");
        rejects([&] { document->select(1); }, "browser-selection");
        rejects([&] { document->select(0, false, true); }, "browser-selection");
        rejects([&] { load_browser({}, [] { return false; }); }, "browser-project");
        auto cancelled = load_browser(request, [] { return true; });
        check(cancelled->before.cancelled && !cancelled->before.complete, "Loader propagates cancellation");
        BrowseDocument git;
        git.request.git = GitSource{root, {}, "moving-before", "moving-after"};
        git.before.revision = std::string(40, 'a');
        git.after.revision = std::string(40, 'b');
        git.assets = {{"A.uasset", "A.uasset", ".uasset", true, false},
                      {"B.uasset", "B.uasset", ".uasset", false, true}};
        auto comparison = git.select(1, false, true);
        check(comparison.before == "B.uasset" && comparison.after.empty() &&
                  comparison.git->before_revision == git.before.revision &&
                  comparison.git->after_revision == git.after.revision,
              "Git selection pins resolved catalogue commits and supports additions");
        rejects([&] { git.select(0, true); }, "browser-selection");
        rejects([&] { git.select(1, false); }, "browser-selection");
        check(git.report()["assets"][1]["after"] == true, "Presence in browser report");
        BrowserSession session;
        check(!session.busy() && !session.document() && !session.poll(), "Empty browser session");
        session.request(request);
        settle(session);
        auto previous = session.document();
        check(previous && session.error().empty(), "Catalogue published asynchronously");
        session.request({});
        settle(session);
        check(session.document() == previous && !session.error().empty(),
              "Failed scan retains previous catalogue");
        session.request(request);
        settle(session);
        check(session.error().empty(), "Scan recovery clears error");
        std::promise<void> started, release;
        auto gate = release.get_future().share();
        auto began = started.get_future();
        std::vector<std::string> calls;
        bool first_cancelled = false;
        BrowserSession latest(
            [&](const BrowseRequest &work,
                const std::function<bool()> &cancel) -> std::shared_ptr<const BrowseDocument> {
                calls.push_back(work.project.u8string());
                if (work.project == "first") {
                    started.set_value();
                    gate.wait();
                    first_cancelled = cancel();
                }
                auto result = std::make_shared<BrowseDocument>();
                result->request = work;
                return result;
            });
        BrowseRequest work;
        work.project = "first";
        latest.request(work);
        if (began.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            release.set_value();
            throw std::runtime_error("Worker did not start");
        }
        work.project = "second";
        latest.request(work);
        work.project = "third";
        latest.request(work);
        release.set_value();
        settle(latest);
        check(first_cancelled && latest.document()->request.project == "third" &&
                  calls == std::vector<std::string>({"first", "third"}),
              "Superseded scan cancelled and pending work bounded");
        latest.cancel();
        check(!latest.busy() && !latest.poll() && latest.document()->request.project == "third",
              "Cancellation retains published catalogue");
        std::promise<void> cancel_started, cancel_release, cancel_finished;
        auto cancel_gate = cancel_release.get_future().share();
        auto cancel_began = cancel_started.get_future();
        auto cancel_ended = cancel_finished.get_future();
        bool observed_cancel = false;
        BrowserSession active(
            [&](const auto &, const std::function<bool()> &cancel) -> std::shared_ptr<const BrowseDocument> {
                cancel_started.set_value();
                cancel_gate.wait();
                observed_cancel = cancel();
                cancel_finished.set_value();
                return std::make_shared<BrowseDocument>();
            });
        active.request(request);
        if (cancel_began.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            cancel_release.set_value();
            throw std::runtime_error("Cancellation worker did not start");
        }
        active.cancel();
        cancel_release.set_value();
        check(cancel_ended.wait_for(std::chrono::seconds(5)) == std::future_status::ready && observed_cancel,
              "Closing an active scan signals cancellation");
        check(!active.busy() && !active.poll() && !active.document(), "Cancelled results cannot publish");
        BrowserSession null(
            [](const auto &, const auto &) -> std::shared_ptr<const BrowseDocument> { return {}; });
        null.request({});
        settle(null);
        check(null.error().rfind("browser-loader:", 0) == 0, "Invalid loader result reported");
#ifndef CYUEDIFF_HAS_GIT
        BrowseRequest unsupported;
        unsupported.git = GitSource{};
        rejects([&] { load_browser(unsupported, [] { return false; }); }, "desktop-git");
#endif
        std::cout << checks << " browser model checks passed\n";
        cleanup();
    } catch (const std::exception &e) {
        cleanup();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
