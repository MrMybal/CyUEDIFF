#pragma once
#include <cyuediff/core.hpp>
#include <cyuediff/catalog.hpp>
#include <memory>
namespace cyuediff {
namespace detail {
struct GitWorkspace;
}
struct GitOptions {
    fs::path executable = "git";
    fs::path temporary_root; // Empty: system temporary directory.
    fs::path lfs_objects;    // Optional explicit objects directory; not the parent lfs directory.
    std::uint64_t max_file_bytes = 2ull * 1024 * 1024 * 1024;
    std::uint64_t max_total_bytes = 4ull * 1024 * 1024 * 1024;
    std::uint32_t timeout_ms = 30'000; // Per Git process.
    OperationControl control;
};
struct GitHistoryOptions {
    std::uint32_t max_commits = 100, skip = 0;
    std::uint64_t max_bytes = 8 * 1024 * 1024;
};
struct GitCommit {
    std::string id, author, authored_at, subject;
    std::vector<std::string> parents;
    Json to_json() const;
};
struct GitHistory {
    std::string head;
    std::uint32_t skip = 0, next_skip = 0;
    bool has_more = false;
    std::vector<GitCommit> commits;
    Json to_json() const;
};
struct GitFile {
    std::string path, blob_oid, lfs_oid;
    std::uint64_t blob_bytes = 0, content_bytes = 0;
    std::string content_sha256;
    Json to_json() const;
};
struct GitProjectOptions {
    fs::path engine_root;
    bool discover_engine = true, engine_plugins = true;
    std::uint32_t max_plugins = 2048, max_tree_entries = 200'000;
    std::uint64_t max_descriptor_bytes = 4ull * 1024 * 1024, max_descriptor_total_bytes = 32ull * 1024 * 1024,
                  max_tree_bytes = 32ull * 1024 * 1024;
};
struct GitProjectDocument {
    GitFile file;
    Json descriptor = nullptr;
    std::string error;
    Json to_json() const;
};
struct GitContentMount {
    std::string name, storage, directory, descriptor;
    Json to_json() const;
};
struct GitPackageLocation {
    std::string storage, path; // Repository-relative or absolute local engine path, without extension.
    Json to_json() const;
};
struct GitProjectContext {
    std::string commit, project_path, engine_association;
    EngineInfo engine;
    Json engine_build = nullptr;
    std::vector<GitProjectDocument> documents;
    std::vector<GitContentMount> mounts;
    std::vector<std::string> diagnostics;
    bool complete = true;
    // A catalogue of content roots, not an evaluation of Unreal's target/plugin activation rules.
    GitPackageLocation locate(const std::string &long_package_name) const;
    Json semantic_snapshot() const;
    Json to_json() const;
};
class GitRevision {
    std::shared_ptr<detail::GitWorkspace> storage_;
    fs::path materialized_file_;
    friend class GitRepository;

  public:
    std::string commit, repository_path;
    bool present = false;
    std::vector<GitFile> files;
    std::string storage = "git";
    // Keep this revision alive while a Package or a preview uses its payload files.
    fs::path asset_path() const;
    fs::path file_path() const;
    Package open(const ReadOptions &options = {}) const;
    Json to_json() const;
};
class GitRepository {
    fs::path root_, common_directory_;
    GitOptions options_;

  public:
    static GitRepository open(const fs::path &, const GitOptions &options = {});
    const fs::path &root() const { return root_; }
    const fs::path &common_directory() const { return common_directory_; }
    fs::path lfs_objects() const;
    // Newest-first topological history. Pass the returned head for stable subsequent pages.
    GitHistory history(const std::string &revision = "HEAD", const GitHistoryOptions &options = {}) const;
    // Repository-wide metadata listing. Does not materialize blobs or inspect LFS content.
    AssetCatalog catalog(const std::string &revision, const CatalogOptions &options = {}) const;
    // Commit-ish resolves once to an immutable commit. No checkout, filters or fetch.
    GitRevision materialize(const std::string &revision, const std::string &repository_path) const;
    // Exact single portable repository file, including .ubulk and arbitrary binary extensions; no companions.
    GitRevision materialize_file(const std::string &revision, const std::string &repository_path,
                                 std::uint64_t max_bytes = 2ull * 1024 * 1024 * 1024,
                                 const OperationControl & = {}) const;
    GitProjectContext project(const std::string &revision, const std::string &project_path,
                              const GitProjectOptions &options = {}) const;
    GitRevision materialize(const GitProjectContext &, const std::string &package_name_or_path) const;
};
struct GitComparison {
    GitRevision before, after;
    std::shared_ptr<const Package> before_package, after_package;
    DiffResult diff;
    Json before_context = nullptr, after_context = nullptr;
    Json context_changes = Json::array();
    std::string status() const;
    Json to_json() const;
    void write_json(std::ostream &, int indent = 2, const OperationControl & = {}) const;
};
GitComparison compare_git(const GitRepository &, const std::string &before_revision,
                          const std::string &after_revision, const std::string &before_path,
                          const std::string &after_path = "", const ReadOptions &options = {});
GitComparison compare_git(const GitRepository &, const GitProjectContext &before,
                          const GitProjectContext &after, const std::string &before_path,
                          const std::string &after_path = "", const ReadOptions &options = {});
// Independent serializers for before/after engine or plugin wire profiles.
GitComparison compare_git(const GitRepository &, const std::string &before_revision,
                          const std::string &after_revision, const std::string &before_path,
                          const std::string &after_path, const ReadOptions &before_options,
                          const ReadOptions &after_options);
GitComparison compare_git(const GitRepository &, const GitProjectContext &before,
                          const GitProjectContext &after, const std::string &before_path,
                          const std::string &after_path, const ReadOptions &before_options,
                          const ReadOptions &after_options);
} // namespace cyuediff
