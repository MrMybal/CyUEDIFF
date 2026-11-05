#include "workspace_file.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <fstream>
#include <set>
namespace cyuediff::desktop {
namespace {
constexpr std::size_t max_bytes = 1024 * 1024;
void require(bool good, const char *message) {
    if (!good)
        throw Error("desktop-workspace-file", message);
}
const std::pair<const char *, std::string Settings::*> strings[] = {
    {"before", &Settings::before},
    {"after", &Settings::after},
    {"project", &Settings::project},
    {"engine", &Settings::engine},
    {"repository", &Settings::repository},
    {"lfs_objects", &Settings::lfs_objects},
    {"after_project", &Settings::after_project},
    {"after_engine", &Settings::after_engine},
    {"before_revision", &Settings::before_revision},
    {"after_revision", &Settings::after_revision},
    {"container_path", &Settings::container_path},
    {"container_entry", &Settings::container_entry},
    {"script_objects", &Settings::script_objects},
    {"after_container_path", &Settings::after_container_path},
    {"after_script_objects", &Settings::after_script_objects},
    {"container_schema", &Settings::container_schema},
    {"after_container_schema", &Settings::after_container_schema},
};
const std::pair<const char *, bool Settings::*> flags[] = {
    {"previews", &Settings::previews},
    {"discover_engine", &Settings::discover_engine},
    {"include_engine", &Settings::include_engine},
    {"compare_container_payloads", &Settings::compare_container_payloads},
};
std::string text(const Json &j) {
    require(j.is_string(), "Workspace string expected");
    auto result = j.get<std::string>();
    require(result.size() <= 32768, "Workspace string exceeds limit");
    for (unsigned char c : result)
        require(c >= 32 && c != 127, "Workspace string contains a control character");
    return result;
}
int number(const Json &j, int low, int high) {
    require(j.is_number_integer() && j >= low && j <= high, "Workspace integer outside range");
    return j.get<int>();
}
void normalize(Settings &s, const fs::path &base) {
    auto path = [&](std::string &value) {
        if (value.empty())
            return;
        auto p = fs::u8path(value);
        require(!p.has_root_name() || p.is_absolute(), "Drive-relative workspace path is ambiguous");
        value = (p.is_absolute() ? p : base / p).lexically_normal().u8string();
    };
    for (auto member :
         {&Settings::engine, &Settings::after_engine, &Settings::repository, &Settings::lfs_objects,
          &Settings::container_path, &Settings::after_container_path, &Settings::script_objects,
          &Settings::after_script_objects, &Settings::container_schema, &Settings::after_container_schema})
        path(s.*member);
    if (s.source != 1) {
        for (bool after : {false, true}) {
            auto &name = after ? s.after : s.before;
            const auto &project = after && !s.after_project.empty() ? s.after_project : s.project;
            if (!name.empty() && (project.empty() || name.front() != '/'))
                path(name);
        }
        path(s.project);
        path(s.after_project);
    }
}
WorkspaceFile parse_bytes(const std::string &bytes) {
    std::vector<std::set<std::string>> keys;
    auto value = Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &j) {
        require(depth <= 24, "Workspace nesting exceeds limit");
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        else if (event == Json::parse_event_t::key)
            require(!keys.empty() && keys.back().insert(j.get<std::string>()).second,
                    "Duplicate workspace field");
        else if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
    return WorkspaceFile::from_json(value);
}
void check_path(const fs::path &path) {
    require(!path.empty() && path.extension() == ".json", "Workspace requires a .json path");
    require(!fs::exists(path) || (fs::is_regular_file(path) && !fs::is_symlink(fs::symlink_status(path))),
            "Workspace must be a regular file");
}
} // namespace
Json WorkspaceFile::to_json() const {
    Json docs = Json::array();
    for (const auto &d : documents) {
        Json s = Json::object();
        for (const auto &field : strings)
            s[field.first] = d.settings.*field.second;
        for (const auto &field : flags)
            s[field.first] = d.settings.*field.second;
        s["source"] = d.settings.source;
        s["cooked_profile"] = d.settings.cooked_profile;
        s["after_cooked_profile"] = d.settings.after_cooked_profile;
        docs.push_back({{"settings", s},
                        {"view", d.view},
                        {"containerTab", d.container_tab},
                        {"cookedView", d.cooked_view},
                        {"viewState", d.view_state}});
    }
    Json result = {{"schema", "cyuediff.workspace/3"}, {"active", active}, {"documents", docs}};
    from_json(result);
    return result;
}
WorkspaceFile WorkspaceFile::from_json(const Json &j) {
    require(j.is_object() && j.size() == 3 && j.contains("schema") &&
                (j["schema"] == "cyuediff.workspace/1" || j["schema"] == "cyuediff.workspace/2" ||
                 j["schema"] == "cyuediff.workspace/3") &&
                j.contains("active") && j.contains("documents"),
            "Unsupported workspace format");
    const auto &docs = j["documents"];
    require(docs.is_array() && !docs.empty() && docs.size() <= 8,
            "Workspace requires one to eight documents");
    WorkspaceFile result;
    result.documents.clear();
    result.active = number(j["active"], 0, static_cast<int>(docs.size() - 1));
    for (const auto &d : docs) {
        const bool legacy = j["schema"] == "cyuediff.workspace/1";
        require(d.is_object() && d.size() == (legacy ? 4 : 5) && (legacy || d.contains("viewState")) &&
                    d.contains("settings") && d.contains("view") && d.contains("containerTab") &&
                    d.contains("cookedView"),
                "Invalid workspace document");
        SavedDocument saved;
        if (!legacy) {
            view_state::validate(d["viewState"]);
            saved.view_state = d["viewState"];
        }
        saved.view = text(d["view"]);
        saved.cooked_view = text(d["cookedView"]);
        const std::set<std::string> views = {"properties", "blueprint",  "materials", "textures",
                                             "meshes",     "comparison", "collisions", "skeleton", "animations"};
        require(views.count(saved.view) && (saved.cooked_view.empty() || saved.cooked_view == "properties" ||
                                            saved.cooked_view == "object"),
                "Unsupported workspace view");
        saved.container_tab = number(d["containerTab"], -1, 4);
        const auto &s = d["settings"];
        require(s.is_object() && s.size() == std::size(strings) + std::size(flags) + 3,
                "Unexpected workspace settings");
        for (const auto &field : strings) {
            require(s.contains(field.first), "Missing workspace setting");
            saved.settings.*field.second = text(s[field.first]);
        }
        for (const auto &field : flags) {
            require(s.contains(field.first) && s[field.first].is_boolean(), "Invalid workspace boolean");
            saved.settings.*field.second = s[field.first].get<bool>();
        }
        for (const auto &key : {"source", "cooked_profile", "after_cooked_profile"})
            require(s.contains(key), "Missing workspace source/profile");
        saved.settings.source = number(s["source"], 0, 2);
        saved.settings.cooked_profile = number(s["cooked_profile"], 0, 2);
        saved.settings.after_cooked_profile = number(s["after_cooked_profile"], 0, 2);
        result.documents.push_back(std::move(saved));
    }
    return result;
}
WorkspaceFile read_workspace(const fs::path &path) {
    check_path(path);
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Cannot read workspace");
    std::string bytes(max_bytes + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = file.gcount();
    require(!file.bad() && count <= max_bytes, "Workspace exceeds read limit");
    bytes.resize(static_cast<std::size_t>(count));
    auto result = parse_bytes(bytes);
    for (auto &doc : result.documents)
        normalize(doc.settings, fs::absolute(path).parent_path());
    return result;
}
void save_workspace(const fs::path &path, const WorkspaceFile &value) {
    // Validate and serialize before creating any directory or temporary file.
    auto saved = WorkspaceFile::from_json(value.to_json());
    for (auto &doc : saved.documents)
        normalize(doc.settings, fs::current_path());
    const auto bytes = saved.to_json().dump(2) + "\n";
    require(bytes.size() <= max_bytes, "Workspace exceeds write limit");
    check_path(path);
    const bool exists = fs::exists(path);
    if (exists)
        read_workspace(path);
    const auto target = fs::absolute(path);
    fs::create_directories(target.parent_path());
    static std::atomic<unsigned long> sequence{0};
    fs::path temp;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temp = target;
        temp += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
        handle =
            CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        require(GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS,
                "Cannot create workspace temporary file");
    }
    require(handle != INVALID_HANDLE_VALUE, "Cannot reserve workspace temporary file");
    try {
        DWORD written = 0;
        require(WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size(),
                "Cannot write workspace");
        require(FlushFileBuffers(handle) != 0, "Cannot flush workspace");
        const bool closed = CloseHandle(handle) != 0;
        handle = INVALID_HANDLE_VALUE;
        require(closed, "Cannot close workspace");
        require(MoveFileExW(temp.c_str(), target.c_str(),
                            MOVEFILE_WRITE_THROUGH | (exists ? MOVEFILE_REPLACE_EXISTING : 0)) != 0,
                "Cannot replace workspace");
    } catch (...) {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
        std::error_code ignored;
        fs::remove(temp, ignored);
        throw;
    }
}
} // namespace cyuediff::desktop
