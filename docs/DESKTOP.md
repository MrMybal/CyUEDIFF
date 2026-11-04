# Desktop application — 0.77

`cyuediff_desktop` is an optional Windows host for the CyUI views. It inspects local Unreal packages and compares local Git/LFS revisions without launching Unreal Editor. The Core SDK, CLI, Git adapter and embeddable views remain separate targets.

## Build and launch

```powershell
python scripts/build.py --desktop --cyui D:/path/to/CyUI
build-native/Release/cyuediff_desktop.exe
build-native/Release/cyuediff_desktop.exe before/BP_Player.uasset after/BP_Player.uasset --locale fr
```

`CYUEDIFF_BUILD_DESKTOP=ON` enables the UI library and the Win32/Direct3D 11 host. The matching ImGui backends come from the supplied CyUI checkout; a host providing its own CyUI targets must also set `CYUEDIFF_IMGUI_BACKENDS_DIR`. Builds do not download dependencies. The window uses hardware rendering with WARP fallback; hidden validation forces WARP.

English is the default interface language, with French second. Select **View → Language → Français**, or use `--locale fr`. The same menu selects the five CyUI themes. Asset names, paths, serialized properties and reader diagnostics retain their data values.

## Local packages

Select **Local files**, enter or choose a before package and an optional after package, then select **Load**. `Ctrl+O` chooses before, `Ctrl+Shift+O` chooses after, and `Ctrl+R` reloads. **Swap files** exchanges and loads both paths.

An optional local `.uproject` and engine root enable mounted package names such as `/Game/BP_Player`. The same selected context applies to both sides. The engine supplies paths and metadata; the file's recorded serialization versions still select the reader profile.

```powershell
build-native/Release/cyuediff_desktop.exe /Game/BP_Player --project D:/Game/Game.uproject --engine-root D:/Unreal/UE_5.5
```

## Git/LFS revisions

Select **Git revisions** and enter a local repository directory, a before reference, an after reference and an asset path. References default to `HEAD~1` and `HEAD`; branch names, tags and commit IDs are accepted. Paths are repository-relative, with forward slashes. An empty after path reuses the before path; specify a different after path for a renamed asset. **Swap revisions** exchanges references and any explicit paths, then loads the comparison.

```powershell
build-native/Release/cyuediff_desktop.exe --repo D:/Projects/Game --before-revision HEAD~1 --after-revision HEAD --before Content/BP_Player.uasset --view comparison
# A renamed asset:
build-native/Release/cyuediff_desktop.exe --repo D:/Projects/Game --before-revision old --after-revision new --before Content/Old.uasset --after Content/New.uasset
# A historical project descriptor, resolved independently at each commit:
build-native/Release/cyuediff_desktop.exe --repo D:/Projects/Game --before Content/BP_Player.uasset --project Game.uproject --no-engine
```

The **Historical project** section accepts a repository-relative `.uproject` path, shared by both sides. With this context, asset paths can use `/Game/...`, `/Engine/...` or plugin mounts. Each revision reads its own project/plugin descriptors. **Discover engine** controls local engine discovery; an explicit engine root requires discovery enabled. The selected local engine root is shared by both sides. Independent project paths/engine roots per side remain available through the Git API and CLI.

The sidebar displays the resolved commit IDs. Added or removed assets keep the missing side explicit, inspect the present side by default and expose a comparison against absence. Changes in project context appear separately; they can produce **Changed** even when the package diff is identical. An incomplete context produces **Incomplete input**.

Git/LFS reads run on the document worker. Each side owns its matching companion files and materializations for as long as the document uses their payloads. Locally available LFS objects are verified by size and SHA-256. The **LFS cache** section or `--lfs-objects DIR` selects an explicit objects directory; otherwise repository LFS settings apply. Missing objects produce an error. Reads never fetch, check out revisions, run filters or change the repository. Git 2.45+ must be available in PATH.

The Git controls are present when `CYUEDIFF_BUILD_GIT=ON`. A desktop build with Git disabled remains functional for loose files and rejects Git requests explicitly. References and paths are entered as text; the content browser below can supply asset paths. The commit history browser below can supply both revision IDs.

## Content browser

