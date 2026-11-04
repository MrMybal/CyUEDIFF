# Asset catalogues — 0.11

Catalogues discover package paths efficiently before opening individual assets. They read directory entries, sizes or Git tree metadata. They never open package headers, hash payloads, decode asset types or materialize LFS objects. A `.uasset` or `.umap` suffix is a candidate package; successful listing does not certify that its contents are valid.

## Core API

```cpp
#include <cyuediff/catalog.hpp>

auto project = cyuediff::ProjectContext::open("Game.uproject");
cyuediff::CatalogOptions options;
options.include_engine = false;
auto catalog = cyuediff::catalog_assets(project, options);
for (const auto &asset : catalog.assets) {
    // asset.path is a local UTF-8 file path; package_name identifies its mount.
    // Open only when selected: cyuediff::Package::open(cyuediff::fs::u8path(asset.path)).
}
```

`CatalogAsset` records a path, optional long package name/mount, extension, stored main-file size and optional Git blob OID. Companion files are not catalogue rows. Local results are sorted by exact normalized path and duplicate paths from overlapping mounts are removed. `.uasset` and `.umap` with the same stem remain distinct entries; callers should open the explicit selected path.

Local scans use the supplied `ProjectContext` mounts and its existing project/plugin discovery rules. Engine content and plugins under the selected engine's `Engine` directory are excluded by default. Set `include_engine` to include them. Filesystem links and Windows reparse points encountered in the walk or at mount roots are skipped and reported. Unavailable mounts, filesystem errors and exceeded budgets preserve available entries with `complete=false`.

Defaults are 100,000 assets, 1,000,000 visited directory/tree entries, depth 64, 64 MiB of indexed path bytes and 32 MiB of Git tree output. API callers can adjust them. Depth zero lists only root-level packages; depth above 256 or zero asset/entry/path budgets are invalid. Diagnostics are capped at 128 entries. Results are sorted, but a limited or cancelled scan may contain a traversal-dependent subset.

`options.cancelled` is an optional cooperative callback. A stopped scan returns `cancelled=true` and `complete=false`. This covers the catalogue walk/tree parse. Project discovery and a running Git subprocess finish before the next cancellation check; Git subprocesses retain their timeout/output bounds. `complete` describes enumeration of the selected roots, not package validity, plugin activation completeness or filesystem snapshot isolation.

`AssetCatalog::to_json()` uses `cyuediff.asset-catalog/1`, with storage, resolved revision, completeness/cancellation, visited entry/path-byte counters, assets and diagnostics. No payload data is embedded. See [the installed SDK consumer](../examples/catalog_consumer/).

## Git adapter

```cpp
#include <cyuediff/git.hpp>
auto repository = cyuediff::GitRepository::open("D:/Projects/Game");
auto before = repository.catalog("HEAD~1");
auto after = repository.catalog("HEAD");
auto items = cyuediff::merge_catalogs(before, &after);
```

The adapter enumerates repository-wide regular package blobs, pins each reference to a commit and retains blob IDs. It skips non-portable paths, links and submodule package entries with explicit diagnostics. It does not read blob contents, resolve LFS pointers or require the corresponding LFS object. Stored size is the Git blob size, which may be a small pointer rather than the actual asset size. This listing is independent of historical project/plugin activation and local engine content; `include_engine` applies to local mount scans only.

`merge_catalogs` returns a sorted path union with `before`/`after` presence flags. These flags describe existence, not semantic change or rename detection. Open a selected path with the existing revision/comparison API to obtain those asset changes. Keep the selected catalogue's resolved commit IDs so a later branch move cannot silently change the selected comparison.

## Command line

```powershell
cyuediff catalog Game.uproject --engine-root D:/Unreal/UE_5.5
cyuediff catalog Game.uproject --include-engine --max-assets 10000 --out reports/catalog.json
cyuediff git-catalog D:/Projects/Game HEAD --max-entries 200000
```

Both commands write JSON and return `0` for complete enumeration or `2` for errors/partial results. `--max-assets` and `--max-entries` adjust the corresponding limits; `git-catalog` also accepts `--git-temp`. Outputs must be outside protected local source/engine content and Git metadata/LFS storage. Listed packages, companions and project descriptors are protected against output overlap, including existing hard-link aliases.

## Embeddable and desktop views

`CyUEDiff::UI` provides `<cyuediff/catalog_view.hpp>` with `CatalogView`. Supply immutable shared `CatalogItem` data, register translations and call `draw`. It returns a `CatalogAction`; the host handles opening/comparison outside drawing. Search results are cached until the query/type/catalogue changes and table rendering clips off-screen rows. English is default and French is built in. The view owns no repository, worker, window or filesystem scanner.

The Windows host supplies a cancellable latest-request worker and a modal opened with **Browse content...** (`Ctrl+B`). Local scans require a selected `.uproject`; Git scans require a repository and two references. The default action opens a fresh local inspection or Git comparison; the before/after actions replace one input while preserving the other. Catalogue errors retain the previous data while selection is disabled. See [DESKTOP.md](DESKTOP.md) for controls and native validation.
