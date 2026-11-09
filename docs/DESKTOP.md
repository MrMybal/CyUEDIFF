# Desktop application — 0.87

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

The desktop restores its theme, language, **Load previews** and **Include engine content** options from `%LOCALAPPDATA%/CyUEDiff/preferences.json`, then saves them at normal shutdown. The first launch defaults to English and mid-grey. Preferences also retain native window placement and the opt-in session setting. Asset/project/repository paths and revisions belong only to explicit workspaces or the separate optional session file. With session restoration disabled, launch opens an empty document.

`--preferences FILE.json` selects an explicit preferences file. `--no-preferences` disables both reads and writes. Explicit `--locale`, `--theme`, `--previews`/`--no-previews` and `--include-engine`/`--exclude-engine` override saved values. `--include-engine` retains its existing requirement for `--browse` when supplied on the command line. The include-engine preference applies to the next local content scan.

Preferences use `cyuediff.preferences/2`, including nullable `window` and boolean `restoreSession` fields; schema 1 remains readable. Reads are bounded to 64 KiB and reject duplicate fields, unsupported versions/keys, unknown themes/locales and invalid types. A malformed or future file produces a warning and English/default settings, and remains unchanged. Saves validate existing contents, flush a new file in the same directory and atomically rename it over recognized preferences. Asset/companion/context/Git-storage protections also apply to preference outputs. A save failure is reported separately from document errors.

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

Launch with `--workspace Review.json`; do not combine it with source arguments or `--new-document`. `--document-index` and preview flags can override saved choices. `--smoke --save-workspace Review.json` records a workspace through the hidden validation host. Workspace data cannot select/load codec modules. Optional last-session restoration is described below. Version-two workspace files additionally record supported inspector/container filters and identity-based selections, graph/image transforms and the mesh camera. Version three additionally records the per-document sidebar width, named context/diagnostic expansion, object/property splits, property/material/member column proportions and property preview height. Drag the sidebar edge or a table divider to resize; double-click the sidebar/preview handle to reset its default. The input pane scrolls when its controls exceed the available height. Native window geometry is retained separately in preferences; dialog layout, general scroll and active gestures are not persisted. Format, limits, path rules and command examples are documented in [workspace files](WORKSPACES.md).


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

## Collision geometry review - 0.82

The Collisions tab prepares stored body-local geometry outside the frame loop. Choose one body, then all its stored shapes or one exact shape identity. Before/after panes share scale and camera navigation. Click a wire to select its shape in the typed inspector; Alt-drag orbits, middle-drag pans and the wheel zooms. Changed-shapes filtering, fit-changes and camera presets aid review. Explicit workspace files retain the camera, shape and property selection. Bodies are not placed in a common skeleton/world space. Unavailable geometry and preview limits remain visible while stored properties stay inspectable.

## Raw reference skeleton review - 0.87

The Skeleton tab presents a read-only union of stored raw bone hierarchies. Search/select a bone to compare local transforms and parents; node outlines mark stored changes. The Skeleton properties section retains UObject metadata and native tail fingerprints. Explicit workspace files retain object/bone selection, typed property filter/preview and graph canvas. Connections may include both before/after parents. No animation, virtual-bone reconstruction or skinning occurs.

## Reference pose preview - 0.92

In the Skeleton tab, choose Reference pose to inspect prepared affine joint positions from supported Skeleton or SkeletalMesh objects. Before/after share a camera and stack vertically in narrow panels. Click a joint or select its exact name in Bone to inspect its stored local transform and derived position. The displayed displacement is after minus before, in stored coordinate units. Moved joints only filters position/parent changes; hierarchy outlines continue to compare the stored bone fields. Alt-drag orbits, middle-drag pans, and the wheel zooms. Fit reference pose frames the visible prepared positions.

Pose mode, camera, selected bone and pose property inspector state are saved in explicit workspace files. The renderer limits work to 4,096 joint identities and marks positions outside the display camera range unavailable without changing Core data. Unknown rotations or missing ancestor poses never receive defaults. Animation, skinning and mesh vertices are not evaluated.

Windows output guards resolve the nearest existing ancestor before comparing missing companion/output paths. This also covers long and 8.3 temporary Git materialization directories, while existing input and hard-link protection remains active.

## Stored retarget source review - 0.97

