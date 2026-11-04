#include "git_project.hpp"
#include "descriptor.hpp"
#include <algorithm>
#include <fstream>
#include <set>
namespace cyuediff {
namespace {
std::string fold(std::string value) {
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return value;
}
bool inside(const std::string &path, const std::string &directory) {
    return path.size() > directory.size() && path.compare(0, directory.size(), directory) == 0 &&
           path[directory.size()] == '/';
}
void issue(GitProjectContext &context, const std::string &message) {
    context.complete = false;
    context.diagnostics.push_back(message);
}
bool mount_name(const std::string &name) {
    return !name.empty() && name.find_first_of("/\\.:<>\"|?* ") == std::string::npos &&
           std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
Json normalize_project(const Json &descriptor) {
    auto result = descriptor;
    if (descriptor.contains("Plugins") && descriptor.at("Plugins").is_array()) {
        Json plugins = Json::object();
        std::size_t index = 0;
        for (const auto &reference : descriptor.at("Plugins")) {
            auto key = reference.is_object() && reference.contains("Name") && reference.at("Name").is_string()
                           ? fold(reference.at("Name").get<std::string>())
                           : "invalid:" + std::to_string(index);
            while (plugins.contains(key))
                key += ":duplicate";
            plugins[key] = reference;
            ++index;
        }
        result["Plugins"] = std::move(plugins);
    }
    return result;
}
std::string long_tail(const std::string &name) {
    if (name.empty() || name.front() != '/' || name.back() == '/' ||
        name.find_first_of("\\.:<>\"|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
        name.find("//") != std::string::npos)
        throw Error("package-path",
                    "Expected a long package name without extension, object suffix or traversal");
    return name;
}
GitProjectDocument local_document(const fs::path &file, const GitProjectOptions &options,
                                  std::uint64_t &total) {
    GitProjectDocument document;
    document.file.path = file.u8string();
    std::uint64_t size = fs::file_size(file);
    if (size > options.max_descriptor_bytes || total > options.max_descriptor_total_bytes ||
        size > options.max_descriptor_total_bytes - total)
        throw Error("git-project-budget", "Local engine descriptors exceed configured byte budget");
    total += size;
    document.file.content_bytes = document.file.blob_bytes = size;
    std::ifstream stream(file, std::ios::binary);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
    if (!stream || stream.peek() != std::char_traits<char>::eof())
        throw Error("git-project-io", "Engine descriptor changed during read");
    document.file.content_sha256 = sha256_bytes(bytes);
    try {
        document.descriptor = detail::parse_descriptor(std::string(bytes.begin(), bytes.end()));
        if (!document.descriptor.is_object())
            throw Error("git-project-format", "Descriptor must be a JSON object");
    } catch (const std::exception &e) {
        document.error = e.what();
        document.descriptor = nullptr;
    }
    return document;
}
} // namespace
Json GitProjectDocument::to_json() const {
    return {{"file", file.to_json()}, {"descriptor", descriptor}, {"error", error}};
}
Json GitContentMount::to_json() const {
    return {{"storage", storage}, {"directory", directory}, {"descriptor", descriptor}};
}
Json GitPackageLocation::to_json() const {
    return {{"storage", storage}, {"path", path}};
}
GitPackageLocation GitProjectContext::locate(const std::string &input) const {
    auto name = long_tail(input);
    for (const auto &mount : mounts) {
        auto prefix = mount.name + "/";
        if (name.rfind(prefix, 0) != 0)
            continue;
        auto tail = name.substr(prefix.size());
        if (mount.storage == "git")
            return {"git", mount.directory + "/" + tail};
        auto base = fs::weakly_canonical(fs::absolute(fs::u8path(mount.directory)));
        auto candidate = fs::weakly_canonical(base / fs::u8path(tail));
        auto relative = candidate.lexically_relative(base);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw Error("package-path", "Package escapes local engine content mount");
        return {"engine-local", candidate.u8string()};
    }
    throw Error("git-project-mount", "No historical content mount for " + name);
}
Json GitProjectContext::semantic_snapshot() const {
    Json mounted = Json::object(), descriptors = Json::object();
    for (const auto &mount : mounts)
        mounted[mount.name] = mount.to_json();
    for (const auto &document : documents) {
        if (document.error.empty())
            descriptors[document.file.path] = fs::u8path(document.file.path).extension() == ".uproject"
                                                  ? normalize_project(document.descriptor)
                                                  : document.descriptor;
        else
            descriptors[document.file.path] = {{"opaque", document.file.to_json()}};
    }
    return {{"project", project_path},    {"engineAssociation", engine_association},
            {"engine", engine.to_json()}, {"engineBuild", engine_build},
            {"mounts", mounted},          {"descriptors", descriptors},
            {"complete", complete}};
}
Json GitProjectContext::to_json() const {
    auto result = semantic_snapshot();
    result["schema"] = "cyuediff.git-project/1";
    result["commit"] = commit;
    result["pluginActivationEvaluated"] = false;
    result["engineContentSource"] = engine.root.empty() ? Json(nullptr) : Json("local-installation");
    result["diagnostics"] = diagnostics;
    result["documents"] = Json::array();
    for (const auto &document : documents)
        result["documents"].push_back(document.to_json());
    return result;
}
namespace detail {
std::vector<std::string> plugin_directories(const Json &descriptor, const std::string &project_path,
                                            bool &complete, std::vector<std::string> &diagnostics) {
    auto parent = fs::u8path(project_path).parent_path();
    std::set<std::string> directories = {(parent / "Plugins").generic_u8string()};
    if (descriptor.contains("AdditionalPluginDirectories")) {
        const auto &extra = descriptor.at("AdditionalPluginDirectories");
        if (!extra.is_array())
            throw Error("git-project-format", "AdditionalPluginDirectories must be an array");
        for (const auto &directory : extra) {
            if (!directory.is_string())
                throw Error("git-project-format", "Plugin search directory must be a string");
            auto text = directory.get<std::string>();
            auto reject = [&] {
                complete = false;
                diagnostics.push_back(
                    "Plugin search directory is outside the historical repository or not portable: " + text);
            };
            if (text.empty() || text.front() == '/' ||
                text.find_first_of("\\:<>\"|?*") != std::string::npos ||
                std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
                reject();
                continue;
            }
            auto path = (parent / fs::u8path(text)).lexically_normal();
            auto normalized = path.generic_u8string();
            if (normalized.empty() || normalized == "." || path.is_absolute() || *path.begin() == "..") {
                reject();
                continue;
            }
            while (!normalized.empty() && normalized.back() == '/')
                normalized.pop_back();
            directories.insert(normalized);
        }
    }
    return {directories.begin(), directories.end()};
}
GitProjectContext build_git_project(std::string commit, std::string project,
                                    std::vector<GitProjectDocument> documents,
                                    const GitProjectOptions &options, bool discovery_complete,
                                    std::vector<std::string> diagnostics) {
    GitProjectContext context;
    context.commit = std::move(commit);
    context.project_path = std::move(project);
    context.complete = discovery_complete;
    context.diagnostics = std::move(diagnostics);
    const auto &primary = documents.at(0).descriptor;
    if (primary.contains("EngineAssociation") && !primary.at("EngineAssociation").is_string())
        throw Error("git-project-format", "EngineAssociation must be a string");
    context.engine_association = primary.value("EngineAssociation", std::string{});
    if (options.discover_engine || !options.engine_root.empty())
        context.engine = EngineInfo::resolve(context.engine_association, options.engine_root);
    if (context.engine.root.empty())
        context.diagnostics.push_back(
            "Engine association unresolved; only committed project content roots are available");
    else if (!context.engine_association.empty() && context.engine_association.front() != '{') {
        auto version = std::to_string(context.engine.major) + "." + std::to_string(context.engine.minor);
        if (version != context.engine_association)
            context.diagnostics.push_back("Selected engine " + version +
                                          " differs from historical association " +
                                          context.engine_association);
    }
    context.mounts.push_back({"/Game", "git",
                              (fs::u8path(context.project_path).parent_path() / "Content").generic_u8string(),
                              context.project_path});
    std::set<std::string> disabled, references;
    if (primary.contains("Plugins")) {
        if (!primary.at("Plugins").is_array())
            throw Error("git-project-format", "Project Plugins must be an array");
        for (const auto &reference : primary.at("Plugins")) {
            if (!reference.is_object() || !reference.contains("Name") || !reference.at("Name").is_string() ||
                (reference.contains("Enabled") && !reference.at("Enabled").is_boolean()))
                throw Error("git-project-format", "Invalid project plugin reference");
            auto name = fold(reference.at("Name").get<std::string>());
            if (!references.insert(name).second)
                issue(context, "Duplicate project plugin reference: " + name);
            if (reference.contains("Enabled") && reference.at("Enabled") == false)
                disabled.insert(name);
        }
    }
    std::uint64_t total = 0;
    for (const auto &document : documents)
        total += document.file.content_bytes;
    if (!context.engine.root.empty()) {
        context.mounts.push_back(
            {"/Engine", "engine-local", (context.engine.root / "Engine" / "Content").u8string(), ""});
        auto build =
            local_document(context.engine.root / "Engine" / "Build" / "Build.version", options, total);
        if (!build.error.empty())
            throw Error("git-project-format", build.error);
        context.engine_build = build.descriptor;
        if (options.engine_plugins) {
            std::vector<fs::path> pending;
            auto plugins = context.engine.root / "Engine" / "Plugins";
            if (fs::is_directory(plugins))
                pending.push_back(plugins);
            std::uint32_t entries = 0;
            while (!pending.empty()) {
                auto directory = pending.back();
                pending.pop_back();
                std::vector<fs::path> children, found;
                std::error_code error;
                fs::directory_iterator it(directory, error), end;
                if (error) {
                    issue(context, "Cannot enumerate engine plugin directory: " + directory.u8string());
                    continue;
                }
                while (!error && it != end) {
                    if (++entries > options.max_tree_entries)
                        throw Error("git-project-budget", "Engine plugin tree entry limit exceeded");
                    if (it->is_symlink()) {
                        it.increment(error);
                        continue;
                    }
                    if (it->is_directory())
                        children.push_back(it->path());
                    else if (it->is_regular_file() && it->path().extension() == ".uplugin")
                        found.push_back(it->path());
                    it.increment(error);
                }
                if (error)
                    issue(context, "Cannot complete engine plugin directory scan: " + directory.u8string());
                if (found.empty()) {
                    pending.insert(pending.end(), children.begin(), children.end());
                    continue;
                }
                for (const auto &file : found) {
                    if (documents.size() > options.max_plugins)
                        throw Error("git-project-budget", "Combined plugin descriptor limit exceeded");
                    documents.push_back(local_document(file, options, total));
                }
            }
        }
    }
    // Parents precede nested descriptors. Project sources precede local engine sources.
    std::sort(documents.begin() + 1, documents.end(), [](const auto &a, const auto &b) {
        if (a.file.blob_oid.empty() != b.file.blob_oid.empty())
            return !a.file.blob_oid.empty();
        // Count separators instead of comparing iterators belonging to temporaries.
        auto da = std::count(a.file.path.begin(), a.file.path.end(), '/') +
                  std::count(a.file.path.begin(), a.file.path.end(), '\\');
        auto db = std::count(b.file.path.begin(), b.file.path.end(), '/') +
                  std::count(b.file.path.begin(), b.file.path.end(), '\\');
        return da == db ? a.file.path < b.file.path : da < db;
    });
    std::vector<std::string> roots;
    std::set<std::string> project_names, source_names, conflicts;
    for (std::size_t i = 1; i < documents.size(); ++i) {
        auto &document = documents[i];
        auto path = fs::u8path(document.file.path);
        auto directory = path.parent_path().generic_u8string();
        bool nested = false;
        for (const auto &root : roots)
            if (inside(directory, root)) {
                nested = true;
                break;
            }
        if (nested)
            continue;
        roots.push_back(directory);
        auto name = path.stem().u8string(), id = fold(name);
        const bool local = document.file.blob_oid.empty();
        if (local && project_names.count(id))
            continue;
        if (!local)
            project_names.insert(id);
        if (!source_names.insert((local ? "engine:" : "git:") + id).second) {
            conflicts.insert(id);
            context.mounts.erase(std::remove_if(context.mounts.begin(), context.mounts.end(),
                                                [&](const auto &m) { return fold(m.name) == "/" + id; }),
                                 context.mounts.end());
            issue(context, "Ambiguous plugin content mount: /" + name);
            continue;
        }
        if (!document.error.empty()) {
            issue(context, document.file.path + ": " + document.error);
            continue;
        }
        if (!mount_name(name) || id == "game" || id == "engine") {
            issue(context, "Invalid plugin mount name: " + name);
            continue;
        }
        if (document.descriptor.contains("CanContainContent") &&
            !document.descriptor.at("CanContainContent").is_boolean()) {
            issue(context, "CanContainContent is not boolean: " + document.file.path);
            continue;
        }
        if (disabled.count(id) || !document.descriptor.value("CanContainContent", false))
            continue;
        if (conflicts.count(id))
            continue;
        context.mounts.push_back({"/" + name, local ? "engine-local" : "git",
                                  (path.parent_path() / "Content").generic_u8string(), document.file.path});
    }
    std::sort(context.mounts.begin(), context.mounts.end(),
              [](const auto &a, const auto &b) { return a.name < b.name; });
    context.documents = std::move(documents);
    return context;
}
} // namespace detail
} // namespace cyuediff