After choosing a project, select **Browse content...** or press `Ctrl+B`. In Git mode, a repository and both references are sufficient. The modal scans in the background, filters package names/paths and provides an asset/map selector. Double-click or choose **Open package** to inspect a local asset; **Compare revisions** loads the selected Git path on both sides. **Use before** and **Use after** fill one side while retaining the other. The default action starts a fresh single-asset inspection/comparison; the one-side actions preserve the other path.

Local scans include mounted project/plugin content and exclude local engine/engine-plugin content by default. Toggle **Include engine content** and refresh to include it. Git scans list repository-wide package paths in both commits, independently of historical plugin activation. Presence labels describe exact path existence. Catalogues use filenames, file sizes and Git tree metadata; assets, maps, LFS pointers and invalid/unreadable package contents are not decoded or validated until opened. A catalogue can be browsed while LFS content is unavailable.

```powershell
build-native/Release/cyuediff_desktop.exe --browse --project D:/Game/Game.uproject
build-native/Release/cyuediff_desktop.exe --browse --repo D:/Game --before-revision HEAD~1 --after-revision HEAD
```

Search filtering is cached between edits and list rendering clips off-screen rows. Closing or refreshing a scan signals cancellation; only the newest requested catalogue can publish. Pending work is bounded to one request. A failure retains the previous catalogue while disabling selection until a successful refresh. A selection pins the resolved commit IDs from its catalogue. See [CATALOG.md](CATALOG.md) for budgets and cancellation limits.

## Views and document lifetime

Properties inspects either present side, with object filtering, native JSON and opaque ranges. Blueprint and Materials show serialized graphs; Textures shows available source previews; Meshes provides a navigable source wireframe. Comparison shows snapshots side by side, focused on the first change, with CyUI search, next-change and unified display controls.

One worker loads packages, comparisons and CPU previews. One pending request is retained; newer requests supersede pending work and stale results. Both sides publish atomically. A failed load keeps the previous document visible. The worker never calls ImGui or Direct3D.

The main thread prepares the views and uploads images before the next frame. Replacing or closing a document clears image bindings before releasing GPU resources. Views perform no file reads or decoding during `draw`. View preparation can still cost a frame for large documents. Active decoding is not cancellable; shutdown waits for the active worker operation.

The host prepares the first texture source and first mesh source/LOD 0 per side. Texture budgets are 16 million pixels and 128 MiB for payload/decoder data; mesh payloads are limited to 128 MiB with the Core's element limits. **Load previews** can be disabled. An optional codec must be supplied explicitly with `--codec DLL`; it is trusted host code and is never discovered from an asset or engine directory. No Oodle SDK or binary is distributed. See [CODECS.md](CODECS.md).

Independent contexts per side, active cancellation/progress, verified package caching and the typed inspector are available; see [RUNTIME.md](RUNTIME.md). Commit history, multiple documents and persistent preferences are available as documented below. Configurable application-specific decoder profiles remain future work. Material evaluation, skinned mesh rendering and Nanite rendering retain the Core's documented limitations.

## Native validation

```powershell
build-native/Release/cyuediff_desktop.exe --smoke --before Content/BP_Player.uasset --view blueprint --locale fr --capture build-native/preview.png --report build-native/preview.json
python scripts/validate_desktop_git.py --asset D:/Owned/BP_Player.uasset --after D:/Owned/BP_Player_changed.uasset --texture D:/Owned/T_Water.uasset --mesh D:/Owned/SM_Rock.uasset
```

`--smoke` renders at least twelve settled frames in a hidden DX11/WARP window. `--view` accepts `properties`, `blueprint`, `materials`, `textures`, `meshes` and `comparison`. Capture/report require smoke mode. Outputs overlapping input assets, companions, Git metadata, LFS storage or selected engine context are rejected. Exit code `0` means the document loaded, even when changed; `2` means loading/capture failed. The document report exposes `status`, the package `diff` and the optional complete `git` comparison, including project contexts and immutable commit IDs.

The Windows Release configuration passes nineteen CTest suites with 914 checks, thirty renderer-free UI frames and a native renderer smoke test. `catalog` covers 21 checks, `desktop_browser` covers 25 checks and `desktop_git` covers 35 integration checks; the UI frames also exercise additions/removals. The desktop configuration without Git passes sixteen suites. The Core-only configuration passes twelve suites.

