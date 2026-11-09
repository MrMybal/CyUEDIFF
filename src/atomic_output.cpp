#include "atomic_output.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <streambuf>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace cyuediff::detail {
namespace {
#ifdef _WIN32
using Handle = HANDLE;
const Handle invalid = INVALID_HANDLE_VALUE;
void close_handle(Handle h) { CloseHandle(h); }
#else
using Handle = int;
constexpr Handle invalid = -1;
void close_handle(Handle h) { ::close(h); }
#endif
struct File {
    Handle handle = invalid;
    fs::path path;
    bool published = false;
    ~File() {
        if (handle != invalid)
            close_handle(handle);
        if (!published && !path.empty()) {
            std::error_code ec;
            fs::remove(path, ec);
        }
    }
    void close() {
        if (handle == invalid)
            return;
#ifdef _WIN32
        const bool ok = CloseHandle(handle) != 0;
#else
        const bool ok = ::close(handle) == 0;
#endif
        handle = invalid;
        if (!ok)
            throw Error("io", "Cannot close report temporary file");
    }
};
class Buffer final : public std::streambuf {
    Handle handle_;
    std::array<char, 64 * 1024> bytes_{};
    bool raw(const char *data, std::size_t bytes) {
        while (bytes) {
#ifdef _WIN32
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes, 1u << 30));
            DWORD written = 0;
            if (!WriteFile(handle_, data, chunk, &written, nullptr) || !written)
                return false;
#else
            const auto written = ::write(handle_, data, std::min<std::size_t>(bytes, 1u << 30));
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                return false;
#endif
            data += written;
            bytes -= static_cast<std::size_t>(written);
        }
        return true;
    }
    bool drain() {
        const auto n = static_cast<std::size_t>(pptr() - pbase());
        if (!raw(pbase(), n))
            return false;
        setp(bytes_.data(), bytes_.data() + bytes_.size());
        return true;
    }

  protected:
    int_type overflow(int_type c) override {
        if (!drain())
            return traits_type::eof();
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(c);
            pbump(1);
        }
        return traits_type::not_eof(c);
    }
    std::streamsize xsputn(const char *data, std::streamsize n) override {
        if (n <= 0)
            return 0;
        const auto total = n;
        while (n) {
            auto room = epptr() - pptr();
            if (!room) {
                if (!drain())
                    break;
                room = epptr() - pptr();
            }
            const auto part = std::min<std::streamsize>(room, n);
            std::memcpy(pptr(), data, static_cast<std::size_t>(part));
            pbump(static_cast<int>(part));
            data += part;
            n -= part;
        }
        return total - n;
    }
    int sync() override {
        if (!drain())
            return -1;
#ifdef _WIN32
        return FlushFileBuffers(handle_) ? 0 : -1;
#else
        return ::fsync(handle_) == 0 ? 0 : -1;
#endif
    }

  public:
    explicit Buffer(Handle h) : handle_(h) { setp(bytes_.data(), bytes_.data() + bytes_.size()); }
};
} // namespace
void atomic_output(const fs::path &target, const std::function<void(std::ostream &)> &write) {
    if (target.empty() || target.filename().empty() || !write)
        throw Error("io", "Choose a report output file");
    const auto destination = fs::absolute(target).lexically_normal();
    if (fs::is_directory(destination))
        throw Error("io", "Choose a report file, not a directory");
    static std::atomic<std::uint64_t> serial{0};
    File file;
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = ::getpid();
#endif
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto candidate = destination.parent_path() /
                         fs::u8path(".cyuediff-report-" + std::to_string(pid) + "-" + std::to_string(stamp) +
                                    "-" + std::to_string(++serial) + ".tmp");
#ifdef _WIN32
        file.handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.handle != invalid) {
            file.path = std::move(candidate);
            break;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw Error("io", "Cannot create report temporary file");
#else
        file.handle = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (file.handle != invalid) {
            file.path = std::move(candidate);
            break;
        }
        if (errno != EEXIST)
            throw Error("io", "Cannot create report temporary file");
#endif
    }
    if (file.handle == invalid)
        throw Error("io", "Cannot reserve report temporary file");
    {
        Buffer buffer(file.handle);
        std::ostream stream(&buffer);
        write(stream);
        stream.flush();
        if (!stream)
            throw Error("io", "Cannot write report temporary file");
    }
    file.close();
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(destination.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (!ReplaceFileW(destination.c_str(), file.path.c_str(), nullptr, 0, nullptr, nullptr))
            throw Error("io", "Cannot replace report output file");
    } else {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            throw Error("io", "Cannot inspect report output file");
        if (!MoveFileExW(file.path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
            throw Error("io", "Cannot publish report output file");
    }

#else
    if (::rename(file.path.c_str(), destination.c_str()) != 0)
        throw Error("io", "Cannot replace report output file");
#endif
    file.published = true;
}
} // namespace cyuediff::detail