The Skeleton Reference pose view adds independent Retarget source before/after selectors when stored sources are available. Stored raw reference chooses the raw local transforms explicitly. The selections use exact source-map names; the stored display pose labels remain inspectable in metadata. Prepared affine positions, moved-joint filtering, picking and typed local/derived details follow each selected source. Raw hierarchy graph outlines remain independent.

Explicit workspace files retain both source keys, the selected bone, pose camera and property state. A removed saved source stays selected and unavailable. It never silently becomes the raw pose. An unread opposite pose also leaves joint correspondence unknown instead of claiming that its joints were removed. Stored sources with unbound transform counts stay unavailable for preview. No engine or referenced source mesh is opened by these controls.

## Stored curve ranges and sampled comparisons - 0.121

The Curves tab supports independent stored owners and lanes, key inspection and an evaluated input coordinate. Enable **Custom time range** to set finite ordered endpoints. A comparison also exposes **Value tolerance**, sampled changed/unavailable counts and maximum finite delta. Agreement covers the displayed grid only. Stored-key status remains independent of the tolerance; a changed key can coexist with zero changed samples. Missing settings keep gaps outside the known domain. These controls, selected key and typed inspector state persist in explicit workspace files. English and French labels and all five themes are supported.

## Animation transform curve families - 0.126

The Curves tab exposes **Animation curve family** for AnimDataModel owners. Float is the default; Transform selects explicitly stored named transform groups. The lane selector labels translation, Euler rotation and scale XYZ components in the active language. Family, exact component and inspector choices persist in workspace files and process restarts. Missing component tables retain unknown opposite data and unavailable values. Cached projections prepare drawing data without loading packages during drawing.

## Independent animation source frames - 0.131

The Animations tab can compare shared or independent before/after source indices. Position, orientation (degrees) and scale tolerances persist in workspace files and process restarts. The selected-track summary shows local values within tolerance, changed values or unavailable comparison; raw key status and typed channel values remain separately inspectable. Both cursors appear on the cached source-key plot. Chart clicks choose the before frame in independent mode. Unknown opposite identity tables never confirm a removed named track.


## Explicit source animation Skeleton bindings - 0.138–0.142

For **Local files**, enable **Evaluate source hierarchy pose**. Supply a Skeleton for each loaded animation side, choose a shared integer source frame or independent indices, then **Load**. The file pickers choose loose packages; paths such as `/Game/Rig` use that side’s explicit project/engine context. Stored owner selectors may be left blank only when the package contains a sole supported owner. Bindings are explicit and case-sensitive; no external reference or missing-track default is inferred.

The worker resolves and caches the supplied inputs, samples the selected source frames and publishes bounded pose geometry. In **Animations**, **Show source hierarchy pose** switches between prepared component-space origins and the existing local-track review. Position tolerance, joint selection/filter and synchronized camera remain presentation state. Change frames on the left and Load to prepare another selection. Missing Skeletons, invalid stored owners and unavailable channels retain package inspection and explicit unavailable pose results. Diagnostics state the failed side; failures cannot certify added/removed bones.

Workspace format `cyuediff.workspace/4` saves per-document paths, selectors, shared/independent frames and review state. Formats one through three remain readable with bindings disabled when omitted. Relative loose Skeleton files resolve against the workspace location; mounted names retain their project meaning. No codec is loaded from a workspace. Duplication, close/reopen, tab switching and source-pose reload preserve their validated data-only review state.

```powershell
cyuediff_desktop --before Walk.uasset --view animations --pose-before-skeleton Rig.uasset --pose-frame 10
cyuediff_desktop --before Old.uasset --after New.uasset --view animations --pose-before-skeleton OldRig.uasset --pose-after-skeleton NewRig.uasset --pose-before-frame 10 --pose-after-frame 12
```

Optional `--pose-before-object`, `--pose-after-object`, `--pose-before-skeleton-object` and `--pose-after-skeleton-object` select exact stored owners. Each animation side requires its own supplied Skeleton; use the same file twice explicitly when intended. All active/inactive input paths and companions remain protected against report/capture/workspace overwrites. External Skeleton associations support loose and historical Git inputs. Cooked animation binding remains separate work.

For repeatable read-only validation, `scripts/validate_desktop_poses.py --asset FILE --skeleton RIG --out-dir build-native/REPORT` compares first/middle/last native results with the public CLI. Repeat `--asset` for additional files; `--capture`, `--locale` and `--theme` select optional middle-frame screenshots. Outputs stay below build-native and never share a directory containing source inputs.


