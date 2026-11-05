#include "binary_commands.hpp"
#include <cyuediff/binary_delta.hpp>
#ifdef CYUEDIFF_HAS_GIT
#include <cyuediff/git_binary_delta.hpp>
#endif
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
using namespace cyuediff;
namespace {
void require(bool good, const char *message) {
    if (!good)
        throw Error("binary-command", message);
}
std::uint64_t number(const std::string &s) {
    require(!s.empty(), "Missing integer value");
    std::uint64_t n = 0;
    for (const auto c : s) {
        require(c >= '0' && c <= '9' && n <= (UINT64_MAX - std::uint64_t(c - '0')) / 10,
                "Invalid integer value");
        n = n * 10 + std::uint64_t(c - '0');
    }
    return n;
}
fs::path resolved(fs::path path) {
    auto existing = fs::absolute(path).lexically_normal();
    fs::path suffix;
    while (!fs::exists(existing)) {
        const auto parent = existing.parent_path();
        require(!parent.empty() && parent != existing, "Cannot resolve output ancestor");
        suffix = suffix.empty() ? existing.filename() : existing.filename() / suffix;
        existing = parent;
    }
    const auto canonical = fs::canonical(existing);
    return (suffix.empty() ? canonical : canonical / suffix).lexically_normal();
}
auto path_key(const fs::path &p) {
    auto key = p.native();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
#endif
    return key;
}
void guard_output(const fs::path &target, const std::vector<fs::path> &inputs,
                  const std::vector<fs::path> &directories = {}) {
    if (target.empty())
        return;
    const auto normalized = resolved(target);
    for (const auto &input : inputs) {
        require(path_key(normalized) != path_key(resolved(input)), "Output overlaps an input or companion");
        require(!(fs::exists(target) && fs::exists(input) && fs::equivalent(target, input)),
                "Output aliases an input");
    }
    for (const auto &directory : directories) {
        const auto base = path_key(resolved(directory));
        for (auto parent = normalized; !parent.empty();) {
            require(path_key(parent) != base, "Output overlaps protected Git or LFS storage");
            const auto next = parent.parent_path();
            if (next == parent)
                break;
            parent = next;
        }
    }
    require(!fs::exists(target) && !fs::is_symlink(fs::symlink_status(target)),
            "Output already exists; choose a new output path");
}
void protect_file(std::vector<fs::path> &files, const fs::path &input) {
    files.push_back(input);
    if (input.extension() == ".uasset" || input.extension() == ".umap") {
        for (const auto *ext : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
            auto companion = input;
            companion.replace_extension(ext);
            files.push_back(companion);
        }
    }
}
// Publish a fully flushed sibling temporary file without replacing any existing destination.
void write_new(const fs::path &destination, const std::function<void(const BinaryDeltaSink &)> &produce) {
    const auto target = fs::absolute(destination);
    require(fs::is_directory(target.parent_path()), "Output parent directory does not exist");
    static std::atomic<unsigned long> sequence{0};
    fs::path temporary;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    const auto pid = GetCurrentProcessId();
#else
    int handle = -1;
    const auto pid = getpid();
#endif
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        temporary = target;
        temporary += ".cyuediff-tmp-" + std::to_string(pid) + "-" + std::to_string(++sequence);
#ifdef _WIN32
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        require(GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS,
                "Cannot reserve output temporary file");
#else
        handle = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (handle >= 0)
            break;
        require(errno == EEXIST, "Cannot reserve output temporary file");
#endif
    }
#ifdef _WIN32
    require(handle != INVALID_HANDLE_VALUE, "Cannot reserve output temporary file");
#else
    require(handle >= 0, "Cannot reserve output temporary file");
#endif
    try {
        const BinaryDeltaSink sink = [&](const std::uint8_t *data, std::size_t bytes) {
            for (std::size_t at = 0; at < bytes;) {
                const auto n = std::min<std::size_t>(bytes - at, 1024 * 1024);
#ifdef _WIN32
                DWORD written = 0;
                require(WriteFile(handle, data + at, static_cast<DWORD>(n), &written, nullptr) &&
                            written == n,
                        "Cannot write output");
                at += written;
#else
                const auto written = ::write(handle, data + at, n);
                if (written < 0 && errno == EINTR)
                    continue;
                require(written > 0, "Cannot write output");
                at += static_cast<std::size_t>(written);
#endif
            }
        };
        produce(sink);
#ifdef _WIN32
        require(FlushFileBuffers(handle) != 0, "Cannot flush output");
        const bool closed = CloseHandle(handle) != 0;
        handle = INVALID_HANDLE_VALUE;
        require(closed, "Cannot close output");
        require(MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != 0,
                "Cannot publish new output");
#else
        require(::fsync(handle) == 0, "Cannot flush output");
        const bool closed = ::close(handle) == 0;
        handle = -1;
        require(closed, "Cannot close output");
        require(::link(temporary.c_str(), target.c_str()) == 0, "Cannot publish new output");
        std::error_code ignored;
        fs::remove(temporary, ignored);
#endif
    } catch (...) {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
#else
        if (handle >= 0)
            ::close(handle);
#endif
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
void write_new(const fs::path &destination, const std::uint8_t *data, std::size_t bytes) {
    write_new(destination, [&](const BinaryDeltaSink &sink) { sink(data, bytes); });
}
void emit_delta(const BinaryDelta &d, const BinaryDeltaOptions &o, bool summary, bool compact,
                const fs::path &out, const std::vector<fs::path> &inputs,
                const std::vector<fs::path> &directories = {}, const Json *report = nullptr) {
    guard_output(out, inputs, directories);
    if (compact) {
        const auto bytes = d.to_binary(o);
        write_new(out, bytes.data(), bytes.size());
    } else {
        const auto text = (report ? *report : (summary ? d.summary(o) : d.to_json(o))).dump() + "\n";
        require(text.size() <= o.max_manifest_bytes, "Manifest byte budget exceeded");
        if (out.empty())
            std::cout << text;
        else
            write_new(out, reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
    }
}

} // namespace
bool is_binary_command(const std::string &s) {
    return s == "binary-diff" || s == "binary-apply" || s == "git-binary-diff" || s == "binary-compose" ||
           s == "binary-info";
}
int run_binary_command(const std::vector<std::string> &args) {
    const bool apply = args.front() == "binary-apply";
    const bool compose = args.front() == "binary-compose", info = args.front() == "binary-info";
    const bool historical = args.front() == "git-binary-diff";
    BinaryDeltaOptions o;
    std::vector<std::string> positional;
    std::map<std::string, std::string> flags;
    bool summary = false;
    const std::set<std::string> valued = {"--out",
                                          "--block-size",
                                          "--max-input-bytes",
                                          "--max-literal-bytes",
                                          "--max-manifest-bytes",
                                          "--after-path",
                                          "--lfs-objects",
                                          "--git-temp",
                                          "--format",
                                          "--max-composition-fragments"};
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto &a = args[i];
        if (a == "--summary") {
            require(!summary && !apply && !info, "--summary requires a delta comparison or composition");
            summary = true;
        } else if (valued.count(a)) {
            require(i + 1 < args.size() && !flags.count(a), "Missing or duplicate binary command option");
            flags[a] = args[++i];
        } else {
            require(a.rfind("--", 0) != 0, "Unknown binary command option");
            positional.push_back(a);
        }
    }
    require(positional.size() == (historical ? 4 : (info ? 1 : 2)),
            "Wrong number of binary command arguments");
    if (!historical)
        require(!flags.count("--after-path") && !flags.count("--lfs-objects") && !flags.count("--git-temp"),
                "Historical file options require git-binary-diff");
    if (flags.count("--block-size")) {
        require(!apply && !compose && !info, "--block-size requires binary-diff");
        const auto n = number(flags.at("--block-size"));
        require(n >= 16 && n <= 1024 * 1024, "Block size must be between 16 bytes and 1 MiB");
        o.block_size = static_cast<std::uint32_t>(n);
    }
    if (flags.count("--max-input-bytes"))
        o.max_input_bytes = number(flags.at("--max-input-bytes"));
    if (flags.count("--max-literal-bytes"))
        o.max_literal_bytes = number(flags.at("--max-literal-bytes"));
    if (flags.count("--max-manifest-bytes"))
        o.max_manifest_bytes = number(flags.at("--max-manifest-bytes"));
    bool compact = false;
    if (flags.count("--format")) {
        require(!apply && !info, "--format requires a delta comparison or composition");
        const auto &format = flags.at("--format");
        require(format == "json" || format == "binary", "--format must be json or binary");
        compact = format == "binary";
    }
    if (flags.count("--max-composition-fragments")) {
        require(compose, "--max-composition-fragments requires binary-compose");
        const auto n = number(flags.at("--max-composition-fragments"));
        require(n && n <= std::numeric_limits<std::size_t>::max(), "Invalid composition fragment limit");
        o.max_composition_fragments = static_cast<std::size_t>(n);
    }
    const auto out = flags.count("--out") ? fs::u8path(flags.at("--out")) : fs::path{};
    require(!apply || !out.empty(), "binary-apply requires --out with a new destination");
    require(!compact || (!summary && !out.empty()), "Binary format requires --out and excludes --summary");
    if (compose || info) {
        std::vector<fs::path> inputs;
        for (const auto &path : positional)
            inputs.push_back(fs::u8path(path));
        guard_output(out, inputs);
        const auto first = read_binary_delta(inputs.front(), o);
        if (compose) {
            const auto second = read_binary_delta(inputs.back(), o);
            const auto result = compose_binary_deltas(first, second, o);
            emit_delta(result, o, summary, compact, out, inputs);
        } else {
            const Json report = {{"schema", "cyuediff.binary-info/1"},
                                 {"summary", first.summary(o)},
                                 {"identitiesVerified", false}};
            emit_delta(first, o, true, false, out, inputs, {}, &report);
        }
        return 0;
    }
    if (historical) {
#ifdef CYUEDIFF_HAS_GIT
        GitOptions git;
        if (flags.count("--lfs-objects"))
            git.lfs_objects = fs::u8path(flags.at("--lfs-objects"));
        if (flags.count("--git-temp"))
            git.temporary_root = fs::u8path(flags.at("--git-temp"));
        const auto repo = GitRepository::open(fs::u8path(positional[0]), git);
        const std::string after_path = flags.count("--after-path") ? flags.at("--after-path") : positional[3];
        std::vector<fs::path> inputs = {repo.root() / ".git"};
        protect_file(inputs, repo.root() / fs::u8path(positional[3]));
        protect_file(inputs, repo.root() / fs::u8path(after_path));
        const std::vector<fs::path> directories = {repo.common_directory(), repo.lfs_objects()};
        guard_output(out, inputs, directories);
        const auto result =
            compare_git_binary_files(repo, positional[1], positional[2], positional[3], after_path, o);
        require(result.status() != "absent", "Both selected historical files are absent");
        const auto report = summary ? result.summary(o) : Json();
        emit_delta(result.delta, o, summary, compact, out, inputs, directories, summary ? &report : nullptr);
        return result.status() == "absent" ? 2 : (result.status() == "identical" ? 0 : 1);
#else
        throw Error("git-unavailable", "This build does not include the Git adapter");
#endif
    }
    const auto base = fs::u8path(positional[0]), other = fs::u8path(positional[1]);
    std::vector<fs::path> protected_files;
    protect_file(protected_files, base);
    protect_file(protected_files, other);
    guard_output(out, protected_files);
    if (apply) {
        auto delta = read_binary_delta(other, o);
        guard_output(out, protected_files);
        write_new(out, [&](const BinaryDeltaSink &sink) { apply_binary_delta_file(base, delta, sink, o); });
        std::cout << Json{{"schema", "cyuediff.binary-apply/1"},
                          {"bytes", delta.target_bytes},
                          {"sha256", delta.target_sha256},
                          {"baseVerified", true},
                          {"targetVerified", true}}
                         .dump()
                  << '\n';
        return 0;
    }
    const auto delta = make_binary_delta_files(base, other, o);
    emit_delta(delta, o, summary, compact, out, protected_files);
    return delta.base_bytes == delta.target_bytes && delta.base_sha256 == delta.target_sha256 ? 0 : 1;
}
