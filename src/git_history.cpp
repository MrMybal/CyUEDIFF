#include "git_history.hpp"
#include <fstream>
#include <set>
#include <sstream>
namespace cyuediff {
Json GitCommit::to_json() const {
    return {{"id", id},
            {"parents", parents},
            {"author", author},
            {"authoredAt", authored_at},
            {"subject", subject}};
}
Json GitHistory::to_json() const {
    Json rows = Json::array();
    for (const auto &commit : commits)
        rows.push_back(commit.to_json());
    return {{"schema", "cyuediff.git-history/1"},
            {"head", head},
            {"skip", skip},
            {"nextSkip", next_skip},
            {"hasMore", has_more},
            {"commits", std::move(rows)}};
}
namespace detail {
namespace {
bool oid(const std::string &value, std::size_t size) {
    return (size == 40 || size == 64) && value.size() == size &&
           value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void validate(const GitHistoryOptions &options) {
    if (!options.max_commits || options.max_commits > 1000 || options.skip > 1'000'000 ||
        !options.max_bytes || options.max_bytes > 64 * 1024 * 1024)
        throw Error("git-history-options",
                    "History requires 1..1000 commits, skip <= 1000000 and 1..64 MiB bytes");
}
} // namespace
GitHistory parse_history(const std::string &bytes, const std::string &head, const GitHistoryOptions &options,
                         const OperationControl &control) {
    validate(options);
    control.check();
    if (bytes.size() > options.max_bytes)
        throw Error("git-output", "History exceeds its byte limit");
    if (!oid(head, head.size()))
        throw Error("git-format", "Invalid history head");
    GitHistory result;
    result.head = head;
    result.skip = options.skip;
    std::size_t at = 0;
    auto field = [&]() {
        const auto end = bytes.find('\0', at);
        if (end == std::string::npos || end - at > 128 * 1024)
            throw Error("git-format", "Invalid or oversized history field");
        auto value = bytes.substr(at, end - at);
        at = end + 1;
        return value;
    };
    std::set<std::string> seen;
    while (at < bytes.size()) {
        control.checkpoint("Reading Git history", result.commits.size(), options.max_commits);
        if (seen.size() > options.max_commits)
            throw Error("git-format", "Too many history records");
        GitCommit commit;
        commit.id = field();
        auto parents = field();
        commit.author = field();
        commit.authored_at = field();
        commit.subject = field();
        if (!oid(commit.id, head.size()) || !seen.insert(commit.id).second || commit.authored_at.empty() ||
            commit.authored_at.size() > 40)
            throw Error("git-format", "Invalid or duplicate history commit");
        std::istringstream stream(parents);
        std::string parent;
        while (stream >> parent) {
            if (!oid(parent, head.size()) || commit.parents.size() >= 1024)
                throw Error("git-format", "Invalid history parent list");
            commit.parents.push_back(parent);
        }
        try {
            (void)commit.to_json().dump();
        } catch (const Json::exception &) {
            throw Error("git-format", "History contains invalid UTF-8 metadata");
        }
        if (result.commits.size() < options.max_commits)
            result.commits.push_back(std::move(commit));
        else
            result.has_more = true;
    }
    result.next_skip = options.skip + static_cast<std::uint32_t>(result.commits.size());
    control.check();
    return result;
}
GitHistory read_history(const GitOptions &git, const fs::path &root, const std::string &head,
                        const GitHistoryOptions &options, GitWorkspace &workspace) {
    validate(options);
    auto output = workspace.directory / "history", errors = workspace.directory / "stderr";
    const std::vector<std::string> args = {"log",
                                           "--no-show-signature",
                                           "--no-decorate",
                                           "--no-color",
                                           "--topo-order",
                                           "--encoding=UTF-8",
                                           "-z",
                                           "--format=%H%x00%P%x00%an%x00%aI%x00%s",
                                           "--max-count=" + std::to_string(options.max_commits + 1),
                                           "--skip=" + std::to_string(options.skip),
                                           head,
                                           "--"};
    if (git_process(git, root, args, output, errors, options.max_bytes) != 0)
        throw Error("git-history", "Cannot read local commit history");
    const auto size = fs::file_size(output);
    if (size > options.max_bytes)
        throw Error("git-output", "History exceeds its byte limit");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::ifstream file(output, std::ios::binary);
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw Error("git-io", "Cannot read history output");
    return parse_history(bytes, head, options, git.control);
}
} // namespace detail
} // namespace cyuediff
