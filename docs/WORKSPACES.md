# Workspace files

The optional desktop host has a data-only `cyuediff.workspace/12` JSON format (post-0.181 development). It records one to eight documents in order, the active document index, source and project/engine/schema contexts, preview options, initial asset/container view and supported presentation state. It does not serialize asset bytes, runtime view objects, GPU resources, caches, callbacks, closed-document history, appearance preferences or codec modules. A workspace file does not certify that its source files or schemas still match a build.

Local filesystem paths are saved as absolute paths. Relative paths in a file being opened resolve against that file's parent directory. Git asset/project paths remain repository-relative, and mounted names such as `/Game/Example` remain mounted when a project context is present. Git revision expressions are retained as entered, so a later opening can resolve a moving branch differently. No checkout, network fetch or source modification is performed.

The reader limits a file to 1 MiB, eight documents, 32,768 UTF-8 bytes per string and JSON nesting depth 24. Unknown versions/fields, duplicate fields, malformed UTF-8, control characters, invalid types/ranges and ambiguous drive-relative paths are rejected. It reads context data without opening packages, mappings or codecs. Missing files produce an error instead of creating a workspace.

Saving validates the complete document first, reserves a unique file in the destination directory, flushes it and atomically replaces the destination. Existing files must already be valid workspace files; unrelated, malformed and future-version files are preserved. The native host additionally checks output paths against source packages, companions, container partitions, mappings, Git/LFS storage and engine metadata.


## Open and save

Use **File → Open workspace...** (`Ctrl+Alt+O`) to replace the open tabs with a saved workspace. The complete format, feature support and output guards are checked before the old documents are cleared. All restored ready contexts are queued for a fresh read; a later read error belongs to its own tab. Opening a workspace clears the session's closed-document history and cancels old work. Save the current workspace first if you want to return to it.

**Save workspace** (`Ctrl+S`) writes the current workspace path, or opens a native file chooser for a new workspace. **Save workspace as...** (`Ctrl+Shift+S`) always chooses another path. Explicit workspace saving is independent of the optional previous-session setting. With that setting enabled, a separate bounded session file retains the last open document contexts on clean close. Current asset/container tabs and the cooked property/object mode are recorded. Supported filters, identity-based selections and navigation are saved with each document; the scope and bounds are described below.

```powershell
cyuediff_desktop.exe --workspace D:/Work/Review.json
cyuediff_desktop.exe --smoke --save-workspace Review.json --before Before.uasset --after After.uasset --new-document --container Build.pak
cyuediff_desktop.exe --smoke --workspace Review.json --report restored.json
```

`--workspace` cannot be mixed with source options or `--new-document`. `--document-index` can override the saved active tab; `--previews`/`--no-previews` override the saved preview options. Appearance/preferences options remain independent. An explicit `--codec DLL` can supply a local codec when launching a workspace; workspace data cannot select a codec. A desktop build without Git rejects a workspace containing Git documents before replacing existing tabs. `--save-workspace FILE.json` requires smoke mode; the interactive host uses the File menu.

The smoke harness also accepts `workspace-save:PATH` and `workspace-open:PATH` through `--document-action`; these call the same helpers as the menus. Reports add `workspaceFile` with the current path, whether the run imported/saved a file and a data-only context snapshot. This context is distinct from the existing `workspace` report of decoded documents and status. Capture/report/preferences outputs cannot overwrite the workspace input. Workspace saves retain the existing source/companion/partition/mapping/Git protection and may explicitly replace the current valid workspace file.


## Presentation state — 0.38–0.40

Version two adds a `viewState` object to each saved document. Version-one files remain readable and acquire an empty presentation state; their next explicit save upgrades to the current version. Older applications that understand only version one cannot read the new format. The outer file keeps the eight-document/1-MiB limits. Presentation strings/identity keys are limited to 4,096 UTF-8 bytes, each identity map to 128 entries and each state object to 256 KiB. Unknown fields, invalid types, control characters, non-finite/out-of-range transforms and invalid camera vectors are rejected before applying state.

The desktop restores object filters, before/after display choices, raw/typed inspection, the selected export by object path, and the current inspector's search/change filter and selected property path on each side. Blueprint graphs use their stable graph identity; material documents and texture sources use object paths. Their pan/zoom and the mesh's orbit camera are restored after model preparation and preview binding. The texture difference switch only applies when a comparable difference preview exists. Unfitted views retain their normal fit behavior. Captures keep at most 128 graph/image transforms, prioritizing the selected item. Asset property inspectors are saved for the current object on each side, rather than for every export ever visited.

Container state includes member/object filters, package/change filters, highlighted member, cooked object identity and the inspector filters/selected property for byte details, object metadata, mapped values, comparison and schema provenance. Restoring a highlighted member does not inspect or decode it: the saved source request still determines which member's bytes are loaded. Missing or hidden property/container selections clear; missing asset/graph choices use the current data's default. No stale numeric export/row index can select an unrelated item. Existing semantic coverage and unknown-peer rules remain unchanged.

