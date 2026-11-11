#include "git_process.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <random>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
namespace cyuediff::detail {
GitWorkspace::GitWorkspace(const fs::path &temporary_root) {
    auto parent = fs::weakly_canonical(
        fs::absolute(temporary_root.empty() ? fs::temp_directory_path() : temporary_root));
    if (!fs::is_directory(parent))
        throw Error("git-temp", "Temporary root must be an existing directory");
    static std::atomic<std::uint64_t> serial{0};
    std::random_device random;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        auto leaf = "cyuediff-git-" + std::to_string(random()) + "-" + std::to_string(++serial);
        auto candidate = parent / leaf;
        std::error_code error;
        if (fs::create_directory(candidate, error)) {
#ifndef _WIN32
            fs::permissions(candidate, fs::perms::owner_all, fs::perm_options::replace, error);
            if (error) {
                fs::remove(candidate);
                throw Error("git-temp", "Cannot restrict temporary workspace permissions");
            }
#endif
            directory = candidate;
            return;
        }
        if (error && error != std::errc::file_exists)
            throw Error("git-temp", "Cannot create temporary revision directory");
    }
    throw Error("git-temp", "Cannot allocate unique temporary revision directory");
}
GitWorkspace::~GitWorkspace() {
    // Flat, exclusively allocated workspace. Remove entries without following links;
    // never recursively remove an inferred or caller-owned directory.
    std::error_code error;
    if (directory.empty() || directory.filename().u8string().rfind("cyuediff-git-", 0) != 0)
        return;
    auto state = fs::symlink_status(directory, error);
    if (error || !fs::is_directory(state))
        return;
    fs::directory_iterator it(directory, error), end;
    while (!error && it != end) {
        auto file = it->path();
        if (file.parent_path() == directory && !fs::is_directory(it->symlink_status(error)))
            fs::remove(file, error);
        it.increment(error);
    }
    fs::remove(directory, error);
}
namespace {
std::string read_text(const fs::path &file) {
    auto bytes = fs::file_size(file);
    if (bytes > 256 * 1024)
        throw Error("git-output", "Git diagnostic output exceeds its limit");
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        throw Error("git-io", "Cannot read Git process output");
    std::string result(static_cast<std::size_t>(bytes), '\0');
    stream.read(result.data(), static_cast<std::streamsize>(result.size()));
    if (!stream)
        throw Error("git-io", "Cannot read complete Git process output");
    return result;
}
bool exceeds(const fs::path &file, std::uint64_t limit) {
    std::error_code error;
    auto size = fs::file_size(file, error);
    return !error && size > limit;
}
#ifdef _WIN32
std::wstring wide(const std::string &value) {
    if (value.find('\0') != std::string::npos)
        throw Error("git-arguments", "NUL in Git argument");
    if (value.empty())
        return {};
    auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                    static_cast<int>(value.size()), nullptr, 0);
    if (!size)
        throw Error("git-arguments", "Invalid UTF-8 Git argument");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}
