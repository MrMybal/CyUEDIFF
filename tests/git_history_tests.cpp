#include "../src/git_history.hpp"
#include "package_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F call, const char *code) {
    try {
        call();
    } catch (const Error &error) {
        check(error.code == code, "Expected history error code");
        return;
    }
    throw std::runtime_error("Expected history error");
}
std::string record(const std::string &id, const std::string &parents = "",
                   const std::string &subject = "Subject") {
    std::string result;
    for (const auto &field :
         {id, parents, std::string("Auteur é"), std::string("2026-01-02T12:30:00+01:00"), subject}) {
        result += field;
        result += '\0';
    }
    return result;
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
        auto root = fs::u8path(argv[2]);
        fs::create_directories(root / "before");
        fs::create_directories(root / "after");
        save(root / "before", "Asset", fixture(1012, 1));
        save(root / "after", "Asset", fixture(1012, 2));
        return 0;
    }
    auto parent = fs::weakly_canonical(fs::current_path());
    auto root = parent / (".test-tmp-history-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        const std::string a(40, 'a'), b(40, 'b'), c(40, 'c');
        auto bytes = record(a, b + " " + c, "100% ### [é]") + record(b, "", "");
        GitHistoryOptions page;
        page.max_commits = 1;
        auto history = detail::parse_history(bytes, a, page);
        check(history.commits.size() == 1 && history.has_more && history.next_skip == 1 &&
                  history.commits[0].parents == std::vector<std::string>({b, c}),
              "Merge record and pagination sentinel");
        check(history.to_json()["commits"][0]["subject"] == "100% ### [é]", "Metadata retained as data");
        page.skip = 1;
        history = detail::parse_history(record(b), a, page);
        check(!history.has_more && history.next_skip == 2 && history.head == a, "Pinned next page");
        check(detail::parse_history("", a, page).commits.empty(), "Empty terminal page");
        for (std::size_t n = 1; n < record(a).size(); ++n)
            rejects([&] { detail::parse_history(record(a).substr(0, n), a, page); }, "git-format");
        rejects([&] { detail::parse_history(record(a) + record(a), a, page); }, "git-format");
        rejects([&] { detail::parse_history(record(a) + record(b) + record(c), a, page); }, "git-format");
        rejects([&] { detail::parse_history(record(a, "bad"), a, page); }, "git-format");
        rejects([&] { detail::parse_history(record(a, "", std::string(1, char(255))), a, page); },
                "git-format");
        rejects([&] { detail::parse_history(record(a, "", std::string(128 * 1024 + 1, 'x')), a, page); },
                "git-format");
        rejects([&] { detail::parse_history(record(a), "bad", page); }, "git-format");
        auto limited = page;
        limited.max_bytes = 1;
        rejects([&] { detail::parse_history(record(a), a, limited); }, "git-output");
        limited = page;
        limited.max_commits = 0;
        rejects([&] { detail::parse_history("", a, limited); }, "git-history-options");
        limited.max_commits = 1001;
        rejects([&] { detail::parse_history("", a, limited); }, "git-history-options");
        limited = page;
        limited.skip = 1000001;
        rejects([&] { detail::parse_history("", a, limited); }, "git-history-options");
        OperationControl cancelled;
        cancelled.cancelled = [] { return true; };
        rejects([&] { detail::parse_history(bytes, a, page, cancelled); }, "cancelled");
        int progress = 0;
        cancelled.cancelled = [&] { return progress == 2; };
        cancelled.progress = [&](const auto &) { ++progress; };
        rejects([&] { detail::parse_history(bytes, a, page, cancelled); }, "cancelled");
        GitOptions options;
        if (argc == 3 && std::string(argv[1]) == "--git")
            options.executable = fs::u8path(argv[2]);
        options.temporary_root = root;
        detail::GitWorkspace helper(root);
        auto repo_path = root / fs::u8path("répo [historique]");
        fs::create_directory(repo_path);
        auto git = [&](const std::vector<std::string> &args) {
            return detail::git_text(options, repo_path, args, helper);
        };
        git({"init", "--quiet"});
        git({"config", "user.name", "Auteur é"});
        git({"config", "user.email", "test@invalid.example"});
        git({"config", "core.hooksPath", "missing-hooks"});
        git({"config", "commit.gpgSign", "false"});
        git({"commit", "--quiet", "--allow-empty", "-m", "Initial é"});
        git({"commit", "--quiet", "--allow-empty", "-m", "Second ### 100%"});
        auto repo = GitRepository::open(repo_path, options);
        page.skip = 0;
        const auto first = repo.history("HEAD", page);
        check(first.has_more && first.commits[0].subject == "Second ### 100%" &&
                  first.commits[0].author == "Auteur é",
              "Real Git metadata and UTF-8");
        git({"commit", "--quiet", "--allow-empty", "-m", "New tip"});
        page.skip = first.next_skip;
        auto next = repo.history(first.head, page);
        check(!next.has_more && next.commits[0].subject == "Initial é",
              "Page remains pinned after branch moves");
        check(repo.history().commits.size() == 3, "Refresh reads new tip");
        auto short_options = options;
        short_options.control.cancelled = [] { return true; };
        rejects([&] { GitRepository::open(repo_path, short_options).history(); }, "cancelled");
        limited = {};
        limited.max_bytes = 1;
        rejects([&] { repo.history("HEAD", limited); }, "git-output");
        rejects([&] { repo.history("--all"); }, "git-revision");
        git({"status", "--porcelain"});
        check(git({"status", "--porcelain"}).empty(), "History leaves checkout unchanged");
        std::cout << checks << " Git history checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
    fs::remove_all(root);
    return 0;
}
