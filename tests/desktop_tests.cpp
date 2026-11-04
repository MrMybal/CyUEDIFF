#include "../apps/desktop/document.hpp"
#include "package_fixture.hpp"
#include <iostream>
#include <chrono>
#include <future>
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
template <class F> void error(F f, const char *code) {
    try {
        f();
        throw std::runtime_error("Expected error");
    } catch (const Error &e) {
        check(e.code == code, "Expected error code");
    }
}
void settle(DocumentSession &session) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (session.busy() && std::chrono::steady_clock::now() < end) {
        session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!session.busy(), "Worker settled within deadline");
}
} // namespace
int main() {
    auto root =
        fs::current_path() /
        (".test-tmp-desktop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        auto before = save(root, "before é", fixture(1012));
        auto after = save(root, "after", fixture(1012, 2), true);
        auto hash_before = sha256_file(before), hash_after = sha256_file(after);
        LoadRequest request;
        request.before = before.u8string();
        request.previews = false;
        auto document = load_document(request);
        check(document->before->exports[0].properties["Value"]["value"] == 1 && !document->after &&
                  !document->diff,
              "Inspection of Unicode filename");
        request.after = after.u8string();
        document = load_document(request);
        check(document->diff && document->diff->status() == "changed" && document->diff->changes.size() == 1,
              "Integrated and split packages compared");
        check(document->report()["before"]["exports"] == 1 && document->report()["after"]["exports"] == 1,
              "Report contains both sides");
        check(document->previews.empty() && !document->before_mesh && !document->after_mesh,
              "Previews can be disabled independently");
        request.after = request.before;
        document = load_document(request);
        check(document->diff->status() == "identical", "Self comparison");
        error([&] { load_document({}); }, "desktop-input");
        auto missing = request;
        missing.after = (root / "missing.uasset").u8string();
        error([&] { load_document(missing); }, "io");
        auto engine_without_project = request;
        engine_without_project.engine_root = root;
        error([&] { load_document(engine_without_project); }, "desktop-project");
        auto bad_codec = request;
        bad_codec.codec = root / "missing.dll";
        bad_codec.previews = false;
        check(load_document(bad_codec)->before != nullptr, "Disabled previews never load a codec");
        auto project_root = root / "project";
        fs::create_directories(project_root / "Content");
        auto project_asset = save(project_root / "Content", "TestAsset", fixture(1012));
        auto engine = root / "engine";
        fs::create_directories(engine / "Engine/Build");
        {
            std::ofstream(engine / "Engine/Build/Build.version")
                << R"({"MajorVersion":5,"MinorVersion":5,"PatchVersion":4})";
        }
        auto project_file = project_root / "Test.uproject";
        {
            std::ofstream(project_file) << R"({"FileVersion":3,"EngineAssociation":"5.5"})";
        }
        LoadRequest project_request;
        project_request.before = "/Game/TestAsset";
        project_request.project = project_file;
        project_request.engine_root = engine;
        project_request.previews = false;
        auto contextual = load_document(project_request);
        check(contextual->project && contextual->project->engine.major == 5 &&
                  fs::equivalent(contextual->before->source, project_asset),
              "Long package name resolved with explicit project and engine");
        DocumentSession session;
        check(!session.busy() && !session.document() && !session.poll(), "Empty session");
        session.request(request);
        check(session.busy(), "Load starts asynchronously");
        settle(session);
        auto previous = session.document();
        check(previous && session.error().empty(), "Successful document published");
        session.request(missing);
        settle(session);
        check(session.document() == previous && session.error().find("io:") == 0,
              "Failed after load preserves previous document atomically");
        session.request(request);
        check(session.error().empty(), "New request clears the previous error");
        settle(session);
        check(session.document() != previous && session.error().empty(), "Recovery after failure");
        std::promise<void> started, release;
        auto gate = release.get_future().share();
        std::vector<std::string> calls;
        DocumentSession latest([&](const LoadRequest &work) -> std::shared_ptr<const Document> {
            calls.push_back(work.before);
            if (work.before == "first") {
                started.set_value();
                gate.wait();
            }
            auto result = std::make_shared<Document>();
            result->request = work;
            return result;
        });
        auto began = started.get_future();
        latest.request({"first"});
        if (began.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            release.set_value();
            throw std::runtime_error("Worker did not start");
        }
        latest.request({"second"});
        latest.request({"third"});
        release.set_value();
        settle(latest);
        check(latest.document()->request.before == "third",
              "Stale active result cannot replace latest request");
        check(calls == std::vector<std::string>({"first", "third"}),
              "Pending work is bounded to the latest request");
        DocumentSession exception([](const auto &) -> std::shared_ptr<const Document> { throw 42; });
        exception.request({"x"});
        settle(exception);
        check(!exception.document() && !exception.error().empty(), "Non-standard exceptions reported");
        DocumentSession empty([](const auto &) -> std::shared_ptr<const Document> { return {}; });
        empty.request({"x"});
        settle(empty);
        check(!empty.document() && empty.error().find("desktop-loader:") == 0, "Null loader result rejected");
        error([] { DocumentSession invalid(DocumentSession::Loader{}); }, "desktop-loader");
        check(sha256_file(before) == hash_before && sha256_file(after) == hash_after, "Inputs unchanged");
        fs::remove_all(root);
        std::cout << checks << " desktop model checks passed\n";
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << "Desktop model test failure: " << e.what() << '\n';
        return 1;
    }
}