## Historical Git animation Skeleton bindings - 0.143–0.147

In **Git revisions**, enable **Evaluate source hierarchy pose** and enter a Skeleton path for each revision. Repository-relative paths such as `Content/Rig.uasset` are read from the same immutable commit as that side’s animation. Mounted names such as `/Game/Rig` or `/MyPlugin/Rig` require the historical project descriptor. Each side resolves its own historical descriptor/plugin catalogue, including independent project selections. Local file pickers are available for loose inputs.

```powershell
cyuediff_desktop --repo C:/Repo --before-revision HEAD~1 --after-revision HEAD `
  --before Content/Clip.uasset --pose-before-skeleton Content/Rig.uasset `
  --pose-after-skeleton Content/Rig.uasset --pose-before-frame 10 --pose-after-frame 20
```

An omitted `--after` in Git mode means the same animation path in the after revision, so both Skeleton choices are active. History revision changes retain existing Git binding selections. Workspace format four preserves revision-relative and mounted Skeleton identities. Duplicate/reload/close/reopen retain source-pose review settings. Missing or corrupt local LFS objects disable the affected pose while preserving package inspection and the known side. Animation additions/removals evaluate only their present sides.

Reports keep animation package comparison separate from derived source-pose comparison. A Skeleton-only edit can leave the animation package identical while `animationPose.comparison` records changed joint origins. `beforeBinding` and `afterBinding` retain the requested name, pinned animation commit and materialized revision with blob/LFS/content hashes. Engine content selected through a historical context is explicitly labeled `engine-local`: its bytes come from the selected local installation and are not claimed to be historical Git engine bytes.

The [standalone Git SDK consumer](../examples/git_animation_pose_consumer) demonstrates this workflow without CyUI or the desktop. Its optional final project arguments resolve historical repository mounts with engine discovery disabled. The reusable `scripts/validate_git_desktop_poses.py` requires explicit original animation/Skeleton files and a compiled installed consumer, then checks first/middle/last frame pairs against the original Core CLI and native desktop. Validation outputs stay under `build-native` outside the source repository.


## Animated socket origins - 0.148–0.152

After loading an animation with an explicit Skeleton, open **Animations**, enable **Show source hierarchy pose**, then **Show source socket origins**. The worker prepares these points from that side's selected source frame and Skeleton definitions, including historical Git/LFS inputs. This view shows socket origins only. Select the exact socket object path to inspect its raw parent name, ready position and before/after displacement. Use the position tolerance, moved-socket filter and shared camera to review changes. Socket details and viewport use the same membership certainty and position comparison.

Unavailable local locations, parents or definitions stay explicit. They do not become implicit zero offsets or removed sockets. The socket scope can remain ready when an unrelated bone source track is missing. Bone-pose and socket-pose readiness are reported separately under `animationPose` and `animationPose.socketOrigins`.

Workspace format four retains `animationSockets`, `animationSocketsPreview` and the existing source-pose mode. Selection/filter, position tolerance and camera persist independently of the bone review across reload, duplicate and close/reopen. Existing workspaces remain readable. The reusable `AnimationPoseView::set_socket_poses` accepts host-prepared Core results and performs no package reads or hierarchy evaluation while drawing. It owns at most 4,096 displayed socket identities, marks truncation incomplete and does not retain source affine matrices.

## Window placement policy — 0.153

The desktop model provides checked outer-window screen-pixel placement, monitor selection by overlap then distance, DPI size conversion, minimum usable dimensions and work-area clamping. Negative monitor coordinates and maximized intent are retained. Disconnected monitors and smaller work areas have deterministic fallbacks. The policy does not depend on CyUI or affect the Core SDK.

## Native window restoration and preference migration — 0.154

Preference schema 2 stores an optional checked normal window rectangle, its DPI and maximized state. Schema 1 remains readable and migrates on a permitted save; invalid or future files retain their exact bytes. Windows restores a fitted rectangle before showing the application, remembers the normal rectangle while maximized or minimized, and keeps the window alive until clean-close persistence finishes. Smoke validation stays hidden and never reads or writes the default user profile.

## Bounded session persistence — 0.155

