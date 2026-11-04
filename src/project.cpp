#include "archive.hpp"
#include <cstdlib>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace cyuediff {
namespace {
fs::path normalized(const fs::path &p) {
    return fs::weakly_canonical(fs::absolute(p));
}
bool within(const fs::path &p, const fs::path &root) {
    auto relative = p.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != fs::path("..");
}
fs::path find_engine(const std::string &association) {
    if (auto value = std::getenv("CYUEDIFF_ENGINE_ROOT"))
        return fs::u8path(value);
#ifdef _WIN32
    DWORD type = 0, bytes = 0;
    auto wide = fs::u8path(association).wstring();
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Epic Games\\Unreal Engine\\Builds", wide.c_str(),
                     RRF_RT_REG_SZ, &type, nullptr, &bytes) == ERROR_SUCCESS) {
        std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Epic Games\\Unreal Engine\\Builds", wide.c_str(),
                         RRF_RT_REG_SZ, &type, text.data(), &bytes) == ERROR_SUCCESS)
            return fs::path(text.data());
    }
    if (association.find_first_not_of("0123456789.") == std::string::npos)
        if (auto files = std::getenv("ProgramFiles")) {
            auto root = fs::path(files) / "Epic Games" / ("UE_" + association);
            if (fs::exists(root / "Engine" / "Build" / "Build.version"))
                return root;
        }
#endif
    return {};
}
} // namespace
EngineInfo EngineInfo::open(const fs::path &path) {
    EngineInfo result;
    result.root = normalized(path);
    auto file = result.root / "Engine" / "Build" / "Build.version";
    std::error_code error;
    if (!fs::is_regular_file(file, error))
        throw Error("io", "Engine Build.version is unavailable: " + file.u8string());
    auto descriptor = detail::load_json(file);
    result.major = descriptor.at("MajorVersion").get<int>();
    result.minor = descriptor.at("MinorVersion").get<int>();
    result.patch = descriptor.value("PatchVersion", 0);
    return result;
}
EngineInfo EngineInfo::resolve(const std::string &association, const fs::path &explicit_root) {
    auto root = explicit_root.empty() ? find_engine(association) : explicit_root;
    return root.empty() ? EngineInfo{} : open(root);
}
Json EngineInfo::to_json() const {
    return {{"root", root.u8string()}, {"major", major}, {"minor", minor}, {"patch", patch}};
}
ProjectContext ProjectContext::open(const fs::path &path, const fs::path &root) {
    ProjectContext result;
    result.project_file = normalized(path);
    auto descriptor = detail::load_json(result.project_file);
    result.engine_association = descriptor.value("EngineAssociation", std::string{});
    result.engine = EngineInfo::resolve(result.engine_association, root);
    if (result.engine.root.empty())
        result.diagnostics.push_back("Engine association unresolved; provide --engine-root to mount Engine "
                                     "content. Reading versioned packages remains available.");
    if (result.engine.major && result.engine_association.size() && result.engine_association[0] != '{') {
        auto version = std::to_string(result.engine.major) + "." + std::to_string(result.engine.minor);
        if (version != result.engine_association)
            result.diagnostics.push_back("Selected engine " + version + " differs from project association " +
                                         result.engine_association);
    }
    result.mounts.push_back({"/Game", result.project_file.parent_path() / "Content"});
    if (!result.engine.root.empty())
        result.mounts.push_back({"/Engine", result.engine.root / "Engine" / "Content"});
    std::set<std::string> disabled;
    for (const auto &plugin : descriptor.value("Plugins", Json::array()))
        if (plugin.contains("Enabled") && !plugin.at("Enabled").get<bool>())
            disabled.insert(plugin.at("Name").get<std::string>());
    std::vector<fs::path> plugin_roots = {result.project_file.parent_path() / "Plugins"};
    if (!result.engine.root.empty())
        plugin_roots.push_back(result.engine.root / "Engine" / "Plugins");
    for (const auto &directory : plugin_roots) {
        if (!fs::is_directory(directory))
            continue;
        for (const auto &entry :
             fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".uplugin")
                continue;
            auto name = entry.path().stem().u8string();
            if (disabled.count(name))
                continue;
            try {
                if (!detail::load_json(entry.path()).value("CanContainContent", false))
                    continue;
                auto mount = "/" + name;
                auto duplicate = std::find_if(result.mounts.begin(), result.mounts.end(),
                                              [&](const auto &m) { return m.name == mount; });
                if (duplicate == result.mounts.end())
                    result.mounts.push_back({mount, entry.path().parent_path() / "Content"});
            } catch (const std::exception &e) {
                result.diagnostics.push_back(entry.path().u8string() + ": " + e.what());
            }
        }
    }
    return result;
}
fs::path ProjectContext::resolve(const std::string &name) const {
    for (const auto &mount : mounts) {
        auto prefix = mount.name + "/";
        if (name.compare(0, prefix.size(), prefix) != 0)
            continue;
        auto tail = name.substr(prefix.size());
        if (tail.empty() || tail.find('\\') != std::string::npos || tail.find(':') != std::string::npos ||
            tail.find('.') != std::string::npos)
            throw Error("package-path",
                        "Expected a long package name without traversal, object suffix or extension");
        auto base = normalized(mount.directory);
        auto path = normalized(base / fs::u8path(tail));
        if (!within(path, base))
            throw Error("package-path", "Package escapes content mount");
        for (const auto *extension : {".uasset", ".umap"}) {
            auto candidate = path;
            candidate += extension;
            if (fs::is_regular_file(candidate))
                return candidate;
        }
        throw Error("missing-package", "Package not found: " + name);
    }
    throw Error("mount", "No content mount for " + name);
}
std::string ProjectContext::package_name(const fs::path &file) const {
    auto path = normalized(file);
    for (const auto &mount : mounts) {
        auto base = normalized(mount.directory);
        if (within(path, base)) {
            auto relative = path.lexically_relative(base);
            relative.replace_extension();
            return mount.name + "/" + relative.generic_u8string();
        }
    }
    throw Error("mount", "File is outside the project's content mounts");
}
Json ProjectContext::to_json() const {
    Json mounts_json = Json::object();
    for (const auto &mount : mounts)
        mounts_json[mount.name] = mount.directory.u8string();
    return {{"schema", "cyuediff.project/1"},
            {"project", project_file.u8string()},
            {"engineAssociation", engine_association},
            {"engine", engine.to_json()},
            {"mounts", mounts_json},
            {"diagnostics", diagnostics}};
}
} // namespace cyuediff