std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (auto c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE && value)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
};
// CreateProcessW(nullptr, ...) searches the current directory before PATH, so a bare
// "git" launched from an untrusted repository could run a planted git.exe. Resolve bare
// names through absolute PATH entries only and pass the result as lpApplicationName.
fs::path resolve_executable(const fs::path &executable) {
    if (executable.has_parent_path())
        return fs::absolute(executable);
    auto name = executable;
    if (!name.has_extension())
        name += L".exe";
    std::wstring search;
    auto size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (size) {
        search.resize(size);
        size = GetEnvironmentVariableW(L"PATH", search.data(), size);
        search.resize(size);
    }
    std::size_t begin = 0;
    while (begin <= search.size()) {
        auto end = search.find(L';', begin);
        auto entry = search.substr(begin, end == std::wstring::npos ? std::wstring::npos : end - begin);
        begin = end == std::wstring::npos ? search.size() + 1 : end + 1;
        if (entry.size() >= 2 && entry.front() == L'"' && entry.back() == L'"')
            entry = entry.substr(1, entry.size() - 2);
        const fs::path parent(entry);
        if (entry.empty() || !parent.is_absolute())
            continue;
        std::error_code error;
        auto candidate = parent / name;
        if (fs::is_regular_file(candidate, error))
            return candidate;
    }
    throw Error("git-process", "Cannot find Git executable on PATH");
}
std::vector<wchar_t> environment() {
    auto block = GetEnvironmentStringsW();
    if (!block)
        throw Error("git-process", "Cannot read process environment");
    std::vector<std::wstring> values;
    for (auto entry = block; *entry; entry += wcslen(entry) + 1) {
        if (_wcsnicmp(entry, L"GIT_", 4) != 0)
            values.emplace_back(entry);
    }
    FreeEnvironmentStringsW(block);
    for (const auto *entry : {L"GIT_NO_LAZY_FETCH=1", L"GIT_NO_REPLACE_OBJECTS=1", L"GIT_OPTIONAL_LOCKS=0",
                              L"GIT_TERMINAL_PROMPT=0"})
        values.emplace_back(entry);
    std::sort(values.begin(), values.end(),
              [](const auto &a, const auto &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> result;
    for (const auto &entry : values) {
        result.insert(result.end(), entry.begin(), entry.end());
        result.push_back(0);
    }
    result.push_back(0);
    return result;
}
#endif
} // namespace
int git_process(const GitOptions &options, const fs::path &repository,
                const std::vector<std::string> &arguments, const fs::path &output, const fs::path &errors,
                std::uint64_t output_limit) {
    options.control.checkpoint("Reading Git objects");
    if (!options.timeout_ms || !output_limit || options.executable.empty())
        throw Error("git-options", "Git timeout, executable and output limits must be nonzero");
#ifdef _WIN32
    const auto executable = resolve_executable(options.executable);
#else
    const auto &executable = options.executable;
#endif
    std::vector<std::string> args = {executable.u8string(), "--no-pager", "--literal-pathspecs", "-C",
                                     repository.u8string()};
    args.insert(args.end(), arguments.begin(), arguments.end());
    for (const auto &arg : args)
        if (arg.find('\0') != std::string::npos || arg.size() > 32768)
            throw Error("git-arguments", "Invalid Git process argument");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeout_ms);
    std::string failure;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle out(CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle err(CreateFileW(errors.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (out.value == INVALID_HANDLE_VALUE || err.value == INVALID_HANDLE_VALUE ||
        input.value == INVALID_HANDLE_VALUE)
        throw Error("git-io", "Cannot create Git process streams");
    std::wstring command;
    for (const auto &arg : args) {
        if (!command.empty())
            command += L' ';
        command += quote(wide(arg));
    }
    if (command.size() >= 32767)
        throw Error("git-arguments", "Git command exceeds Windows length limit");
    auto env = environment();
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = out.value;
    startup.StartupInfo.hStdError = err.value;
    startup.StartupInfo.hStdInput = input.value;
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    std::vector<unsigned char> attributes(attribute_bytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes))
        throw Error("git-process", "Cannot initialize child handle list");
    HANDLE handles[] = {out.value, err.value, input.value};
    auto updated = UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                             handles, sizeof(handles), nullptr, nullptr);
    // Git for Windows' git.exe is a launcher for the real git: terminate the whole tree through
    // a kill-on-close job so no descendant keeps writing or locking the workspace files.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value ||
        !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        throw Error("git-process", "Cannot create Git process job");
    }
    PROCESS_INFORMATION process{};
    auto started = updated && CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                                                 EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
                                             env.data(), nullptr, &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!started)
        throw Error("git-process", "Cannot launch Git executable");
    Handle process_handle(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, process_handle.value) || ResumeThread(thread.value) == DWORD(-1)) {
        TerminateProcess(process_handle.value, 2);
        WaitForSingleObject(process_handle.value, INFINITE);
        throw Error("git-process", "Cannot start Git process in its job");
    }
    for (;;) {
        auto wait = WaitForSingleObject(process_handle.value, 20);
        if (options.control.cancelled && options.control.cancelled())
            failure = "cancelled";
        else if (exceeds(output, output_limit) || exceeds(errors, 256 * 1024))
            failure = "git-output";
        else if (wait == WAIT_FAILED)
            failure = "git-process";
        else if (wait != WAIT_OBJECT_0 && std::chrono::steady_clock::now() >= deadline)
            failure = "git-timeout";
        if (!failure.empty()) {
            TerminateJobObject(job.value, 2);
            WaitForSingleObject(process_handle.value, INFINITE);
            break;
        }
        if (wait == WAIT_OBJECT_0)
            break;
    }
    DWORD code = 2;
    GetExitCodeProcess(process_handle.value, &code);
    if (!failure.empty())
        throw Error(failure, failure == "cancelled" ? "Operation cancelled"
                                                    : "Git process exceeded its output or time limit");
    return static_cast<int>(code);