Optional session storage reuses workspace schema 4, its eight-document limit, input validation, absolute local path normalization and atomic replacement. Loose, historical Git and explicit cooked contexts retain their meaning; decoder DLL paths, decoded content, callbacks and closed history are excluded. A missing session creates no files, and malformed, oversized, future or unrelated files are never overwritten. Custom preferences use a distinct deterministic sibling session path.

## Optional previous-session restoration — 0.156

View → Restore previous session on startup enables clean-close session saving and restoration on the next launch; it is off by default. Sessions use a deterministic sibling of the preferences file, or an explicit --session path. Explicit source arguments and --workspace take priority. Every restored ready document is queued for a fresh read, including explicit source Skeleton bindings and retained views. Unsupported host features or unsafe output overlap preserve the existing session with a diagnostic. --no-restore-session overrides the saved setting. Smoke runs never derive session/profile paths and require --session plus --save-session for a session write. Session/report/preferences/workspace output aliases and protected source paths are rejected. View → Reset window placement recovers the default fitted size.

## Native lifecycle and distribution validation — 0.157

Native lifecycle tests verify hidden restoration, actual HWND rectangles, remembered movement, reset behavior and final placement access after WM_CLOSE. Session restart coverage checks opt-in preferences, retained animation/socket selection, explicit launch precedence, Git revision bindings, cooked profile retention, no-Git fallback, read-only damaged sessions and output alias protection. Final configuration, distribution and secret-scan results are recorded in VALIDATION.md.

## Smoke action deadlines — 0.162

The hidden smoke runner grants a separate 30-second loading phase to each explicit scripted document action, including reload and duplication. A completed initial load no longer consumes the next action's loading budget. The phase starts before the action is invoked, so synchronous action work is included. The existing 64-action limit and per-Git-process timeout remain in effect. Longer multi-case historical CTest suites have 360-second aggregate budgets, and the desktop Git model suite has a 240-second budget. Per-case timing is logged to distinguish cumulative test duration from an individual stalled load. These changes concern hidden validation and do not alter interactive document loading.

## Cooked pixels — post-0.181 development

With an explicit UE 5.3 profile, matching texture property schemas and global script objects, inspecting a supported Texture2D member prepares its mip previews off-thread. **Cooked pixels** selects before/after objects, formats and mipmaps and exposes zoom, fitting, sample differences and changed-region fitting. English is the default; **Pixels cooked** supplies the French interface. The selected chapter, mip/side and viewport are retained in workspaces. HDR comparisons use floating samples before display conversion. Unsupported native layouts remain in texture diagnostics. See [COOKED_TEXTURES.md](COOKED_TEXTURES.md).

## Cooked static meshes — post-0.181 development

Choose UE 5.3 Windows and the producer mobile-LOD setting in **Cooked mesh settings**. Inspecting a StaticMesh member with a matching property/schema/global context prepares its available LODs on the worker. **Cooked meshes** selects object/LOD and before/after sides, with orbit navigation and colored changes. The profile, producer setting, tab, selection and camera survive workspace format seven; older workspaces retain their previous readers. Native tails and unavailable channels remain explicit. English/French native captures and Core/CLI/SDK evidence are recorded in [COOKED_MESHES.md](COOKED_MESHES.md) and VALIDATION.md.

## Mapped cooked collision review — post-0.181 development

With matching BodySetup property bindings, **Cooked collisions** displays worker-prepared stored shapes and body-local wires. Select bodies/shapes, inspect values and compare known before/after geometry with a retained camera. The EN/FR interface preserves chapter 7 and the collision state in workspaces. Missing peer geometry stays unavailable rather than being painted as deleted. Native physics remains explicitly opaque. See COOKED_COLLISIONS.md.

## Native Chaos review — post-0.181 development

**Cooked physics settings** selects an explicit UE 5.3 Windows native reader for
before and after independently. With matching schema/global/Zen contexts,
**Native physics** (**Physique native**) displays worker-prepared native convex
and triangle contours in stored cooker coordinates. Body/shape selection, picking,
synchronized camera and changed-shape navigation reuse the collision review;
parameters and native topology/acceleration/maps remain inspectable. Unavailable
peer data does not appear as removed geometry. Workspace format eight preserves
the independent profiles, chapter 8 and checked selection/camera state. See
[COOKED_PHYSICS.md](COOKED_PHYSICS.md).


## Cooked compressed animation review — post-0.181 development

