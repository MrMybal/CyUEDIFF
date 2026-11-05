#include "../src/git_process.hpp"
#include <cyuediff/git_binary_delta.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
int main(int argc, char **argv) {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Git binary delta check failed");
    };
    auto rejects = [&](auto action, const char *code) {
        bool failed = false;
        try {
            action();
        } catch (const Error &e) {
            failed = e.code == code;
        }
        check(failed);
    };
    const auto root =
        fs::current_path() / (".test-tmp-git-binary-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root / "repo/Content");
        fs::create_directory(root / "temporary");
        {
            GitOptions git;
            if (argc == 3 && std::string(argv[1]) == "--git")
                git.executable = fs::u8path(argv[2]);
            git.temporary_root = root / "temporary";
            detail::GitWorkspace helper(git.temporary_root);
            const auto directory = root / "repo";
            auto command = [&](const std::vector<std::string> &args) {
                auto text = detail::git_text(git, directory, args, helper);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                    text.pop_back();
                return text;
            };
            auto write = [](const fs::path &p, const std::vector<std::uint8_t> &v) {
                std::ofstream f(p, std::ios::binary);
                f.write(reinterpret_cast<const char *>(v.data()), v.size());
            };
            auto text = [&](const fs::path &p, const std::string &v) { write(p, {v.begin(), v.end()}); };
            command({"init", "--quiet"});
            for (const auto &kv : {std::make_pair("user.name", "CyUEDiff tests"),
                                   {"user.email", "tests@invalid.example"},
                                   {"core.autocrlf", "false"},
                                   {"core.hooksPath", "nonexistent-hooks"},
                                   {"commit.gpgSign", "false"}})
                command({"config", kv.first, kv.second});
            std::vector<std::uint8_t> base(4096);
            for (std::size_t i = 0; i < base.size(); ++i)
                base[i] = std::uint8_t(i * 37 + i / 256);
            auto target = base;
            target.insert(target.begin() + 71, {1, 2, 3, 4});
            write(directory / "Content/Blob.ubulk", base);
            write(directory / "Content/Empty.dat", {});
            write(directory / "Content/Old.bin", base);
            write(directory / "Content/blob", base);
            write(directory / "Content/stderr", base);
            command({"add", "Content"});
            command({"commit", "--quiet", "-m", "before"});
            const auto before = command({"rev-parse", "HEAD"});
            const auto oid = sha256_bytes(target);
            const auto object = directory / ".git/lfs/objects" / oid.substr(0, 2) / oid.substr(2, 2) / oid;
            fs::create_directories(object.parent_path());
            write(object, target);
            text(directory / "Content/Blob.ubulk",
                 "version https://git-lfs.github.com/spec/v1\noid sha256:" + oid + "\nsize " +
                     std::to_string(target.size()) + "\n");
            command({"mv", "Content/Old.bin", "Content/New.bin"});
            write(directory / "Content/New.bin", target);
            command({"add", "Content"});
            command({"commit", "--quiet", "-m", "after"});
            const auto after = command({"rev-parse", "HEAD"});
            text(directory / "Content/Blob.ubulk", "invalid working copy");
            const auto index_hash = sha256_file(directory / ".git/index"),
                       status = command({"status", "--porcelain=v1"});
            const auto repo = GitRepository::open(directory, git);
            const auto revision = repo.materialize_file(before, "Content/Blob.ubulk");
            check(revision.present && revision.files.size() == 1 &&
                  read_binary_file(revision.file_path(), 10000) == base);
            check(read_binary_file(repo.materialize_file(before, "Content/blob").file_path(), 10000) == base);
            check(read_binary_file(repo.materialize_file(before, "Content/stderr").file_path(), 10000) ==
                  base);
            BinaryDeltaOptions o;
            o.block_size = 32;
            const auto result = compare_git_binary_files(repo, before, after, "Content/Blob.ubulk", "", o);
            check(result.status() == "changed" && apply_binary_delta(base, result.delta, o) == target);
            check(result.before.commit == before && result.after.commit == after &&
                  result.after.files.front().lfs_oid == oid);
            check(result.summary(o)["delta"]["copiedBytes"].get<std::size_t>() > 4000);
            check(result.to_json(o)["delta"] == result.delta.to_json(o));
            const auto renamed =
                compare_git_binary_files(repo, before, after, "Content/Old.bin", "Content/New.bin", o);
            check(renamed.status() == "changed" && apply_binary_delta(base, renamed.delta, o) == target);
            const auto added =
                compare_git_binary_files(repo, before, after, "Content/Missing.bin", "Content/Blob.ubulk", o);
            check(added.status() == "added" && apply_binary_delta({}, added.delta, o) == target);
            const auto removed =
                compare_git_binary_files(repo, before, after, "Content/Blob.ubulk", "Content/Missing.bin", o);
            check(removed.status() == "removed" && apply_binary_delta(base, removed.delta, o).empty());
            check(compare_git_binary_files(repo, before, after, "Content/Missing.bin", "", o).status() ==
                  "absent");
            check(compare_git_binary_files(repo, before, after, "Content/Empty.dat", "", o).status() ==
                  "identical");
            rejects([&] { repo.materialize_file(before, "../escape.bin"); }, "git-path");
            rejects([&] { repo.materialize_file(before, "Content/Blob.ubulk", 1); }, "git-budget");
            OperationControl cancelled;
            cancelled.cancelled = [] { return true; };
            rejects([&] { repo.materialize_file(before, "Content/Blob.ubulk", 10000, cancelled); },
                    "cancelled");
            auto cancel = o;
            cancel.control = cancelled;
            rejects([&] { compare_git_binary_files(repo, before, after, "Content/Blob.ubulk", "", cancel); },
                    "cancelled");
            check(sha256_file(directory / ".git/index") == index_hash &&
                  command({"status", "--porcelain=v1"}) == status);
            check(sha256_file(object) == oid);
            auto wrong = target;
            wrong[0] ^= 1;
            write(object, wrong);
            rejects([&] { repo.materialize_file(after, "Content/Blob.ubulk"); }, "lfs-hash");
            check(read_binary_file(result.after.file_path(), 10000) == target);
        }
        fs::remove_all(root);
        std::cout << checks << " Git binary delta checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
}
