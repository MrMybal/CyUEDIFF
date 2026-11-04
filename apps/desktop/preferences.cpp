#include "preferences.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <set>

namespace cyuediff::desktop {
namespace {
constexpr std::size_t max_bytes = 64 * 1024;
void require(bool condition, const char *message) {
    if (!condition)
        throw Error("desktop-preferences", message);
}
} // namespace
Json DesktopPreferences::to_json() const {
    return {{"schema", "cyuediff.preferences/1"},
            {"theme", theme},
            {"locale", locale},
            {"previews", previews},
            {"includeEngine", include_engine}};
}
DesktopPreferences DesktopPreferences::from_json(const Json &value) {
    require(value.is_object() && value.size() == 5, "Unexpected preferences fields");
    require(value.contains("schema") && value["schema"] == "cyuediff.preferences/1",
            "Unsupported preferences schema");
    require(value.contains("theme") && value["theme"].is_string() && value.contains("locale") &&
                value["locale"].is_string() && value.contains("previews") && value["previews"].is_boolean() &&
                value.contains("includeEngine") && value["includeEngine"].is_boolean(),
            "Invalid preferences field type");
    DesktopPreferences result{value["theme"].get<std::string>(), value["locale"].get<std::string>(),
                              value["previews"].get<bool>(), value["includeEngine"].get<bool>()};
    const std::set<std::string> themes = {"mid-grey", "light-grey", "amber-night", "violet", "navy"};
    require(themes.count(result.theme) != 0, "Unknown preferences theme");
    require(result.locale == "en" || result.locale == "fr", "Unknown preferences locale");
    return result;
}
fs::path default_preferences_path() {
    const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!count || count > 32768)
        return {};
    std::wstring folder(count, L'\0');
    const auto written = GetEnvironmentVariableW(L"LOCALAPPDATA", folder.data(), count);
    if (!written || written >= count)
        return {};
    folder.resize(written);
    fs::path path(folder);
    return path.is_absolute() ? path / "CyUEDiff" / "preferences.json" : fs::path{};
}
PreferencesRead read_preferences(const fs::path &path) {
    PreferencesRead result;
    try {
        require(!path.empty() && path.extension() == ".json", "Preferences require a .json path");
        if (!fs::exists(path)) {
            result.writable = true;
            return result;
        }
        require(fs::is_regular_file(path) && !fs::is_symlink(fs::symlink_status(path)),
                "Preferences must be a regular file");
        std::ifstream file(path, std::ios::binary);
        require(bool(file), "Cannot read preferences");
        std::string bytes(max_bytes + 1, '\0');
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        const auto count = file.gcount();
        require(!file.bad() && static_cast<std::size_t>(count) <= max_bytes, "Preferences exceed read limit");
        bytes.resize(static_cast<std::size_t>(count));
        std::vector<std::set<std::string>> keys;
        auto parsed = Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &value) {
            require(depth <= 16, "Preferences exceed depth limit");
            if (event == Json::parse_event_t::object_start)
                keys.emplace_back();
            else if (event == Json::parse_event_t::key)
                require(!keys.empty() && keys.back().insert(value.get<std::string>()).second,
                        "Duplicate preferences field");
            else if (event == Json::parse_event_t::object_end)
                keys.pop_back();
            return true;
        });
        result.value = DesktopPreferences::from_json(parsed);
        result.loaded = result.writable = true;
    } catch (const std::exception &error) {
        result.diagnostic = error.what();
    }
    return result;
}
void save_preferences(const fs::path &path, const DesktopPreferences &value) {
    DesktopPreferences::from_json(value.to_json());
    auto existing = read_preferences(path);
    require(existing.writable, "Existing preferences cannot be replaced");
    const auto destination = fs::absolute(path);
    fs::create_directories(destination.parent_path());
    static std::atomic<unsigned long> sequence{0};
    fs::path temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary = destination;
        temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        require(GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS,
                "Cannot create preferences temporary file");
    }
    require(handle != INVALID_HANDLE_VALUE, "Cannot reserve preferences temporary file");
    try {
        auto bytes = value.to_json().dump(2) + "\n";
        DWORD written = 0;
        require(WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size(),
                "Cannot write preferences");
        require(FlushFileBuffers(handle) != 0, "Cannot flush preferences");
        const bool closed = CloseHandle(handle) != 0;
        handle = INVALID_HANDLE_VALUE;
        require(closed, "Cannot close preferences");
        require(MoveFileExW(temporary.c_str(), destination.c_str(),
                            MOVEFILE_WRITE_THROUGH | (existing.loaded ? MOVEFILE_REPLACE_EXISTING : 0)) != 0,
                "Cannot replace preferences");
    } catch (...) {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
} // namespace cyuediff::desktop