Restoration runs on the owner thread outside drawing. The desktop retains pending state for documents that have not yet been displayed, including failed reads, so saving again does not erase their state. Selecting a ready tab prepares/binds its view and consumes that pending state once. Selecting a loaded tab still does not decode again. The explicit workspace save/open flow remains separate from preferences and session-local closed-tab history.

The embedding APIs expose `view_state()` and `restore_view_state(Json)` on the individual views and `AssetView`; bind data and GPU previews first, then restore. The header-only contract in `cyuediff/view_state.hpp` has no ImGui/CyUI dependency. It never contains resource handles or invokes readers/codecs. Hosts still own decoding, scheduling and resources. `--document-action view-state:JSON` exercises these same APIs in hidden validation; reports expose each document's current or pending `viewState`.

This is bounded presentation restoration, not a complete ImGui layout dump. General panel scroll, selected nodes, active drag/edit gestures and content-browser/history dialogs are not persisted. Native window geometry is retained separately in preferences. Version three adds the bounded panel/column and diagnostic/context layouts described below. Diff split/search/vertical scroll are recorded, but numeric hunk/row selections are not. Camera/canvas coordinates retain the saved pose rather than automatically refitting for another window size; the existing Fit controls remain available. Previous-session restoration is off by default and can be enabled in the View menu; see [desktop session behavior](DESKTOP.md#optional-previous-session-restoration--0156).


## Layout contract - 0.41

Version three accepts bounded layout metadata inside `viewState.layout` and each view's `layout` object. Version-one and version-two files remain readable; an explicit save writes version three. Earlier applications reject the newer format. Sidebar widths use logical units (180-640), preview heights use fractions (0.1-0.7), and table columns use positive finite weights with an exact count for each table. Diagnostic/context sections use named boolean fields. No ImGui INI text, window identifiers, arbitrary section names or native handles are accepted. Validation remains data-only and runs before replacing a workspace or applying any view state.


## Reusable layout views - 0.42

The asset object/property split, typed property columns, material parameter columns, container member columns and cooked object/property split now use resizable stretch proportions. Single-value and comparison tables keep separate column configurations. Property previews have a draggable horizontal divider; double-click it to restore the default height. Blueprint/material/container diagnostic expansion is part of each view's snapshot. Locale changes do not change table identities.

Restore validated layout state after binding data. A restore also resets the corresponding live table instance so saved proportions replace any previous interactive resize. The implementation reads ImGui's resulting stretch weights in one isolated UI adapter; only numeric proportions are serialized. Core and the installed Core SDK do not depend on this adapter or CyUI. The desktop integrates these snapshots with explicit workspace save/open.


## Native layout restoration - 0.43

Each document keeps its own sidebar width and project, historical project, LFS, schema, context-change and diagnostic section flags. The sidebar can be dragged at its inner right edge; double-click this handle to restore 310 logical units. Its displayed width is capped at 45% of the viewport, preserving the requested width for a later larger window or different DPI. The input controls have a scrolling child region when they exceed the available height. Table columns use stretch proportions and adapt to the remaining width.

Shell layout applies immediately when importing a workspace, independently of asset preparation. Pending view state for unvisited or failed documents remains available for saving. This prevents a slow or failed read from discarding the sidebar layout or overwriting edits made while loading. Layout-only state can also be saved for an empty document. Entire state validation precedes any mutation, including mixed shell/view updates. No automatic session restoration is enabled.

For example, the following document `viewState` keeps an expanded project section, an object/property split and comparison inspector columns:

```json
{
  "layout": {"sidebarWidth": 390, "project": true},
  "asset": {
    "layout": {"objectColumns": [1, 2]},
    "before": {
      "properties": {
        "TestAsset": {
          "layout": {
            "comparisonColumns": [2, 1, 2, 2, 1],
            "previewFraction": 0.45
          }
        }
      }
    }
  }
}
```

Single-value tables use `singleColumns` with three weights, comparison tables `comparisonColumns` with five, container members `memberColumns` with four, and object/property splits `objectColumns` with two. Weights are relative; an unchanged saved layout is retained exactly rather than rewritten with floating-point rounding on every frame. Material layouts share a view's parameter column configuration; node selection and canvas poses retain the existing independent contracts.

## Native structure descriptions — 0.176

Workspace format five adds per-document `layouts`, `after_layouts` and `inherit_after_layouts` settings for the declarative native reader. The after description inherits by default; explicitly disabling inheritance and leaving its path empty selects the built-in reader. Paths normalize relative to the workspace file and are protected from output overwrites. Formats one through four still load without overriding their readers. See [binary layout controls](BINARY_LAYOUTS.md#desktop-descriptions--0176).

## Cooked pixel selection — post-0.181 development

Container chapter 5 selects prepared cooked pixels. Container presentation state can retain `pixel` (the exact object/format/mip identity), `pixelAfter` and a checked `pixelView` texture viewport. Restoration resolves that identity against freshly prepared data and ignores stale selections; resource handles and decoded bytes are not persisted. Existing workspace versions and chapters remain readable.

## Cooked mesh context — post-0.181 development

Format seven adds checked per-side native mesh profiles and producer mobile-LOD settings. `mesh_profile` / `after_mesh_profile` select disabled (0) or UE 5.3 Windows (1); `mesh_mobile_lod` / `after_mesh_mobile_lod` select unknown (0), explicitly off (1) or explicitly on (2). Unknown never becomes an inferred setting. Formats one through six remain readable with native mesh decoding disabled; saving upgrades to seven. Unknown or missing version-seven fields remain errors.

Container chapter 6 selects prepared cooked render geometry. The `mesh` identity combines exact object path and LOD; `meshAfter` selects its side and `meshView` retains a validated camera/change filter. The source reader profile and settings are retained independently from this presentation state. Resource handles, decoded geometry and decoder modules are excluded. See [COOKED_MESHES.md](COOKED_MESHES.md).

## Cooked collision review — post-0.181 development

Container chapter 7 selects mapped cooked collisions. The `container.collisions` state reuses the checked collision body/shape, viewport and property-inspector contract. Restoration resolves stored identities against freshly prepared models; no decoded vertices or runtime handles are persisted. Context paths retain the existing per-side schema/global/profile controls. See COOKED_COLLISIONS.md.

## Version eight native physics context and review

Format 8 introduced the native physics settings. Each document retains `physics_profile` and
`after_physics_profile` independently (0 disabled, 1 explicit UE 5.3 Windows).
Container chapter 8 and `container.physics` retain the checked collision-style
body/shape, viewport and property-inspector state. Runtime geometry, native buffers
and codec handles are not persisted. Formats 1–7 still open; the new reader
profiles remain disabled on migration. See [COOKED_PHYSICS.md](COOKED_PHYSICS.md).

## Version nine cooked skeletal context and review

Format 9 adds independent before/after cooked skeletal profiles, producer mobile-LOD presence and color fallback facts. The selected cooked mesh keeps its orbit state plus an optional bones/weights inspector, identity/filter and focused property state. Formats 1–8 migrate with skeletal decoding disabled and unknown producer facts; no reader or color default is invented.


## Cooked animation contexts — post-0.181 development

Version ten adds independent before/after native animation profiles, interpolation facts, settings/Skeleton container paths and literal member IDs, plus an optional independent after-member selection. Only physical dependency paths are normalized. Formats one through nine remain readable and gain disabled native animation profiles, unknown interpolation and empty dependency/after-member selections. Their compatibility or defaults are not inferred.

The container's optional `beforeAnimation`/`afterAnimation` store exact object paths (or null). Its `animation` state retains a stable named/numeric track key, nonnegative fractional `frame`/`afterFrame`, `independentFrames`, one of ten TRS components, filters, change-only mode, position/rotation/scale tolerances, original key indices and focused property state. Missing saved objects stay unavailable instead of selecting another animation. Restored tracks scroll into view when present. Worker results publish only for the latest request; pending inspector state survives preparation. Native animation is container tab 9. Section expansion uses the `animation-native` layout key. The existing data-only size/type/finite-value limits and output protections apply.


## Version eleven compressed scalar curves

Format 11 adds `curve_settings_container`, `after_curve_settings_container`, `curve_settings_entry` and `after_curve_settings_entry`. TOC paths normalize relative to the workspace; member IDs remain literal. Before/after contexts are independent. Formats 1–10 remain readable: format 10 preserves bone animation settings and leaves curve dependencies empty, while earlier formats retain their previous migration behavior. No codec compatibility is inferred.

`container.animation.scalarMode` selects the scalar view; its `curves` object retains exact `selected` name identity, `time` / `afterTime` seconds, `independentTimes`, `valueTolerance`, `emptyDefault`, `rangeStart` / `rangeEnd`, original `beforeKey` / `afterKey` indices, `filter`, `changedOnly` and checked `properties` inspector state. Times/defaults/range are finite native float32, range start must precede end and tolerance is finite/nonnegative. Unknown fields/types/ranges are refused before host resources change. Decoded buffers, sampled values, runtime name permutations and worker state are excluded. See [COOKED_ANIMATIONS.md](COOKED_ANIMATIONS.md#desktop-compressed-scalar-curves).

## Cooked Nanite contexts — post-0.181 development

Version twelve adds independent `nanite_profile`/`after_nanite_profile` and `nanite_attributes`/`after_nanite_attributes` settings, plus container tab 10. Formats one through eleven migrate with Nanite decoding disabled and attribute requests enabled only for a later explicit profile selection. No native producer compatibility is inferred.

The optional container `beforeNanite`/`afterNanite` fields store exact object paths or null; stale identities clear instead of selecting another resource. `nanite` presentation state retains side, full-leaf/selected-cluster scope, attribute comparison scope, page/cluster/vertex/triangle/hierarchy indices, inspector mode, filters, property path, camera and panel proportions. Bounds and field types are validated before replacing current state. Resource bindings are restored before focused values and camera. The full contract and unavailable/empty semantics are in [NANITE_GEOMETRY.md](NANITE_GEOMETRY.md).
