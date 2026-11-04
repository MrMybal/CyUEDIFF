#include <cyuediff/catalog.hpp>
#include <algorithm>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace cyuediff {
namespace {
void issue(AssetCatalog &catalog, const std::string &message) {
    catalog.complete = false;
    if (catalog.diagnostics.size() < 128)
        catalog.diagnostics.push_back(message);
}
bool link(const fs::path &path, const fs::file_status &status) {
    if (fs::is_symlink(status))
        return true;
#ifdef _WIN32
    auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    (void)path;
    return false;
#endif
}
bool inside(const fs::path &path, const fs::path &directory) {
    auto relative = path.lexically_relative(directory);
    return path == directory || (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..");
}
} // namespace
Json CatalogAsset::to_json() const {
    Json result = {{"path", path},
                   {"package", package_name},
                   {"mount", mount},
                   {"extension", extension},
                   {"storedBytes", stored_bytes}};
    if (!blob_oid.empty())
        result["blobOid"] = blob_oid;
    return result;
}
Json AssetCatalog::to_json() const {
    Json entries = Json::array();
    for (const auto &asset : assets)
        entries.push_back(asset.to_json());
    return {{"schema", "cyuediff.asset-catalog/1"},
            {"storage", storage},
            {"revision", revision},
            {"complete", complete},
            {"cancelled", cancelled},
            {"visitedEntries", visited_entries},
            {"visitedPathBytes", visited_path_bytes},
            {"assets", entries},
            {"diagnostics", diagnostics}};
}
std::vector<CatalogItem> merge_catalogs(const AssetCatalog &before, const AssetCatalog *after) {
    std::map<std::string, CatalogItem> rows;
    for (const auto &asset : before.assets)
        rows[asset.path] = {asset.path, asset.package_name.empty() ? asset.path : asset.package_name,
                            asset.extension, true, false};
    if (after)
        for (const auto &asset : after->assets) {
            auto &row = rows[asset.path];
            row.path = asset.path;
            row.label = asset.package_name.empty() ? asset.path : asset.package_name;
            row.extension = asset.extension;
            row.after = true;
        }
    std::vector<CatalogItem> result;
    for (auto &pair : rows)
        result.push_back(std::move(pair.second));
    return result;
}
AssetCatalog catalog_assets(const ProjectContext &context, const CatalogOptions &options) {
    if (!options.max_assets || !options.max_entries || !options.max_path_bytes || options.max_depth > 256)
        throw Error("catalog-options",
                    "Asset, entry and path budgets must be nonzero; depth must not exceed 256");
    AssetCatalog result;
    auto cancelled = [&] {
        if (!options.cancelled || !options.cancelled())
            return false;
        result.cancelled = true;
        issue(result, "Catalogue scan cancelled");
        return true;
    };
    if (cancelled())
        return result;
    std::error_code error;
    fs::path engine;
    if (!context.engine.root.empty())
        engine = fs::weakly_canonical(fs::absolute(context.engine.root / "Engine"));
    std::set<fs::path> seen;
    auto mounts = context.mounts;
    std::sort(mounts.begin(), mounts.end(), [](const auto &a, const auto &b) { return a.name < b.name; });
    bool stopped = false;
    for (const auto &mount : mounts) {
        if (cancelled() || stopped)
            break;
        auto root = fs::weakly_canonical(fs::absolute(mount.directory), error);
        if (error) {
            issue(result, "Cannot resolve content mount: " + mount.name);
            error.clear();
            continue;
        }
        if (!options.include_engine && (mount.name == "/Engine" || (!engine.empty() && inside(root, engine))))
            continue;
        auto status = fs::symlink_status(mount.directory, error);
        if (error || !fs::is_directory(status)) {
            issue(result, "Content mount unavailable: " + mount.name);
            error.clear();
            continue;
        }
        if (link(mount.directory, status)) {
            issue(result, "Content mount is a link and was skipped: " + mount.name);
            continue;
        }
        struct Directory {
            fs::path path;
            std::uint32_t depth;
        };
        std::vector<Directory> pending = {{root, 0}};
        while (!pending.empty() && !stopped && !result.cancelled) {
            if (cancelled())
                break;
            auto directory = std::move(pending.back());
            pending.pop_back();
            fs::directory_iterator iterator(directory.path, error), end;
            if (error) {
                issue(result, "Cannot enumerate directory: " + directory.path.u8string());
                error.clear();
                continue;
            }
            while (iterator != end) {
                if (cancelled())
                    break;
                if (result.visited_entries >= options.max_entries) {
                    issue(result, "Catalogue entry limit reached");
                    stopped = true;
                    break;
                }
                ++result.visited_entries;
                auto entry = *iterator;
                auto name_bytes = entry.path().u8string().size();
                if (name_bytes > options.max_path_bytes - result.visited_path_bytes) {
                    issue(result, "Catalogue path byte limit reached");
                    stopped = true;
                    break;
                }
                result.visited_path_bytes += name_bytes;
                auto state = entry.symlink_status(error);
                if (error) {
                    issue(result, "Cannot inspect entry: " + entry.path().u8string());
                    error.clear();
                } else if (link(entry.path(), state)) {
                    issue(result, "Linked entry skipped: " + entry.path().u8string());
                } else if (fs::is_directory(state)) {
                    if (directory.depth >= options.max_depth)
                        issue(result, "Catalogue depth limit reached: " + entry.path().u8string());
                    else
                        pending.push_back({entry.path(), directory.depth + 1});
                } else if (fs::is_regular_file(state)) {
                    auto extension = entry.path().extension().u8string();
                    if ((extension == ".uasset" || extension == ".umap") &&
                        seen.insert(entry.path()).second) {
                        if (result.assets.size() >= options.max_assets) {
                            issue(result, "Catalogue asset limit reached");
                            stopped = true;
                            break;
                        }
                        auto size = entry.file_size(error);
                        if (error) {
                            issue(result, "Cannot read asset size: " + entry.path().u8string());
                            error.clear();
                        } else {
                            auto relative = entry.path().lexically_relative(root);
                            relative.replace_extension();
                            result.assets.push_back({entry.path().u8string(),
                                                     mount.name + "/" + relative.generic_u8string(),
                                                     mount.name,
                                                     extension,
                                                     {},
                                                     size});
                        }
                    }
                }
                iterator.increment(error);
                if (error) {
                    issue(result, "Directory changed or could not be read: " + directory.path.u8string());
                    error.clear();
                    break;
                }
            }
        }
    }
    std::sort(result.assets.begin(), result.assets.end(),
              [](const auto &a, const auto &b) { return a.path < b.path; });
    return result;
}
} // namespace cyuediff
