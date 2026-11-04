#include "../apps/desktop/document.hpp"
#include "../apps/desktop/browser.hpp"
#include "../src/git_process.hpp"
#include "package_fixture.hpp"
#include <chrono>
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
        check(e.code == code, ("Expected " + code + ", got " + e.code).c_str());
        return;
    }
    throw std::runtime_error("Expected error: " + code);
}
void text(const fs::path &path, const std::string &value) {
    std::ofstream file(path, std::ios::binary);
    file << value;
    if (!file)
        throw std::runtime_error("Cannot write fixture");
}
std::string pointer(const fs::path &file, const fs::path &objects) {
    auto oid = sha256_file(file);
    auto size = fs::file_size(file);
    auto target = objects / oid.substr(0, 2) / oid.substr(2, 2) / oid;
    fs::create_directories(target.parent_path());
    fs::copy_file(file, target, fs::copy_options::overwrite_existing);
    text(file, "version https://git-lfs.github.com/spec/v1\noid sha256:" + oid + "\nsize " +
                   std::to_string(size) + "\n");
    return oid;
}
void settle(DocumentSession &session) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (session.busy() && std::chrono::steady_clock::now() < deadline) {
        session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!session.busy(), "Worker completed within deadline");
}
} // namespace
int main(int argc, char **argv) {
    auto parent = fs::weakly_canonical(fs::current_path());
    auto root = parent / (".test-tmp-desktop-git-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    auto cleanup = [&] {
        auto target = fs::weakly_canonical(root);
        if (target.parent_path() == parent &&
            target.filename().u8string().rfind(".test-tmp-desktop-git-", 0) == 0)
            fs::remove_all(target);
    };
    try {
        auto repo_path = root / fs::u8path("Dépôt [local]");
        fs::create_directories(repo_path / "Content");
        GitOptions options;
        if (argc == 3 && std::string(argv[1]) == "--git")
            options.executable = fs::u8path(argv[2]);
        options.temporary_root = root;
        detail::GitWorkspace helper(root);
        auto git = [&](const std::vector<std::string> &args) {
            return detail::git_text(options, repo_path, args, helper);
        };
        git({"init", "--quiet"});
        git({"config", "user.name", "CyUEDiff tests"});
        git({"config", "user.email", "tests@invalid.example"});
        git({"config", "core.autocrlf", "false"});
        git({"config", "core.hooksPath", "nonexistent-hooks"});
        git({"config", "commit.gpgSign", "false"});
        auto commit = [&](const std::string &message) {
            git({"add", "--all"});
            git({"commit", "--quiet", "--allow-empty", "-m", message});
            auto id = git({"rev-parse", "HEAD"});
            while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
                id.pop_back();
            return id;
        };
        auto descriptor = repo_path / "Game.uproject";
        text(descriptor, R"({"FileVersion":3,"EngineAssociation":"5.3"})");
        auto empty = commit("empty");
        auto objects = repo_path / ".git/lfs/objects";
        auto asset = save(repo_path / "Content", "Asset é", fixture(1012, 1), true);
        auto exports = asset;
        exports.replace_extension(".uexp");
        pointer(exports, objects);
        auto bulk = asset;
        bulk.replace_extension(".ubulk");
        text(bulk, "historical bulk");
        auto before = commit("before");
        text(descriptor, R"({"FileVersion":3,"EngineAssociation":"5.5"})");
        auto context_only = commit("context only");
        save(repo_path / "Content", "Asset é", fixture(1012, 2), true);
        auto after_oid = pointer(exports, objects);
        text(bulk, "new bulk");
        auto after = commit("after");
        const auto path = asset.lexically_relative(repo_path).generic_u8string();
        auto renamed = repo_path / "Content/Renamed.uasset";
        for (const auto *extension : {".uasset", ".uexp", ".ubulk"}) {
            auto a = asset, b = renamed;
            a.replace_extension(extension);
            b.replace_extension(extension);
            git({"mv", "--", a.lexically_relative(repo_path).generic_u8string(),
                 b.lexically_relative(repo_path).generic_u8string()});
        }
        auto moved = commit("renamed");
        auto index_hash = sha256_file(repo_path / ".git/index");
        auto checkout_hash = sha256_file(renamed);
        auto state = git({"status", "--porcelain=v1"});
        LoadRequest request;
        request.before = path;
        request.previews = false;
        request.git = GitSource{repo_path, {}, before, after};
        auto document = load_document(request);
        check(document->status() == "changed" && document->diff && document->git,
              "Git document has semantic diff and provenance");
        check(document->before->exports[0].properties["Value"]["value"] == 1 &&
                  document->after->exports[0].properties["Value"]["value"] == 2,
              "Each revision uses its own LFS exports");
        check(document->git->before.commit == before && document->git->after.commit == after,
              "Resolved immutable commit IDs retained");
        check(document->report()["git"]["comparison"] == document->diff->to_json(),
              "Report includes Git comparison contract");
        auto materialized = document->before->source;
        auto owned_bulk = materialized;
        owned_bulk.replace_extension(".ubulk");
        check(fs::exists(materialized) && fs::exists(owned_bulk),
              "Revision payload files outlive the loader");
        {
            std::ifstream data(owned_bulk);
            std::string value;
            std::getline(data, value);
            check(value == "historical bulk", "Historical companion comes from matching commit");
        }
        auto direct = compare_git(GitRepository::open(repo_path), before, after, path);
        check(document->diff->to_json() == direct.diff.to_json(), "Desktop and Git API diff parity");
        document.reset();
        check(!fs::exists(materialized) && !fs::exists(owned_bulk),
              "Document releases owned materializations");
        auto repository = GitRepository::open(repo_path);
        auto catalog = repository.catalog(before);
        check(catalog.complete && catalog.assets.size() == 1 && catalog.revision == before &&
                  !catalog.assets[0].blob_oid.empty(),
              "Git metadata catalogue pins revision and blob ID");
        CatalogOptions limits;
        limits.max_entries = 1;
        check(!repository.catalog(after, limits).complete, "Git tree entry limit is explicit");
        limits = {};
        limits.max_path_bytes = 1;
        check(!repository.catalog(before, limits).complete, "Git catalogue path memory budget");
        limits = {};
        limits.max_listing_bytes = 1;
        rejects([&] { repository.catalog(before, limits); }, "git-output");
        limits = {};
        limits.cancelled = [] { return true; };
        check(repository.catalog(before, limits).cancelled, "Git catalogue cancellation");
        BrowseRequest browse;
        browse.git = request.git;
        auto browsed = load_browser(browse, [] { return false; });
        check(browsed->assets.size() == 1 && browsed->assets[0].before && browsed->assets[0].after,
              "Git browser merges both revision catalogues");
        check(load_document(browsed->select(0, false, true))->diff->to_json() == direct.diff.to_json(),
              "Git catalogue selection matches direct comparison");
        auto equal = request;
        equal.git->after_revision = before;
        check(load_document(equal)->status() == "identical", "Identical revisions");
        auto external_cache = root / "external-lfs";
        fs::rename(objects, external_cache);
        auto explicit_cache = request;
        explicit_cache.git->lfs_objects = external_cache;
        check(load_document(explicit_cache)->status() == "changed",
              "Explicit external LFS cache used by desktop loader");
        fs::rename(external_cache, objects);
        auto added = request;
        added.git->before_revision = empty;
        auto addition = load_document(added);
        check(addition->status() == "added" && !addition->before && addition->after &&
                  addition->report()["before"].is_null(),
              "Added package keeps absent before side explicit");
        auto removed = request;
        removed.git->after_revision = empty;
        auto removal = load_document(removed);
        check(removal->status() == "removed" && removal->before && !removal->after,
              "Removed package keeps absent after side explicit");
        auto renamed_request = request;
        renamed_request.git->after_revision = moved;
        renamed_request.after = "Content/Renamed.uasset";
        check(load_document(renamed_request)->git->after.repository_path == renamed_request.after,
              "Independent after path supports renames");
        auto contextual = request;
        contextual.git->after_revision = context_only;
        contextual.git->discover_engine = false;
        contextual.project = "Game.uproject";
        contextual.before = "/Game/Asset é";
        text(descriptor, "invalid checkout descriptor");
        auto context = load_document(contextual);
        check(context->diff->status() == "identical" && context->status() == "changed",
              "Context-only edit changes document status");
        check(!context->git->context_changes.empty() &&
                  context->git->before_context["engineAssociation"] == "5.3" &&
                  context->git->after_context["engineAssociation"] == "5.5",
              "Historical descriptors used on both sides");
        check(std::find(context->protected_files.begin(), context->protected_files.end(), descriptor) !=
                  context->protected_files.end(),
              "Historical descriptor protected from output overlap");
        text(descriptor, R"({"FileVersion":3,"EngineAssociation":"5.5"})");
        auto missing = request;
        missing.before = "Content/Missing.uasset";
        rejects([&] { load_document(missing); }, "git-asset-missing");
        auto invalid = request;
        invalid.git->before_revision = "";
        rejects([&] { load_document(invalid); }, "desktop-git");
        invalid = request;
        invalid.before = "../escape.uasset";
        rejects([&] { load_document(invalid); }, "git-path");
        invalid = request;
        invalid.engine_root = root;
        rejects([&] { load_document(invalid); }, "desktop-git");
        invalid = contextual;
        invalid.engine_root = root;
        rejects([&] { load_document(invalid); }, "desktop-git");
        auto cached = objects / after_oid.substr(0, 2) / after_oid.substr(2, 2) / after_oid;
        auto unavailable = root / "held-lfs-object";
        fs::rename(cached, unavailable);
        rejects([&] { load_document(request); }, "lfs-missing");
        check(repository.catalog(after).assets.size() == 1 &&
                  load_browser(browse, [] { return false; })->assets.size() == 1,
              "Git catalogue and browser work with missing LFS content");
        fs::rename(unavailable, cached);
        DocumentSession session;
        session.request(request);
        settle(session);
        auto previous = session.document();
        check(previous && session.error().empty(), "Git document publishes on worker");
        session.request(missing);
        settle(session);
        check(session.document() == previous && session.error().rfind("git-asset-missing:", 0) == 0,
              "Git failure preserves previous document");
        check(sha256_file(repo_path / ".git/index") == index_hash && sha256_file(renamed) == checkout_hash &&
                  git({"status", "--porcelain=v1"}) == state,
              "Reads leave index and checkout unchanged");
        std::cout << checks << " desktop Git checks passed\n";
        cleanup();
    } catch (const std::exception &e) {
        cleanup();
        std::cerr << "Desktop Git test failure: " << e.what() << '\n';
        return 1;
    }
}