`scripts/validate_desktop_git.py` produced 22 native captures, six option/output refusals, desktop/CLI Git diff parity and unchanged index/checkout/source fingerprints. It uses a fresh owned repository with generated local LFS pointers and copies of explicitly selected UE 5.5.4/5.3.2 assets. Captures were reviewed in all five themes, both languages and the relevant views. The existing loose-file validator also covers 21 captures and seven refusals. Generated assets, repositories, reports and images are ignored and never committed.

`scripts/validate_browser.py` adds 21 native captures and five output refusals. It checks both languages and all themes, local/engine/plugin content, repository-wide Git presence, filtering, local opening and Git comparison, missing LFS content, error states and CLI parity. Browser-only capture is available with `--smoke --browse`; `--browse-filter TEXT` presets search. The controlled `--browse-select PATH` option requires smoke mode and opens an entry through the same selection path for validation. Reports include the complete browser metadata, filtered count and document if opened. Captures and generated repositories are ignored.

HDR previews and difference masks retain floating-point source comparisons; see [HDR.md](HDR.md). A successfully loaded preview keeps source diagnostic details in a collapsible section.

## Persistent preferences — 0.22

The desktop restores its theme, language, **Load previews** and **Include engine content** options from `%LOCALAPPDATA%/CyUEDiff/preferences.json`, then saves them at normal shutdown. The first launch defaults to English and mid-grey. Asset/project/repository paths, revisions, codec paths and document contents are not stored. Opening an application does not reopen previous assets or scan a previous project.

`--preferences FILE.json` selects an explicit preferences file. `--no-preferences` disables both reads and writes. Explicit `--locale`, `--theme`, `--previews`/`--no-previews` and `--include-engine`/`--exclude-engine` override saved values. `--include-engine` retains its existing requirement for `--browse` when supplied on the command line. The include-engine preference applies to the next local content scan.

Preferences use the flat `cyuediff.preferences/1` JSON schema. Reads are bounded to 64 KiB and reject duplicate fields, unsupported versions/keys, unknown themes/locales and invalid types. A malformed or future file produces a warning and English/default settings, and remains unchanged. Saves validate existing contents, flush a new file in the same directory and atomically rename it over recognized preferences. Asset/companion/context/Git-storage protections also apply to preference outputs. A save failure is reported separately from document errors.

Hidden `--smoke` validation never accesses default user preferences. An explicit `--preferences FILE.json` loads only that file; adding `--save-preferences` also exercises the save path. The JSON smoke report includes active values, loaded/saved flags and a separate diagnostic. These test options can validate restart behavior in an isolated directory:

```powershell
build-native/Release/cyuediff_desktop.exe --smoke --preferences build-native/test-preferences.json --save-preferences --theme violet --locale fr
build-native/Release/cyuediff_desktop.exe --smoke --preferences build-native/test-preferences.json --report build-native/preferences-restart.json
```

## Git history browser — 0.24

In **Git revisions**, choose a repository and select **Browse history...** (also in the File menu). **Start revision** defaults to `HEAD`; **Refresh history** resolves it again. Each page contains up to 100 local commits in newest-first topological order, with commit ID, author date, author and subject. The date column is an author timestamp, not a chronological sorting guarantee. Search filters only the current page; **Previous page** and **Next page** keep the original resolved head fixed. Refresh starts again from the typed revision.

Select a row and choose **Use as before**, then choose another row and **Use as after**. Choices can come from different pages and retain their full immutable IDs. **Copy commit ID** copies the selected row's full ID. **Compare selected revisions** becomes available after both sides have been chosen: it loads the current asset paths, or opens the catalogue of those two commits when no asset is selected. The normal Git/LFS reader then compares the selected asset. History itself reads only commit metadata.

```powershell
build-native/Release/cyuediff_desktop.exe --repo D:/Projects/Game --history
build-native/Release/cyuediff_desktop.exe --repo D:/Projects/Game --history --history-revision main
```