#else
    // Build environment/argv before fork so concurrent hosts do not allocate in the child.
    std::vector<std::string> env_values;
    for (char **entry = environ; *entry; ++entry)
        if (std::string(*entry).rfind("GIT_", 0) != 0)
            env_values.emplace_back(*entry);
    for (const auto *entry :
         {"GIT_NO_LAZY_FETCH=1", "GIT_NO_REPLACE_OBJECTS=1", "GIT_OPTIONAL_LOCKS=0", "GIT_TERMINAL_PROMPT=0"})
        env_values.emplace_back(entry);
    std::vector<char *> env, argv;
    for (auto &entry : env_values)
        env.push_back(entry.data());
    env.push_back(nullptr);
    for (auto &arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    fs::path executable = options.executable;
    if (!executable.has_parent_path()) {
        const char *path = std::getenv("PATH");
        std::string search = path ? path : "/usr/bin:/bin";
        std::size_t begin = 0;
        for (;;) {
            auto end = search.find(':', begin);
            auto parent = search.substr(begin, end == std::string::npos ? end : end - begin);
            // Empty or relative PATH entries name the current directory: never search it.
            auto candidate = fs::path(parent) / executable;
            if (!parent.empty() && parent.front() == '/' && ::access(candidate.c_str(), X_OK) == 0) {
                executable = fs::absolute(candidate);
                break;
            }
            if (end == std::string::npos)
                throw Error("git-process", "Cannot find Git executable");
            begin = end + 1;
        }
    }
    int out = ::open(output.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600),
        err = ::open(errors.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (out < 0 || err < 0) {
        if (out >= 0)
            close(out);
        if (err >= 0)
            close(err);
        throw Error("git-io", "Cannot open Git process streams");
    }
    auto pid = fork();
    if (pid == 0) {
        // Own process group, so timeouts and cancellation also stop Git's descendants.
        setpgid(0, 0);
        int input = ::open("/dev/null", O_RDONLY);
        dup2(out, STDOUT_FILENO);
        dup2(err, STDERR_FILENO);
        dup2(input, STDIN_FILENO);
        close(out);
        close(err);
        close(input);
        execve(executable.c_str(), argv.data(), env.data());
        _exit(127);
    }
    close(out);
    close(err);
    if (pid < 0)
        throw Error("git-process", "Cannot fork Git process");
    setpgid(pid, pid); // Also set by the child; whichever runs first wins the race.
    int status = 0;
    for (;;) {
        auto wait = waitpid(pid, &status, WNOHANG);
        if (options.control.cancelled && options.control.cancelled())
            failure = "cancelled";
        else if (exceeds(output, output_limit) || exceeds(errors, 256 * 1024))
            failure = "git-output";
        else if (wait < 0 && errno != EINTR)
            failure = "git-process";
        else if (wait != pid && std::chrono::steady_clock::now() >= deadline)
            failure = "git-timeout";
        if (!failure.empty()) {
            if (wait != pid) {
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
            }
            break;
        }
        if (wait == pid)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!failure.empty())
        throw Error(failure, failure == "cancelled" ? "Operation cancelled"
                                                    : "Git process exceeded its output or time limit");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
#endif
}
std::string git_text(const GitOptions &options, const fs::path &repository,
                     const std::vector<std::string> &arguments, GitWorkspace &workspace,
                     bool missing_config_ok) {
    auto output = workspace.directory / "stdout", errors = workspace.directory / "stderr";
    auto code = git_process(options, repository, arguments, output, errors, 256 * 1024);
    if (code != 0 && !(missing_config_ok && code == 1 && fs::file_size(errors) == 0))
        throw Error("git-command", "Git command failed: " + read_text(errors));
    return read_text(output);
}
} // namespace cyuediff::detail