The **Containers → Cooked animation settings** section selects explicit UE 5.3 Windows readers, producer interpolation and the exact compression-settings/Skeleton members independently before/after. Separate dependency TOCs are optional; empty paths use the main input on that side. **After member override** can compare different animation IDs. Use **Cooked animations** for named track search/change navigation, encoding versus selected-frame statuses, independent fractional frames, local TRS metrics/tolerances, bounded channel plots, stored-key markers and focused original-key values. Names require verified Skeleton binding; unavailable peers remain unknown. Decoding and review preparation run on workers. The interface uses stored compressed local channels without runtime retargeting, additive bases or skinning. Enable **Scalar curves** for independently sampled compressed curve values, encoding/value statuses, traces and exact keys. Curve-settings TOCs/member IDs are explicit before/after. Version-eleven workspaces retain both contexts and navigation. See [the full contract and flags](COOKED_ANIMATIONS.md#focused-queries-and-desktop-review).

## Native Nanite review — post-0.181 development

The **Nanite** container tab now exposes the selected UE 5.3 Windows reader and exact surface/shading comparison in the source-built desktop. Each side keeps its own cooked/mesh/Nanite profile, producer mobile-LOD fact, mapping, script objects and resource choice. Full-leaf and selected-cluster wires, aligned corner values, focused native vertices, stored hierarchy and next-change navigation are available in EN/FR. Missing data remains unknown; verified empty resources can establish removal. Workspace 12 retains bindings and presentation state. See [the complete Nanite view contract and launch example](NANITE_GEOMETRY.md#cyui-and-windows-desktop-review--post-0181-development). The published 0.181 archive predates this development feature.

## Named cooked skeletal weights — post-0.181 development

The existing cooked mesh **Bones and weights** inspector now includes the selected reader's named alternate profiles, stored under `profile:<name>` beside `default`. Select a vertex to inspect exact raw before/after weights by reference bone name, filter the property table by profile, or navigate changed skin entries. Source contexts, camera and inspector state use the existing workspace format. When both member IDs are explicitly selected and each package contains one mesh export, equal LOD indices can pair different source object names; reports preserve both identities and the pairing scope. No implicit cross-object pairing or weight normalization is introduced. See [the complete native profile contract](COOKED_SKELETAL.md#named-alternate-cooked-weight-profiles--post-0181-development).

## Cooked compressed render morphs — post-0.181 development

The selected UE 5.3 Windows reader now exposes immutable compressed morph buffers, exact stored index/position/normal values and explicit native target-name bindings through Core/CLI/SDK. Geometry differences preserve sparse zero semantics and empty target inventory; unknown names remain incomplete. The reusable EN/FR render-morph inspector adds target/vertex selection, clipped lists, search, changed-vertex navigation, focused deltas, wire highlights and persistent workspace state. Actual cooked/reference buffer hashes and Engine source-delta quantization validate the implementation. See [the full contract](COMPRESSED_MORPHS.md). This closes this selected stored-buffer profile; broader native/plugin/runtime/cloth/producer coverage and final distribution remain required for overall completion. No push or release is implied.

## Cooked cloth mapping inspection — post-0.181 development

Stored UE 5.3 Windows cloth section bindings, per-LOD-bias render vertex mappings and matching native render buffers now have a reusable typed Core model, geometric difference projection and EN/FR desktop inspector. Section/bias/vertex selection, filters, change navigation, incident-edge highlights and focused binding/coefficient values persist in workspaces. Actual cooked references validate the selected mapping scope. Physical cloth assets/configuration, simulation bounds/runtime evaluation, further native/plugin/producer coverage and final distribution remain open. See [CLOTH.md](CLOTH.md). This is local source development and does not publish a new release.


### Retained skeletal vertex-overlap tables — post-0.181 development

The selected UE 5.3 Windows Core/CLI/SDK reader now exposes typed raw section-local overlap arrays, exact native fingerprints, focused render-vertex queries and scoped relation differences. Aggregate raw/resolved limits prevent window expansion; stripped tables remain unavailable. Desktop wires highlight changed known relations through the existing EN/FR mesh views. See [MESH_OVERLAPS.md](MESH_OVERLAPS.md). Custom skeletal attributes, physical cloth assets, broader native/plugin/producer/runtime coverage and final distribution remain open; this milestone does not complete the full product goal or imply a new archive/push/release.
