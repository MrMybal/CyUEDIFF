#include "../src/git_process.hpp"
#include "package_fixture.hpp"
#include <cyuediff/decoders.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const Error &e) {
        check(e.code == code, ("Expected " + code + ", got " + e.code + ": " + e.what()).c_str());
        return;
    }
    throw std::runtime_error("Expected error: " + code);
}
void write(const fs::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    if (!file)
        throw std::runtime_error("Cannot write test file");
}
void bytes(const fs::path &path, const Bytes &b) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(b.data.data()), static_cast<std::streamsize>(b.data.size()));
}
std::string lfs(const fs::path &path, const Bytes &b, const fs::path &objects) {
    bytes(path, b);
    auto oid = sha256_file(path);
    auto folder = objects / oid.substr(0, 2) / oid.substr(2, 2);
    fs::create_directories(folder);
    fs::copy_file(path, folder / oid, fs::copy_options::overwrite_existing);
    write(path, "version https://git-lfs.github.com/spec/v1\noid sha256:" + oid + "\nsize " +
                    std::to_string(b.data.size()) + "\n");
    return oid;
}
} // namespace
int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--fake-timeout") {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            return 0;
        }
        if (arg == "--fake-output") {
            std::cout << std::string(1024 * 1024, 'x');
            return 0;
        }
        if (arg == "--fake-argument" && i + 1 < argc) {
            std::cout << argv[i + 1];
            return 0;
        }
        if (arg == "--fake-environment") {
            std::cout << (std::getenv("GIT_DIR") ? "wrong" : "clean") << ":"
                      << (std::getenv("GIT_NO_LAZY_FETCH") ? std::getenv("GIT_NO_LAZY_FETCH") : "missing");
            return 0;
        }
    }
    auto parent = fs::weakly_canonical(fs::current_path());
    auto root = parent / (".test-tmp-git-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    auto cleanup = [&] {
        auto absolute = fs::weakly_canonical(fs::absolute(root));
        if (absolute.parent_path() == parent &&
            absolute.filename().u8string().rfind(".test-tmp-git-", 0) == 0)
            fs::remove_all(absolute);
    };
    try {
        auto temporary = root / "temporary";
        fs::create_directory(temporary);
        GitOptions options;
        if (argc == 3 && std::string(argv[1]) == "--git")
            options.executable = fs::u8path(argv[2]);
        options.temporary_root = temporary;
        detail::GitWorkspace helper(temporary);
        auto repo_path = root / fs::u8path("répo [local] avec espace");
        fs::create_directory(repo_path);
        auto git = [&](const std::vector<std::string> &args) {
            return detail::git_text(options, repo_path, args, helper);
        };
        git({"init", "--quiet"});
        git({"config", "user.name", "CyUEDiff tests"});
        git({"config", "user.email", "tests@invalid.example"});
        git({"config", "core.autocrlf", "false"});
        git({"config", "commit.gpgSign", "false"});
        // Override global hooks only in this disposable repository.
        git({"config", "core.hooksPath", "nonexistent-hooks"});
        git({"commit", "--quiet", "--allow-empty", "-m", "empty"});
        auto commit = [&] {
            auto id = git({"rev-parse", "HEAD"});
            while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
                id.pop_back();
            return id;
        };
        auto empty = commit();
        fs::create_directory(repo_path / "Content");
        auto asset = save(repo_path / "Content", "Asset [x] é", fixture(1012, 1), true);
        auto exports = asset;
        exports.replace_extension(".uexp");
        auto optional = asset;
        optional.replace_extension(".uptnl");
        write(optional, "optional before");
        auto payload = asset;
        payload.replace_extension(".upayload");
        write(payload, "bulk before");
        auto objects = repo_path / ".git" / "lfs" / "objects";
        auto before_oid = lfs(exports, fixture(1012, 1).script, objects);
        git({"add", "--", "Content"});
        git({"commit", "--quiet", "-m", "before"});
        auto before = commit();
        auto after_oid = lfs(exports, fixture(1012, 2).script, objects);
        fs::remove(optional);
        write(payload, "bulk after");
        git({"add", "--all"});
        git({"commit", "--quiet", "-m", "after"});
        auto after = commit();
        const std::string path = "Content/Asset [x] é.uasset";
        auto repo = GitRepository::open(repo_path, options);
        check(fs::equivalent(repo.root(), repo_path), "Repository root with Unicode, brackets and spaces");
        check(fs::equivalent(GitRepository::open(repo_path / "Content", options).root(), repo_path),
              "Open from subdirectory");
        auto index_before = sha256_file(repo_path / ".git" / "index");
        auto head_before = sha256_file(repo_path / ".git" / "HEAD");
        write(asset, "dirty worktree data must never be read");
        auto worktree_before = sha256_file(asset);
        {
            auto a = repo.materialize(before, path), b = repo.materialize(after, path);
            auto a_path = a.asset_path();
            check(a.present && b.present && a.commit == before && b.commit == after,
                  "Immutable commit identities");
            check(a.files.size() == 4 && b.files.size() == 3, "Each revision has its own companions");
            check(a.open().exports[0].properties["Value"]["value"] == 1 &&
                      b.open().exports[0].properties["Value"]["value"] == 2,
                  "LFS .uexp data from the correct commits");
            AssetDecoder decoder;
            decoder.id = "test.git-native/1";
            decoder.kind = DecoderKind::native_export;
            decoder.target = "/Script/CoreUObject.DataAsset";
            decoder.profile.ue5 = {1012, 1012};
            decoder.decode = [](DecoderInput &input, const DecoderContext &) -> Json {
                return {{"counter", input.get<std::int32_t>()}};
            };
            ReadOptions read_options;
            read_options.decoders =
                std::make_shared<const DecoderRegistry>(std::vector<AssetDecoder>{decoder});
            auto extension = compare_git(repo, before, after, path, "", read_options);
            check(extension.before_package->exports[0].native_coverage == "extension" &&
                      extension.after_package->exports[0].native_data["decoder"] == "test.git-native/1",
                  "Host decoders propagate through Git/LFS comparisons");
            bool found = false;
            for (const auto &f : a.files)
                if (f.lfs_oid == before_oid)
                    found = f.content_bytes == fixture(1012, 1).script.data.size();
            check(found, "LFS provenance and verified content size");
            check(a_path.parent_path() != b.asset_path().parent_path(), "Isolated revision directories");
            auto copy = a;
            a = GitRevision{};
            check(fs::exists(a_path), "Revision copies retain payload ownership");
            copy = GitRevision{};
            check(!fs::exists(a_path), "Last revision owner cleans temporary content");
            auto result = compare_git(repo, before, after, path);
            check(result.status() == "changed" && result.diff.complete_input, "Git comparison status");
            bool value_change = false;
            for (const auto &c : result.diff.changes)
                if (c["path"] == "/objects/TestAsset/properties/Value/value")
                    value_change = c["before"] == 1 && c["after"] == 2;
            check(value_change, "Semantic diff through Git and LFS");
            check(compare_git(repo, before, before, path).status() == "identical", "Same revision equality");
            check(compare_git(repo, empty, before, path).status() == "added", "Package addition");
            check(compare_git(repo, before, empty, path).status() == "removed", "Package removal");
            check(!repo.materialize(empty, path).present, "Absent package is explicit");
            rejects([&] { compare_git(repo, empty, empty, path); }, "git-asset-missing");
            auto missing = repo.materialize(before, "Missing.uasset");
            rejects([&] { missing.open(); }, "git-asset-missing");
            check(result.to_json()["before"]["files"].size() == 4, "Manifest serialization");
            check(result.before_package && result.after_package, "Host can pass loaded packages to CyUI");
        }
        check(sha256_file(repo_path / ".git" / "index") == index_before &&
                  sha256_file(repo_path / ".git" / "HEAD") == head_before &&
                  sha256_file(asset) == worktree_before,
              "Index, HEAD and dirty checkout unchanged");
        for (const auto &bad : std::vector<std::string>{"../Asset.uasset", "/Asset.uasset", "C:/Asset.uasset",
                                                        "Content//Asset.uasset", "Content/../Asset.uasset",
                                                        "Content/Asset?.uasset", "Content/CON.uasset",
                                                        "Content/Asset.txt", std::string("A\0B.uasset", 10)})
            rejects([&] { repo.materialize(before, bad); }, "git-path");
        rejects([&] { repo.materialize("--help", path); }, "git-revision");
        rejects([&] { repo.materialize("not-a-real-revision", path); }, "git-command");
        git({"tag", "release-before", before});
        check(repo.materialize("release-before", path).commit == before, "Tag resolution");
        check(repo.materialize("HEAD^", path).commit == before, "Commit-ish resolution");
        git({"replace", before, after});
        check(repo.materialize(before, path).open().exports[0].properties["Value"]["value"] == 1,
              "Replace refs cannot substitute historical contents");
        git({"replace", "--delete", before});
        ReadOptions metadata;
        metadata.properties = false;
        rejects([&] { compare_git(repo, before, after, path, "", metadata); }, "git-options");
        // A missing or corrupt local LFS object must never fall back to the dirty checkout.
        auto object = objects / before_oid.substr(0, 2) / before_oid.substr(2, 2) / before_oid;
        auto original_size = fs::file_size(object);
        fs::rename(object, object.string() + ".saved");
        rejects([&] { repo.materialize(before, path); }, "lfs-missing");
        write(object, std::string(static_cast<std::size_t>(original_size), 'x'));
        rejects([&] { repo.materialize(before, path); }, "lfs-hash");
        write(object, "short");
        rejects([&] { repo.materialize(before, path); }, "lfs-size");
        fs::remove(object);
        fs::rename(object.string() + ".saved", object);
        auto limited = options;
        limited.max_file_bytes = 32;
        auto tiny = GitRepository::open(repo_path, limited);
        rejects([&] { tiny.materialize(before, path); }, "git-budget");
        limited = options;
        limited.max_total_bytes = 1;
        auto total = GitRepository::open(repo_path, limited);
        rejects([&] { total.materialize(before, path); }, "git-budget");
        auto custom = repo_path / ".git" / "custom-lfs" / "objects";
        fs::create_directories(custom);
        fs::copy(objects, custom, fs::copy_options::recursive);
        git({"config", "lfs.storage", "custom-lfs"});
        check(repo.materialize(before, path).open().complete_input, "Relative lfs.storage");
        check(fs::equivalent(repo.lfs_objects(), custom), "Configured LFS storage is discoverable by hosts");
        auto worktree = root / "linked worktree";
        git({"worktree", "add", "--quiet", "--detach", "--no-checkout", worktree.u8string(), before});
        check(GitRepository::open(worktree, options).materialize(before, path).open().complete_input,
              "Linked worktree uses common LFS storage");
        auto explicit_options = options;
        explicit_options.lfs_objects = objects;
        check(
            GitRepository::open(repo_path, explicit_options).materialize(before, path).open().complete_input,
            "Explicit LFS objects directory");
        // Restore the deliberately dirty header before creating the next fixture commit.
        bytes(asset, fixture(1012, 2).header);
        auto renamed_asset = asset.parent_path() / "Renamed.uasset";
        auto renamed_exports = renamed_asset;
        renamed_exports.replace_extension(".uexp");
        auto renamed_payload = renamed_asset;
        renamed_payload.replace_extension(".upayload");
        for (const auto &pair : std::vector<std::pair<fs::path, fs::path>>{
                 {asset, renamed_asset}, {exports, renamed_exports}, {payload, renamed_payload}})
            git({"mv", "--", pair.first.u8string(), pair.second.u8string()});
        git({"commit", "--quiet", "-m", "rename package group"});
        auto renamed = commit();
        check(compare_git(repo, after, renamed, path, "Content/Renamed.uasset").status() == "identical",
              "Explicit before/after paths preserve content equality through a rename");
        check(!repo.materialize(renamed, path).present, "Old path is absent after rename");
        for (const auto &pair : std::vector<std::pair<fs::path, fs::path>>{
                 {renamed_asset, asset}, {renamed_exports, exports}, {renamed_payload, payload}})
            git({"mv", "--", pair.first.u8string(), pair.second.u8string()});
        git({"commit", "--quiet", "-m", "restore path"});
        // Make a revision with a missing required .uexp and retain incomplete status.
        fs::remove(exports);
        git({"add", "--all"});
        git({"commit", "--quiet", "-m", "missing exports"});
        auto incomplete = commit();
        check(compare_git(repo, incomplete, incomplete, path).status() == "incomplete",
              "Missing companion cannot establish equality");
        // A tracked symbolic link must not be materialized as an asset.
        auto blob = git({"hash-object", "-w", asset.u8string()});
        while (!blob.empty() && (blob.back() == '\n' || blob.back() == '\r'))
            blob.pop_back();
        git({"update-index", "--add", "--cacheinfo", "120000," + blob + ",Link.uasset"});
        git({"commit", "--quiet", "-m", "link"});
        auto link_commit = commit();
        rejects([&] { repo.materialize(link_commit, "Link.uasset"); }, "git-file-mode");
        // Pointer extensions and malformed pointers remain explicit errors.
        write(repo_path / "Extension.uasset",
              "version https://git-lfs.github.com/spec/v1\next-0-test sha256:" + before_oid +
                  "\noid sha256:" + before_oid + "\nsize 1\n");
        write(repo_path / "Malformed.uasset",
              "version https://git-lfs.github.com/spec/v1\noid sha256:bad\nsize 1\n");
        git({"add", "--all"});
        git({"commit", "--quiet", "-m", "pointer errors"});
        auto pointers = commit();
        rejects([&] { repo.materialize(pointers, "Extension.uasset"); }, "lfs-extension");
        rejects([&] { repo.materialize(pointers, "Malformed.uasset"); }, "lfs-pointer");
        // A bare repository reads the same objects without needing a checkout.
        auto bare = root / "bare.git";
        git({"clone", "--quiet", "--bare", repo_path.u8string(), bare.u8string()});
        check(GitRepository::open(bare, explicit_options).materialize(before, path).open().complete_input,
              "Bare repository with explicit shared LFS cache");
        auto sha_repo = root / "sha256 repository";
        git({"init", "--quiet", "--object-format=sha256", sha_repo.u8string()});
        save(sha_repo, "Asset", fixture(1012));
        detail::git_text(options, sha_repo, {"add", "--", "Asset.uasset"}, helper);
        detail::git_text(options, sha_repo,
                         {"-c", "user.name=CyUEDiff", "-c", "user.email=tests@invalid.example", "-c",
                          "commit.gpgSign=false", "-c", "core.hooksPath=nonexistent-hooks", "commit",
                          "--quiet", "-m", "SHA256"},
                         helper);
        auto sha_revision = GitRepository::open(sha_repo, options).materialize("HEAD", "Asset.uasset");
        check(sha_revision.commit.size() == 64 && sha_revision.open().complete_input,
              "SHA-256 Git object format");
        auto fake = options;
        fake.executable = fs::absolute(fs::u8path(argv[0]));
        const std::string quoted = "A \"quoted\" C:\\folder\\final\\";
        check(detail::git_text(fake, repo_path, {"--fake-argument", quoted}, helper) == quoted,
              "Direct process arguments preserve quotes and trailing backslashes");
        fake.timeout_ms = 100;
        rejects([&] { detail::git_text(fake, repo_path, {"--fake-timeout"}, helper); }, "git-timeout");
        auto output = helper.directory / "stdout", errors = helper.directory / "stderr";
        rejects([&] { detail::git_process(fake, repo_path, {"--fake-output"}, output, errors, 32); },
                "git-output");
        check(detail::git_text(fake, repo_path, {"--fake-environment"}, helper) == "clean:1",
              "Child environment isolates Git context and disables lazy fetch");
        std::cout << checks << " Git/LFS checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << "Git test failure: " << e.what() << "\nPreserved test workspace: " << root.u8string()
                  << '\n';
        return 1;
    }
    cleanup();
}
