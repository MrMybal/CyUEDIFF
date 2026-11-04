#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct CatalogOptions {
    std::uint32_t max_assets = 100'000, max_entries = 1'000'000, max_depth = 64;
    std::uint64_t max_path_bytes = 64ull * 1024 * 1024;
    std::uint64_t max_listing_bytes = 32ull * 1024 * 1024; // Git tree output only.
    bool include_engine = false;                           // Local engine content and engine plugin mounts.
    std::function<bool()> cancelled;
};
struct CatalogAsset {
    std::string path, package_name, mount, extension, blob_oid;
    std::uint64_t stored_bytes = 0; // Filesystem size or Git blob size; may be an LFS pointer.
    Json to_json() const;
};
struct AssetCatalog {
    std::string storage = "local", revision;
    std::vector<CatalogAsset> assets;
    std::vector<std::string> diagnostics;
    std::uint64_t visited_entries = 0, visited_path_bytes = 0;
    bool complete = true, cancelled = false;
    Json to_json() const;
};
struct CatalogItem {
    std::string path, label, extension;
    bool before = false, after = false;
};
// Sorted union by exact path. Presence is metadata, not semantic equivalence.
std::vector<CatalogItem> merge_catalogs(const AssetCatalog &before, const AssetCatalog *after = nullptr);
// Enumerate metadata only. Does not open package headers, hash payloads or follow links.
AssetCatalog catalog_assets(const ProjectContext &, const CatalogOptions &options = {});
} // namespace cyuediff
