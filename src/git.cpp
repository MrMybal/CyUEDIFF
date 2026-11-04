#include "git_process.hpp"
#include "git_project.hpp"
#include "git_history.hpp"
#include "descriptor.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
namespace cyuediff {
namespace {
std::string line(std::string value) {
    if (!value.empty() && value.back() == '\n')
        value.pop_back();
    if (!value.empty() && value.back() == '\r')
        value.pop_back();
    return value;
}
bool hex(const std::string &value, std::size_t size) {
    return value.size() == size && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::uint64_t integer(const std::string &value) {
    if (value.empty())
        throw Error("git-format", "Missing byte count");
    std::uint64_t result = 0;
    for (auto c : value) {
        if (c < '0' || c > '9' || result > (std::numeric_limits<std::uint64_t>::max() - (c - '0')) / 10)
            throw Error("git-format", "Invalid or overflowing byte count");
        result = result * 10 + static_cast<unsigned>(c - '0');
    }
    return result;
}
fs::path valid_repository_path(const std::string &input) {
    if (input.empty() || input.size() > 4096 || input.front() == '/' || input.back() == '/' ||
        input.find_first_of("\\:<>\"|?*") != std::string::npos ||
        std::any_of(input.begin(), input.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw Error("git-path", "Asset must be a portable repository-relative path with forward slashes");
    std::size_t at = 0;
    for (;;) {
        auto end = input.find('/', at);
        auto part = input.substr(at, end == std::string::npos ? end : end - at);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            throw Error("git-path", "Invalid repository path component");
        auto stem = part.substr(0, part.find('.'));
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '1' &&
             stem[3] <= '9'))
            throw Error("git-path", "Reserved filename in repository path");
        if (end == std::string::npos)
            break;
        at = end + 1;
    }
    return fs::u8path(input);
}
fs::path valid_path(const std::string &input) {
    auto path = valid_repository_path(input);
    if (path.extension() != ".uasset" && path.extension() != ".umap")
        throw Error("git-path", "Git package path must end in .uasset or .umap");
    return path;
}
struct Entry {
    std::string path, oid, mode;
    std::uint64_t bytes = 0;
};
std::map<std::string, Entry> tree(const std::string &output, const std::set<std::string> &wanted) {
    std::map<std::string, Entry> result;
    std::size_t begin = 0;
    while (begin < output.size()) {
        auto end = output.find('\0', begin), tab = output.find('\t', begin);
        if (end == std::string::npos || tab == std::string::npos || tab >= end)
            throw Error("git-format", "Invalid NUL-delimited tree entry");
        std::istringstream fields(output.substr(begin, tab - begin));
        Entry item;
        std::string type, bytes, extra;
        fields >> item.mode >> type >> item.oid >> bytes;
        item.path = output.substr(tab + 1, end - tab - 1);
        if (!fields || fields >> extra || !wanted.count(item.path) || result.count(item.path))
            throw Error("git-format", "Unexpected or duplicate Git tree entry");
        if (type != "blob" || (item.mode != "100644" && item.mode != "100755"))
            throw Error("git-file-mode", "Package files must be regular Git blobs: " + item.path);
        if (!hex(item.oid, 40) && !hex(item.oid, 64))
            throw Error("git-format", "Invalid Git blob object id");
        item.bytes = integer(bytes);
        result[item.path] = std::move(item);
        begin = end + 1;
    }
    return result;
}
struct Pointer {
    std::string oid;
    std::uint64_t bytes = 0;
};
Pointer pointer(const fs::path &file, std::uint64_t size) {
    // Probe just a prefix; ordinary large blobs are never loaded into memory.
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        throw Error("git-io", "Cannot read materialized blob");
    std::string data(static_cast<std::size_t>(std::min<std::uint64_t>(size, 1024)), '\0');
    stream.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!stream)
        throw Error("git-io", "Cannot read complete blob prefix");
    if (data.rfind("version https://git-lfs.github.com/spec/", 0) != 0 &&
        data.rfind("version https://hawser.github.com/spec/", 0) != 0)
        return {};
    if (size >= 1024 || data.empty() || data.back() != '\n' || data.find('\r') != std::string::npos ||
        data.find('\0') != std::string::npos)
        throw Error("lfs-pointer", "Malformed or oversized Git LFS pointer");
    std::istringstream lines(data);
    std::string version, oid, count, extra;
    std::getline(lines, version);
    std::getline(lines, oid);
    std::getline(lines, count);
    if (version != "version https://git-lfs.github.com/spec/v1" &&
        version != "version https://hawser.github.com/spec/v1")
        throw Error("lfs-version", "Unsupported Git LFS pointer version");
    if (oid.rfind("ext-", 0) == 0)
        throw Error("lfs-extension", "LFS pointer extensions need a dedicated content adapter");
    if (oid.rfind("oid sha256:", 0) != 0 || !hex(oid.substr(11), 64) || count.rfind("size ", 0) != 0 ||
        std::getline(lines, extra))
        throw Error("lfs-pointer", "Unsupported or malformed Git LFS pointer fields");
    return {oid.substr(11), integer(count.substr(5))};
}
void budget(std::uint64_t bytes, std::uint64_t &total, const GitOptions &options) {
    if (bytes > options.max_file_bytes || total > options.max_total_bytes ||
        bytes > options.max_total_bytes - total)
        throw Error("git-budget", "Revision exceeds configured file or total byte budget");
    total += bytes;
}
void copy_lfs(const fs::path &source, const fs::path &target, const Pointer &p,
              const OperationControl &control) {
    std::error_code error;
    if (!fs::is_regular_file(source, error))
        throw Error("lfs-missing", "LFS object is not available locally: sha256:" + p.oid);
    if (fs::file_size(source) != p.bytes)
        throw Error("lfs-size", "Local LFS object size differs from pointer: " + p.oid);
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!input || !output)
        throw Error("git-io", "Cannot open local LFS copy streams");
    std::array<char, 65536> block{};
    std::uint64_t copied = 0;
    while (copied < p.bytes) {
        control.checkpoint("Copying local LFS data", copied, p.bytes);
        auto take = static_cast<std::streamsize>(std::min<std::uint64_t>(block.size(), p.bytes - copied));
        input.read(block.data(), take);
        if (input.gcount() != take)
            throw Error("lfs-size", "LFS object truncated during materialization");
        output.write(block.data(), take);
        if (!output)
            throw Error("git-io", "Cannot write materialized LFS content");
        copied += static_cast<std::uint64_t>(take);
    }
    if (input.peek() != std::char_traits<char>::eof())
        throw Error("lfs-size", "LFS object grew during materialization");
    output.close();
    if (!output)
        throw Error("git-io", "Cannot finish materialized LFS content");
    if (sha256_file(target, control) != p.oid)
        throw Error("lfs-hash", "Local LFS object SHA-256 differs from pointer: " + p.oid);
}
std::string resolve_commit(const GitOptions &options, const fs::path &root, const std::string &revision,
                           detail::GitWorkspace &workspace) {
    if (revision.empty() || revision.front() == '-' || revision.size() > 1024 ||
        std::any_of(revision.begin(), revision.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw Error("git-revision", "Invalid commit-ish");
    auto commit = line(detail::git_text(
        options, root, {"rev-parse", "--verify", "--end-of-options", revision + "^{commit}"}, workspace));
    if (!hex(commit, 40) && !hex(commit, 64))
        throw Error("git-revision", "Revision did not resolve to one commit");
    return commit;
}
std::map<std::string, Entry> entries(const GitOptions &options, const fs::path &root,
                                     const std::string &commit, const std::set<std::string> &wanted,
                                     detail::GitWorkspace &workspace) {
    std::vector<std::string> args = {"ls-tree", "--full-tree", "-l", "-z", commit, "--"};
    args.insert(args.end(), wanted.begin(), wanted.end());
    return tree(detail::git_text(options, root, args, workspace), wanted);
}
GitFile extract(const Entry &entry, const GitOptions &options, const fs::path &root,
                detail::GitWorkspace &workspace, const fs::path &target, std::uint64_t &total,
                const std::function<fs::path()> &get_lfs) {
    if (entry.bytes > options.max_file_bytes ||
        (entry.bytes >= 1024 && entry.bytes > options.max_total_bytes - total))
        throw Error("git-budget", "Git blob exceeds configured byte budget");
    auto blob = workspace.directory / "blob";
    auto code = detail::git_process(options, root, {"cat-file", "blob", entry.oid}, blob,
                                    workspace.directory / "stderr", std::max<std::uint64_t>(1, entry.bytes));
    if (code != 0)
        throw Error("git-blob", "Cannot read Git blob: " + entry.oid);
    if (fs::file_size(blob) != entry.bytes)
        throw Error("git-blob", "Git blob length differs from tree metadata");
    auto p = pointer(blob, entry.bytes);
    GitFile file{entry.path, entry.oid, p.oid, entry.bytes, p.oid.empty() ? entry.bytes : p.bytes};
    budget(file.content_bytes, total, options);
    if (p.oid.empty())
        fs::rename(blob, target);
    else {
        auto objects = get_lfs();
        copy_lfs(objects / p.oid.substr(0, 2) / p.oid.substr(2, 2) / p.oid, target, p, options.control);
        fs::remove(blob);
    }
    return file;
}
GitProjectDocument read_document(const GitOptions &options, const fs::path &root, const std::string &commit,
                                 const std::string &path, detail::GitWorkspace &workspace,
                                 std::uint64_t &total, const std::function<fs::path()> &get_lfs) {
    auto found = entries(options, root, commit, {path}, workspace);
    if (found.empty())
        throw Error("git-project-missing", "Descriptor absent from commit: " + path);
    auto target = workspace.directory / "descriptor";
    fs::remove(target);
    GitProjectDocument document;
    document.file = extract(found.at(path), options, root, workspace, target, total, get_lfs);
    std::ifstream stream(target, std::ios::binary);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(document.file.content_bytes));
    stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream || stream.peek() != std::char_traits<char>::eof())
        throw Error("git-project-io", "Descriptor changed during read");
    try {
        document.descriptor = detail::parse_descriptor(std::string(data.begin(), data.end()));
        if (!document.descriptor.is_object())
            throw Error("git-project-format", "Descriptor must be a JSON object");
    } catch (const std::exception &e) {
        document.error = e.what();
        document.descriptor = nullptr;
    }
    return document;
}
} // namespace
Json GitFile::to_json() const {
    Json result = {{"path", path},
                   {"blobOid", blob_oid},
                   {"blobBytes", blob_bytes},
                   {"contentBytes", content_bytes},
                   {"storage", blob_oid.empty() ? "engine-local" : (lfs_oid.empty() ? "git" : "lfs-local")},
                   {"lfsOid", lfs_oid.empty() ? Json(nullptr) : Json(lfs_oid)}};
    if (!content_sha256.empty())
        result["sha256"] = content_sha256;
    return result;
}
fs::path GitRevision::asset_path() const {
    return storage_ && present ? storage_->directory / fs::u8path(repository_path).filename() : fs::path{};
}
Package GitRevision::open(const ReadOptions &options) const {
    if (!present || !storage_)
        throw Error("git-asset-missing", "Asset is absent from this commit: " + repository_path);
    return Package::open(asset_path(), options);
}
Json GitRevision::to_json() const {
    Json entries = Json::array();
    for (const auto &file : files)
        entries.push_back(file.to_json());
    Json result = {{"commit", commit}, {"path", repository_path}, {"present", present}, {"files", entries}};
    if (storage != "git")
        result["storage"] = storage;
    return result;
}
GitRepository GitRepository::open(const fs::path &path, const GitOptions &options) {
    if (!options.max_file_bytes || !options.max_total_bytes || !options.timeout_ms)
        throw Error("git-options", "Git byte budgets and timeout must be nonzero");
    detail::GitWorkspace temp(options.temporary_root);
    GitRepository repo;
    repo.options_ = options;
    auto directory = fs::weakly_canonical(fs::absolute(path));
    if (!fs::is_directory(directory))
        throw Error("git-repository", "Repository directory does not exist");
    auto version = detail::git_text(options, directory, {"--version"}, temp);
    int major = 0, minor = 0;
    char dot = 0;
    std::istringstream numbers(version.rfind("git version ", 0) == 0 ? version.substr(12) : std::string{});
    if (!(numbers >> major >> dot >> minor) || dot != '.' || major < 2 || (major == 2 && minor < 45))
        throw Error("git-version", "Git 2.45 or later is required for local-only object reads");
    auto bare = line(detail::git_text(options, directory, {"rev-parse", "--is-bare-repository"}, temp));
    if (bare == "true")
        repo.root_ =
            fs::u8path(line(detail::git_text(options, directory, {"rev-parse", "--absolute-git-dir"}, temp)));
    else if (bare == "false")
        repo.root_ =
            fs::u8path(line(detail::git_text(options, directory, {"rev-parse", "--show-toplevel"}, temp)));
    else
        throw Error("git-repository", "Cannot determine repository type");
    repo.common_directory_ = fs::u8path(line(detail::git_text(
        options, repo.root_, {"rev-parse", "--path-format=absolute", "--git-common-dir"}, temp)));
    if (!fs::is_directory(repo.common_directory_))
        throw Error("git-repository", "Common Git directory is unavailable");
    return repo;
}
fs::path GitRepository::lfs_objects() const {
    if (!options_.lfs_objects.empty())
        return fs::absolute(options_.lfs_objects);
    detail::GitWorkspace workspace(options_.temporary_root);
    auto config = detail::git_text(options_, root_, {"config", "--path", "--null", "--get", "lfs.storage"},
                                   workspace, true);
    if (config.empty())
        return common_directory_ / "lfs" / "objects";
    if (config.back() != '\0')
        throw Error("git-config", "Invalid LFS storage configuration");
    config.pop_back();
    if (config.empty() || config.find('\0') != std::string::npos)
        throw Error("git-config", "Invalid LFS storage value");
    auto storage = fs::u8path(config);
    if (storage.is_relative())
        storage = common_directory_ / storage;
    return fs::absolute(storage / "objects");
}
GitRevision GitRepository::materialize(const std::string &revision,
                                       const std::string &repository_path) const {
    auto path = valid_path(repository_path);
    GitRevision result;
    result.storage_ = std::make_shared<detail::GitWorkspace>(options_.temporary_root);
    auto &workspace = *result.storage_;
    result.repository_path = repository_path;
    result.commit = resolve_commit(options_, root_, revision, workspace);
    std::set<std::string> wanted = {repository_path};
    for (const auto *ext : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
        auto companion = path;
        companion.replace_extension(ext);
        wanted.insert(companion.generic_u8string());
    }
    auto found = entries(options_, root_, result.commit, wanted, workspace);
    if (!found.count(repository_path))
        return result;
    result.present = true;
    fs::path lfs;
    std::uint64_t total = 0;
    auto get_lfs = [&] {
        if (lfs.empty())
            lfs = lfs_objects();
        return lfs;
    };
    for (const auto &pair : found) {
        auto target = workspace.directory / fs::u8path(pair.first).filename();
        result.files.push_back(extract(pair.second, options_, root_, workspace, target, total, get_lfs));
    }
    return result;
}
GitHistory GitRepository::history(const std::string &revision, const GitHistoryOptions &options) const {
    detail::GitWorkspace workspace(options_.temporary_root);
    const auto commit = resolve_commit(options_, root_, revision, workspace);
    return detail::read_history(options_, root_, commit, options, workspace);
}
AssetCatalog GitRepository::catalog(const std::string &revision, const CatalogOptions &options) const {
    if (!options.max_assets || !options.max_entries || !options.max_path_bytes ||
        !options.max_listing_bytes || options.max_depth > 256)
        throw Error("catalog-options", "Catalogue budgets must be nonzero; depth must not exceed 256");
    AssetCatalog result;
    result.storage = "git";
    auto issue = [&](const std::string &message) {
        result.complete = false;
        if (result.diagnostics.size() < 128)
            result.diagnostics.push_back(message);
    };
    auto cancelled = [&] {
        if (!options.cancelled || !options.cancelled())
            return false;
        result.cancelled = true;
        issue("Catalogue scan cancelled");
        return true;
    };
    if (cancelled())
        return result;
    detail::GitWorkspace workspace(options_.temporary_root);
    result.revision = resolve_commit(options_, root_, revision, workspace);
    auto output = workspace.directory / "catalog";
    if (detail::git_process(options_, root_, {"ls-tree", "--full-tree", "-r", "-l", "-z", result.revision},
                            output, workspace.directory / "stderr", options.max_listing_bytes))
        throw Error("git-catalog", "Cannot enumerate revision assets");
    std::ifstream stream(output, std::ios::binary);
    std::string record;
    while (std::getline(stream, record, '\0')) {
        if (cancelled())
            break;
        if (stream.eof())
            throw Error("git-format", "Unterminated Git tree entry");
        if (result.visited_entries >= options.max_entries) {
            issue("Catalogue entry limit reached");
            break;
        }
        ++result.visited_entries;
        auto tab = record.find('\t');
        if (tab == std::string::npos)
            throw Error("git-format", "Invalid Git tree entry");
        auto name = record.substr(tab + 1);
        if (name.size() > options.max_path_bytes - result.visited_path_bytes) {
            issue("Catalogue path byte limit reached");
            break;
        }
        result.visited_path_bytes += name.size();
        auto extension = fs::u8path(name).extension().u8string();
        if (extension != ".uasset" && extension != ".umap")
            continue;
        std::istringstream fields(record.substr(0, tab));
        std::string mode, type, oid, bytes, extra;
        fields >> mode >> type >> oid >> bytes;
        if (!fields || fields >> extra || (!hex(oid, 40) && !hex(oid, 64)))
            throw Error("git-format", "Invalid Git tree metadata");
        if (type != "blob" || (mode != "100644" && mode != "100755")) {
            issue("Non-regular package skipped: " + name);
            continue;
        }
        try {
            valid_path(name);
        } catch (const Error &) {
            issue("Non-portable package path skipped: " + name);
            continue;
        }
        if (static_cast<std::uint32_t>(std::count(name.begin(), name.end(), '/')) > options.max_depth) {
            issue("Catalogue depth limit reached: " + name);
            continue;
        }
        if (result.assets.size() >= options.max_assets) {
            issue("Catalogue asset limit reached");
            break;
        }
        result.assets.push_back({name, {}, {}, extension, oid, integer(bytes)});
    }
    if (stream.bad())
        throw Error("git-io", "Cannot read Git asset listing");
    std::sort(result.assets.begin(), result.assets.end(),
              [](const auto &a, const auto &b) { return a.path < b.path; });
    return result;
}
GitProjectContext GitRepository::project(const std::string &revision, const std::string &project_path,
                                         const GitProjectOptions &project_options) const {
    auto path = valid_repository_path(project_path);
    if (path.extension() != ".uproject")
        throw Error("git-project-path", "Expected repository-relative .uproject path");
    if (!project_options.max_plugins || !project_options.max_tree_entries ||
        !project_options.max_tree_bytes || !project_options.max_descriptor_bytes ||
        !project_options.max_descriptor_total_bytes)
        throw Error("git-project-options", "Project discovery limits must be nonzero");
    detail::GitWorkspace workspace(options_.temporary_root);
    auto commit = resolve_commit(options_, root_, revision, workspace);
    auto limits = options_;
    limits.max_file_bytes = std::min(limits.max_file_bytes, project_options.max_descriptor_bytes);
    limits.max_total_bytes = std::min(limits.max_total_bytes, project_options.max_descriptor_total_bytes);
    std::uint64_t total = 0;
    fs::path lfs;
    auto get_lfs = [&] {
        if (lfs.empty())
            lfs = lfs_objects();
        return lfs;
    };
    std::vector<GitProjectDocument> documents;
    documents.push_back(read_document(limits, root_, commit, project_path, workspace, total, get_lfs));
    if (!documents[0].error.empty())
        throw Error("git-project-format", project_path + ": " + documents[0].error);
    bool complete = true;
    std::vector<std::string> diagnostics;
    auto directories =
        detail::plugin_directories(documents[0].descriptor, project_path, complete, diagnostics);
    std::set<std::string> candidates;
    if (!directories.empty()) {
        std::vector<std::string> args = {"ls-tree", "--full-tree", "-r", "--name-only", "-z", commit, "--"};
        for (const auto &directory : directories) {
            valid_repository_path(directory);
            args.push_back(directory);
        }
        auto output = workspace.directory / "tree";
        if (detail::git_process(options_, root_, args, output, workspace.directory / "stderr",
                                project_options.max_tree_bytes))
            throw Error("git-project-tree", "Cannot enumerate historical plugin descriptors");
        std::ifstream stream(output, std::ios::binary);
        std::string name;
        std::uint32_t count = 0;
        while (std::getline(stream, name, '\0')) {
            if (stream.eof())
                throw Error("git-format", "Unterminated Git tree path");
            if (++count > project_options.max_tree_entries)
                throw Error("git-project-budget", "Historical tree entry limit exceeded");
            if (fs::u8path(name).extension() != ".uplugin")
                continue;
            valid_repository_path(name);
            bool within = false;
            for (const auto &directory : directories)
                if (name.rfind(directory + "/", 0) == 0)
                    within = true;
            if (!within)
                throw Error("git-project-path", "Plugin descriptor escapes historical search roots");
            candidates.insert(name);
            if (candidates.size() > project_options.max_plugins)
                throw Error("git-project-budget", "Plugin descriptor limit exceeded");
        }
        if (!stream.eof())
            throw Error("git-io", "Cannot read historical tree listing");
    }
    for (const auto &candidate : candidates)
        documents.push_back(read_document(limits, root_, commit, candidate, workspace, total, get_lfs));
    return detail::build_git_project(commit, project_path, std::move(documents), project_options, complete,
                                     std::move(diagnostics));
}
GitRevision GitRepository::materialize(const GitProjectContext &context, const std::string &input) const {
    if (input.empty() || input.front() != '/')
        return materialize(context.commit, input);
    auto location = context.locate(input);
    if (location.storage == "git") {
        detail::GitWorkspace workspace(options_.temporary_root);
        auto a = location.path + ".uasset", b = location.path + ".umap";
        auto found = entries(options_, root_, resolve_commit(options_, root_, context.commit, workspace),
                             {a, b}, workspace);
        if (found.size() > 1)
            throw Error("git-project-ambiguous", "Both .uasset and .umap exist for package: " + input);
        return materialize(context.commit, found.empty() ? a : found.begin()->first);
    }
    if (location.storage != "engine-local")
        throw Error("git-project-mount", "Unsupported content storage");
    auto base = fs::u8path(location.path), asset = base;
    asset += ".uasset";
    auto map = base;
    map += ".umap";
    if (fs::is_regular_file(asset) && fs::is_regular_file(map))
        throw Error("git-project-ambiguous", "Ambiguous local engine package: " + input);
    if (!fs::is_regular_file(asset) && fs::is_regular_file(map))
        asset = map;
    GitRevision result;
    result.commit = context.commit;
    result.storage = "engine-local";
    result.repository_path = input + asset.extension().u8string();
    if (!fs::is_regular_file(asset))
        return result;
    result.present = true;
    result.storage_ = std::make_shared<detail::GitWorkspace>(options_.temporary_root);
    std::vector<fs::path> sources = {asset};
    for (const auto *ext : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
        auto file = asset;
        file.replace_extension(ext);
        if (fs::is_regular_file(file))
            sources.push_back(file);
    }
    std::uint64_t total = 0;
    auto engine_base = fs::weakly_canonical(context.engine.root / "Engine");
    for (const auto &source : sources) {
        auto resolved = fs::weakly_canonical(source);
        auto relative = resolved.lexically_relative(engine_base);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw Error("package-path", "Engine source escapes selected installation");
        auto size = fs::file_size(source);
        budget(size, total, options_);
        auto hash = sha256_file(source, options_.control);
        try {
            copy_lfs(source, result.storage_->directory / source.filename(), {hash, size}, options_.control);
        } catch (const Error &e) {
            throw Error("engine-source", e.what());
        }
        result.files.push_back({source.u8string(), "", "", size, size, hash});
    }
    return result;
}
std::string GitComparison::status() const {
    if (!diff.complete_input || (!before_context.is_null() && !before_context.value("complete", false)) ||
        (!after_context.is_null() && !after_context.value("complete", false)))
        return "incomplete";
    if (!before.present)
        return "added";
    if (!after.present)
        return "removed";
    if (!context_changes.empty() && diff.status() == "identical")
        return "changed";
    return diff.status();
}
Json GitComparison::to_json() const {
    Json result = {{"schema", "cyuediff.git-diff/1"},
                   {"status", status()},
                   {"before", before.to_json()},
                   {"after", after.to_json()},
                   {"comparison", diff.to_json()}};
    if (!before_context.is_null() || !after_context.is_null()) {
        result["projectContexts"] = {{"before", before_context}, {"after", after_context}};
        result["contextChanges"] = context_changes;
    }
    return result;
}
GitComparison compare_git(const GitRepository &repo, const std::string &before_revision,
                          const std::string &after_revision, const std::string &before_path,
                          const std::string &after_path, const ReadOptions &options) {
    if (!options.properties || !options.fingerprints)
        throw Error("git-options", "Git comparison requires properties and fingerprints");
    GitComparison result;
    result.before = repo.materialize(before_revision, before_path);
    result.after = repo.materialize(after_revision, after_path.empty() ? before_path : after_path);
    if (!result.before.present && !result.after.present)
        throw Error("git-asset-missing", "Package is absent from both commits");
    if (result.before.present)
        result.before_package = std::make_shared<Package>(result.before.open(options));
    if (result.after.present)
        result.after_package = std::make_shared<Package>(result.after.open(options));
    Package absent;
    absent.fingerprints = {{"absent", true}};
    result.diff = compare(result.before_package ? *result.before_package : absent,
                          result.after_package ? *result.after_package : absent, options.control);
    return result;
}
GitComparison compare_git(const GitRepository &repo, const GitProjectContext &before,
                          const GitProjectContext &after, const std::string &before_path,
                          const std::string &after_path, const ReadOptions &options) {
    if (!options.properties || !options.fingerprints)
        throw Error("git-options", "Git comparison requires properties and fingerprints");
    GitComparison result;
    result.before = repo.materialize(before, before_path);
    result.after = repo.materialize(after, after_path.empty() ? before_path : after_path);
    if (!result.before.present && !result.after.present)
        throw Error("git-asset-missing", "Package is absent from both contexts");
    if (result.before.present)
        result.before_package = std::make_shared<Package>(result.before.open(options));
    if (result.after.present)
        result.after_package = std::make_shared<Package>(result.after.open(options));
    Package absent;
    absent.fingerprints = {{"absent", true}};
    result.diff = compare(result.before_package ? *result.before_package : absent,
                          result.after_package ? *result.after_package : absent, options.control);
    result.before_context = before.to_json();
    result.after_context = after.to_json();
    auto a = before.semantic_snapshot(), b = after.semantic_snapshot();
    for (const auto &patch : Json::diff(a, b)) {
        Json change = {{"op", patch.at("op")}, {"path", patch.at("path")}};
        auto pointer = Json::json_pointer(patch.at("path").get<std::string>());
        if (patch.at("op") != "add")
            change["before"] = a.at(pointer);
        if (patch.at("op") != "remove")
            change["after"] = patch.at("value");
        result.context_changes.push_back(std::move(change));
    }
    return result;
}
} // namespace cyuediff