History runs on a separate worker. Refresh/page requests cancel earlier work, retain at most one pending request and publish only the latest result. Closing the dialog cancels active reading. Errors retain the previous page with selection actions disabled. The dialog, search and actions are available in English and French with all five themes. Builds without the Git adapter omit the controls and reject history launch requests. No checkout, fetch or history mutation occurs. Shallow repositories expose only their locally available ancestry.

Hidden validation accepts `--history-filter TEXT` and, with `--smoke --history`, `--history-before ID --history-after ID --history-compare`. Both IDs must be visible on the loaded page; the latter flag exercises the same selection-to-comparison action used by the dialog. History metadata and current selection are included in the smoke JSON report. Capture, report and preference outputs retain the active history repository's Git-storage protection.

## Pak/IoStore browser — 0.26

Select **Containers**, or **File → Open container...**, and choose a supported `.pak` or `.utoc`. **Index container** lists virtual paths/chunk IDs, decoded sizes, chunk types and live/deleted/encrypted state. Search matches paths and IDs; **Zen packages only** restricts IoStore rows to export-bundle chunks. The table clips off-screen rows. Indexing reads metadata and does not load a codec or decode member payloads.

Select a live member and choose **Inspect selected member** to read decoded bytes, their SHA-256 and a hex prefix limited to 64 bytes. Member names never become extraction destinations; no package is written to disk. For an IoStore export bundle, choose **UE 5.3** or **UE 5.5** explicitly. An optional **Global script TOC** resolves script classes/references from its unique type-5 chunk. **Codec module** accepts an explicit host-provided decoder; it loads only for a member read. No engine or codec is discovered or launched automatically.

The **Cooked objects** tab shows exports by name, with full-path tooltips and a typed inspector for class, references, offsets, sizes and body fingerprints. **Container diagnostics** exposes format/verification limitations. Without an explicit schema context, this desktop path inspects Zen object tables and opaque bodies. It does not reconstruct defaults, interpret Pak-contained packages, or render cooked Blueprints, textures or meshes. Explicit mapped reads are available in the desktop, Core and CLI. The selected engine layout never implies unsupported native payload coverage.

```powershell
build-native/Release/cyuediff_desktop.exe --container D:/Build/Content.pak
build-native/Release/cyuediff_desktop.exe --container D:/Build/Content.utoc --container-filter BP_Player
build-native/Release/cyuediff_desktop.exe --container D:/Build/Content.utoc --container-entry CHUNK_ID --cooked-profile ue5.3 --script-objects D:/Build/global.utoc --codec D:/Host/cyuediff_oodle.dll
```

Requests use the same cancellable background document worker as loose/Git assets. A new request reopens current indexes; no previous index is reused to read changed source files. One request per document waits at most in the shared queue, and stale results cannot publish. A failed inspection retains the previous document. All physical container partitions, global-script sources and the explicit codec path are protected against overlapping capture/report/preference outputs. No path is persisted in user preferences. Containers also work in builds without Git.

Desktop limits are 100,000 indexed entries, a 64 MiB index, 64 MiB per decoded member/global-script chunk, 16 MiB Zen headers and 16 MiB Zen text. The Core has separate configurable limits. Hidden `--smoke` reports include `document.container` with summary/inspection/Zen data and `containerView` with visible rows, selected member/object and active tab. Full member tables are not duplicated in the report.

## Container comparisons — 0.28

Enter an **After container** alongside the first container, then choose **Index comparison**. Live members pair by exact virtual path, or by chunk ID when unnamed. The table labels additions/removals immediately; common members remain **Unchecked**. Tombstones are omitted from comparisons. A shared size or stored hash never establishes decoded equality. The size/type columns describe the after member when present, otherwise the before member.

**Compare all decoded members** explicitly reads and hashes every live member on both sides, with a 512 MiB aggregate decoded-byte budget and a 64 MiB per-member limit. The preflight rejects an oversized comparison before decoding its members. This bounds decoding work, not total heap usage. Large containers can still be indexed and individual members compared with this option off. There is no decoded-container cache; a new inspection reopens both sources and repeats a full scan if this option remains enabled. Cancellation and errors retain the previously displayed document; partial equality is never published.

**Inspect selected member** reads the matched pair, or the sole present side. **Inspection** displays before/after decoded sizes, hashes and bounded prefixes. Explicit cooked profiles and global script TOCs are independent per side; an empty after profile means byte inspection. **Cooked objects** matches exports by resolved object path, marks added/removed/modified/unchanged objects and uses the typed property inspector for before/after values. Matching uses the Core snapshot, so serialization offsets alone do not create object changes. Native bodies remain opaque hashes. If a present peer has no Zen decode, its objects remain unknown and the decoded side is inspected without removal/addition claims.

**Comparison** exposes the selected member's byte/Zen diff, including changed interpretation contexts, or the complete decoded-member diff when no member is selected. **Hide unchanged** filters verified equal members and object snapshots; unchecked members stay visible. Container byte equality does not imply matching cooked contexts or equality of external bulk data. Capture/report/preferences protection includes both containers, all their physical partitions and both global-script sources. Nothing is extracted or persisted as an asset.

```powershell
cyuediff_desktop.exe --container Before.pak --after-container After.pak --compare-container-bytes
cyuediff_desktop.exe --container Before.utoc --after-container After.utoc --container-entry CHUNK_ID --cooked-profile ue5.3 --after-cooked-profile ue5.3 --script-objects Before/global.utoc --after-script-objects After/global.utoc --codec D:/Host/cyuediff_oodle.dll
```

`--container-tab members|inspection|objects|comparison` selects an available tab. `--container-changes-only` initially hides verified unchanged rows and requires an after container. Smoke reports add `after`, paired member identities/states, `comparison` and `selectedComparison` under `document.container`; the original single-container fields remain available. `containerView.selectedKey` identifies a canonical pair, while `selectedMember` retains a displayed member ID. Both workflows are available without the Git adapter.

## Explicit desktop schema contexts — 0.30

Container requests accept `--schema CONTEXT.json` and `--after-schema CONTEXT.json`. Each side requires its own explicit cooked profile. An empty after schema selects object/opaque inspection on that side; it does not inherit the before schema. A manifest must match the selected profile and applies only to IoStore Zen export bundles.

Indexing reads only manifest metadata and records its SHA-256 plus the declared mapping path/hash. It does not load the mapping or codec. Inspecting a selected package loads the explicitly supplied mapping, verifies its byte fingerprint and supplies the immutable bindings to the same Zen reader as the CLI. Desktop mapping limits are 16 MiB serialized/decoded/text bytes and 100,000 entries. The ordinary member/header/text limits still apply separately.

Reports add `schemaContext` provenance on each side, including `mappingLoaded`, `contextSha256` and `mappingVerified:false`. A loaded mapping is not proof of compatibility with a particular build. Failed property regions retain their full opaque body hashes and decoding errors; limits and cancellation abort the request. Capture/report/preferences guards protect both manifests and both resolved mappings, including hard links, even during metadata-only indexing.

## Property and schema inspection — 0.31

Expand **Schema contexts (optional)** in the container sidebar. **Before schema** and **After schema** accept the context JSON files described above; native chooser buttons select existing JSON files without reading assets during drawing. Select explicit cooked profiles and global script TOCs as required by the build, then index/inspect the selected member. Selecting a file only changes the settings; the normal background request applies them. Context paths are not persisted in appearance preferences.

In **Cooked objects**, **Decoded properties** displays the stored named values with types, search, change filtering and selected previews. Successfully mapped objects open this mode automatically. **Object details** shows class/reference metadata and complete opaque body fingerprints. Values are paired only when both present object regions decode, or when a missing export establishes a true one-sided object. If just one present side decodes, its properties appear as a single-value inspection with an explicit unknown-peer notice. Missing schemas and failed/unsupported regions never imply removed properties. Changing back to an index clears the previous object's property table.

The **Schema context** tab shows each manifest's source/mapping paths and fingerprints, loaded state, context digest and selected property-region status, offsets/counts/errors. It does not duplicate all property values in the provenance view. `mappingVerified:false` and unresolved defaults remain explicit even after successful reads. A byte fingerprint certifies the selected mapping bytes, not their compatibility with a particular cooked build.

`--container-tab schema` selects provenance when a schema is supplied. `--cooked-view properties|object` selects the initial object inspector mode. Smoke reports include `containerView.objectProperties` with the actual mode, availability, per-side decode status, comparison state and visible property rows. The original Core/CLI snapshot diff is retained in the comparison report, including changes caused by context/coverage differences; the property view indicates when peer values are unknown.

```powershell
cyuediff_desktop.exe --container Before.utoc --after-container After.utoc --container-entry CHUNK_ID --cooked-profile ue5.3 --after-cooked-profile ue5.3 --script-objects Before/global.utoc --after-script-objects After/global.utoc --schema Before/schema.json --after-schema After/schema.json
```

Mapped UI coverage currently uses synthetic fixtures. The supplied CyTrace build still has no matching USMAP; no real mapped-value or native cooked-payload coverage is implied.

## Document workspace model — 0.32

The desktop model now exposes `DocumentWorkspace`: stable document IDs, independent retained snapshots/errors and a single `DocumentSession` worker. At most one pending request is kept per document. A newer request replaces queued work for that ID; different documents queue in order. Selecting a tab does not load files, and failures retain only that document's previous snapshot. All requests share the existing bounded worker package cache unless a host explicitly supplies another cache.

The default maximum is eight open documents, configurable from one to 32 by an embedding host. This bounds document count and queue size, not aggregate decoded/GPU memory. Cancelling or closing a running document invalidates its publication and advances the queue after cooperative cancellation. Closing queued work drops it before decoding. Completed snapshots transfer out of the worker so it does not keep a closed document alive. Saved requests retain context data without callback/cache closures.

## Native document tabs — 0.33

**New document** (`Ctrl+N` or the tab bar's `+`) creates an independent context. Load a loose comparison, a local Git comparison or a Pak/IoStore view in each tab. **Close document** (`Ctrl+W` or the tab close button) releases that tab's views/resources and cancels its work. Closing the last tab leaves an empty document. The default limit is eight tabs.

Switching tabs preserves their input settings, selected objects, filters, canvas/inspector state and uploaded previews. View objects keep stable addresses, and each document has its own ImGui identity scope. Views and GPU previews initialize on first display outside drawing; selecting a loaded tab does not decode its files again. Reload explicitly reads current sources. Cancelling one tab leaves other documents' queued requests intact. Content-browser/history navigation closes when switching documents so it cannot apply a selection to another context.

The launch syntax groups source options with `--new-document`. Appearance, report/capture, preference and automatic browser/history options belong to the first group. `--document-index 0..7` chooses the initially visible group; otherwise the last opens as active. Smoke mode waits for all queued documents and reports background failures as failures. Reports retain the active `document` and add `workspace.documents` with per-tab results/statuses. Input protection covers initial, open and session-local closed document contexts, including inactive containers, companions, mappings and global partitions.

```powershell
cyuediff_desktop.exe --before Before.uasset --after After.uasset --new-document --container Build.pak
cyuediff_desktop.exe --document-index 0 --container Before.utoc --new-document --container After.utoc
```


## Document lifecycle commands — 0.34

The **File** menu and tab context menu expose **Duplicate document**, **Close document** and **Close other documents**. Duplicate (`Ctrl+D`) copies the input context into a new tab and reads its current sources. Each copy has its own snapshot and views. **Reopen closed document** (`Ctrl+Shift+T`, File menu) restores the most recently closed context in a new tab and reads it again. Closing other documents keeps the chosen tab and cancels/drops the others' work as one operation. `Ctrl+Tab` and `Ctrl+Shift+Tab` cycle through the current tab order. New and duplicate/reopen actions respect the eight-tab limit. Closing the last tab creates an empty document with the same preview/content-display preferences.

Up to eight closed contexts are remembered in memory, containing source/settings data only. Decoded snapshots, GPU previews, filters and callback/cache closures are released when a tab closes. Reopening restores context, not the previous view state. The closed list disappears when the process exits. Appearance preferences continue to store only theme, language and preview/content options.

Hidden validation accepts repeated `--document-action` options before `--new-document`, with up to 64 actions. Supported actions include `select:N`, `close[:N]`, `duplicate[:N]`, `new`, `reopen`, `close-others`, `next`, `previous` and `reload`. Indices are zero-based positions in the current tab order. `member:ID_OR_PATH` selects a visible container row without reading it; `filter:TEXT` changes the container filter. Actions run after the shared queue settles and call the same document helpers as the menus. Reports include the action results, per-document revision counts and the number of remembered closed documents. They are validation reports, not restorable workspace files.

```powershell
cyuediff_desktop.exe --smoke --report workspace.json --document-action duplicate --document-action close --document-action reopen --before Before.uasset --after After.uasset
```


## Workspace save and restore — 0.35–0.37

**File → Open workspace...** (`Ctrl+Alt+O`), **Save workspace** (`Ctrl+S`) and **Save workspace as...** (`Ctrl+Shift+S`) manage explicit JSON workspace files. The files restore document order, active tab, source/project/engine/schema contexts, Git revision expressions, preview options and initial asset/container views. Current files are read again through the shared queue. An invalid import leaves the open documents intact. Workspace replacement clears closed-tab history and cancels previous work.

Launch with `--workspace Review.json`; do not combine it with source arguments or `--new-document`. `--document-index` and preview flags can override saved choices. `--smoke --save-workspace Review.json` records a workspace through the hidden validation host. Workspace data cannot select/load codec modules, and no automatic workspace/profile restoration is enabled. Version-two workspace files additionally record supported inspector/container filters and identity-based selections, graph/image transforms and the mesh camera. Version three additionally records the per-document sidebar width, named context/diagnostic expansion, object/property splits, property/material/member column proportions and property preview height. Drag the sidebar edge or a table divider to resize; double-click the sidebar/preview handle to reset its default. The input pane scrolls when its controls exceed the available height. Native window geometry, dialog layout, general scroll and active gestures are not persisted. Format, limits, path rules and command examples are documented in [workspace files](WORKSPACES.md).


## Native cooked property layouts - 0.46

A version-two schema context can explicitly bind native math/color/GUID structs in a mapped property stream. Choose that context through the existing before/after schema inputs; class offsets and build compatibility remain the host's responsibility. The cooked object inspector shows decoded components and their changes, including translation/rotation/scale members of a transform. Search, change filters, selected previews and workspace save/restore use the same paths as other properties. Schema provenance includes the native binding names and selected serialized layouts. These layouts decode property values; skeletal/Nanite/collision/native export payload readers remain separate work. See [native schema contexts](SCHEMAS.md).

## Cooked text and persistent reference values - 0.49

Supported modern text histories, delegates and weak package references appear under **Cooked objects > Decoded properties** when an explicit matching schema binds the export. Compare stored text sources or namespace/key/table identities, multicast functions and object targets; search and select a leaf for its full before/after preview. Delegate order and duplicate/null bindings remain visible. A workspace preserves the selected path and filter through reopening in either language. Histories or malformed values outside the supported layout leave the complete property read opaque; details remain in the object report. These views do not perform localization, invoke functions or reconstruct live pointers. See [schema contracts](SCHEMAS.md).

## Optional values and field paths - 0.52

The cooked property inspector shows supported optional values as `kind`, `isSet` and a `value` only when set. An unset option has no invented inner value; a set null or zero remains distinct. Modern field paths show the original `path` segments and `owner` reference. Compare a presence marker, segment or owner, then select a leaf to view its complete before/after text. Filters and selections persist through workspace save/reopen in both languages. UE 5.5 enables the checked optional format; both supported profiles enable modern field paths subject to recorded custom versions. No reflected field lookup or runtime pointer resolution occurs. See [schema contracts](SCHEMAS.md).

## Stored text formats and byte strings - 0.57

Properties in loose packages and mapped cooked objects show recursive text transforms, named/ordered arguments and number/percent/currency metadata. Select a changed source, argument value, culture, currency code or rounding option for its before/after preview. The reader shows stored inputs without generating a localized display string. Counted UTF-8 strings retain their Unicode bytes; ANSI reading covers ASCII. Filters and selections, including nested arguments, survive workspace save/reopen in either UI language. Unsupported histories, malformed encodings and duplicate named-format argument names retain opaque data and fingerprints. See [schema contracts](SCHEMAS.md).

On a host built with 16-bit ImGui characters, supplementary Unicode scalars appear as distinct JSON surrogate escapes in typed previews. Accents remain readable, and parsing an untruncated preview recovers the exact original value. This avoids two different unsupported glyphs appearing identical. Stored UTF-8, semantic comparisons, property identities and workspace selections stay unchanged.

## Calendar, argument-data and native bounds - 0.62

The existing property inspector compares stored date/time ticks, styles, timezone, culture and custom patterns. Argument-data histories retain an ordered array, including duplicate names; select an argument's name, type or value independently. Named-format histories still reject duplicate map identities. Native schema bindings additionally expose DateTime/Timespan ticks, box min/max/validity, sphere center/radius and combined origin/extents/radius. Search, leaf previews, independent contexts and workspace restart retain their existing behavior in English and French. These are serialized property values; they do not evaluate localization or decode complete mesh/collision payloads. Standalone SDK readers explicitly select an argument-data integer width; Zen contexts use profile and recorded-version rules in [schema contracts](SCHEMAS.md).

## Generator fingerprints and native geometry - 0.67

Supported text generator histories expose the resolved generator name and opaque contents byte count/SHA-256. Select contents/sha256 for the full before/after preview; identical-size binary changes remain visible. Unknown payload contents are not evaluated. Blueprint friendly/default pin texts use the same structured text decoder, so their metadata changes participate in stable node highlighting. Classic tagged boxes/spheres/combined bounds and matrices/planes also appear in the property inspector without a USMAP. Mapped values require an explicit matching context. Matrix cells use row/column paths, while planes retain normal components and distance; filters/selections survive workspace restart in English/French.

## Shared graph inspection - 0.68

The embeddable UI now has a shared read-only GraphInspector with stable snapshot identities, title/identity search, changed-node navigation, typed before/after node details and bounded validated state. Canvas identifiers are excluded from saved state. Node properties are prepared on selection and reused while drawing. This milestone supplies the component; Blueprint and Material hosts are integrated next.

## Blueprint node navigation - 0.69

Blueprint graphs now embed the shared inspector. Select a node on the canvas or in the searchable node list, navigate changed nodes and focus a selected node. The typed comparison includes stored pin text metadata, defaults, references and node properties. Search limits navigation results while retaining the complete graph and its connections. Selection, filters and property previews are saved by graph/node identity in explicit workspace files, independently for each side; restoring selection preserves saved pan/zoom.

## Material expression and override navigation - 0.70

Material expression graphs now use the searchable node inspector, including typed expression properties, saved inputs/outputs and before/after values. Changed-expression navigation uses snapshot identity and retains the full read-only graph. Stored material-instance overrides support name/kind/value/association search and a changed-only filter, separately per instance. These states survive explicit workspace save/restore. This inspects saved serialization only; parent materials and shader execution are not evaluated.

## Changed mesh focus - 0.71

Wireframe comparisons can hide unchanged edges and frame the bounds of changed geometry. Changed bounds are prepared with the bounded edge cache rather than recomputed while drawing; the existing 100,000-edge display cap remains explicit. Perspective/front/right/near-top camera presets complement orbit navigation. The changed-only preference and camera are saved per side. Empty filtered comparisons explain how to recover the full mesh; absent/unchanged geometry has no changed-focus target.

## Changed texture regions - 0.72

LDR and source-float HDR comparisons now return changedBounds (x/y/width/height, half-open coordinates), computed alongside thresholded changed-pixel counts even without a heatmap. Identical/incomparable images return null bounds. The texture view can frame this region, return to actual-pixel zoom and display changed percentage/mean absolute error. Bounds are validated against the bound image before navigation; image pan/zoom and mask selection remain workspace state. No new source formats or cooked texture payload decoders are implied.

See [visual review](VISUAL_REVIEW.md) for the complete node/instance/mesh/texture workflow, embedding APIs, workspace state and format limits.

## Stored collisions - 0.77

The Collisions tab and --view collisions select stored bodies and shapes, navigate changes, inspect typed before/after values and save those selections/filters in explicit workspace files. See [collision contracts](COLLISIONS.md). Native physics blobs and absent struct defaults remain explicit.
