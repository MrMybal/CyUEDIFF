# Workspace files

The optional desktop host has a data-only `cyuediff.workspace/2` JSON format. It records one to eight documents in order, the active document index, source and project/engine/schema contexts, preview options, initial asset/container view and supported presentation state. It does not serialize asset bytes, runtime view objects, GPU resources, caches, callbacks, closed-document history, appearance preferences or codec modules. A workspace file does not certify that its source files or schemas still match a build.

Local filesystem paths are saved as absolute paths. Relative paths in a file being opened resolve against that file's parent directory. Git asset/project paths remain repository-relative, and mounted names such as `/Game/Example` remain mounted when a project context is present. Git revision expressions are retained as entered, so a later opening can resolve a moving branch differently. No checkout, network fetch or source modification is performed.

The reader limits a file to 1 MiB, eight documents, 32,768 UTF-8 bytes per string and JSON nesting depth 24. Unknown versions/fields, duplicate fields, malformed UTF-8, control characters, invalid types/ranges and ambiguous drive-relative paths are rejected. It reads context data without opening packages, mappings or codecs. Missing files produce an error instead of creating a workspace.

Saving validates the complete document first, reserves a unique file in the destination directory, flushes it and atomically replaces the destination. Existing files must already be valid workspace files; unrelated, malformed and future-version files are preserved. The native host additionally checks output paths against source packages, companions, container partitions, mappings, Git/LFS storage and engine metadata.


## Open and save

Use **File → Open workspace...** (`Ctrl+Alt+O`) to replace the open tabs with a saved workspace. The complete format, feature support and output guards are checked before the old documents are cleared. All restored ready contexts are queued for a fresh read; a later read error belongs to its own tab. Opening a workspace clears the session's closed-document history and cancels old work. Save the current workspace first if you want to return to it.

**Save workspace** (`Ctrl+S`) writes the current workspace path, or opens a native file chooser for a new workspace. **Save workspace as...** (`Ctrl+Shift+S`) always chooses another path. Saving is explicit; the desktop does not automatically restore or save source paths through the user preference profile. Current asset/container tabs and the cooked property/object mode are recorded. Supported filters, identity-based selections and navigation are saved with each document; the scope and bounds are described below.

```powershell
cyuediff_desktop.exe --workspace D:/Work/Review.json
cyuediff_desktop.exe --smoke --save-workspace Review.json --before Before.uasset --after After.uasset --new-document --container Build.pak
cyuediff_desktop.exe --smoke --workspace Review.json --report restored.json
```

`--workspace` cannot be mixed with source options or `--new-document`. `--document-index` can override the saved active tab; `--previews`/`--no-previews` override the saved preview options. Appearance/preferences options remain independent. An explicit `--codec DLL` can supply a local codec when launching a workspace; workspace data cannot select a codec. A desktop build without Git rejects a workspace containing Git documents before replacing existing tabs. `--save-workspace FILE.json` requires smoke mode; the interactive host uses the File menu.

The smoke harness also accepts `workspace-save:PATH` and `workspace-open:PATH` through `--document-action`; these call the same helpers as the menus. Reports add `workspaceFile` with the current path, whether the run imported/saved a file and a data-only context snapshot. This context is distinct from the existing `workspace` report of decoded documents and status. Capture/report/preferences outputs cannot overwrite the workspace input. Workspace saves retain the existing source/companion/partition/mapping/Git protection and may explicitly replace the current valid workspace file.


## Presentation state — 0.38–0.40

Version two adds a `viewState` object to each saved document. Version-one files remain readable and acquire an empty presentation state; their next explicit save upgrades to version two. Older applications that understand only version one cannot read the new format. The outer file keeps the eight-document/1-MiB limits. Presentation strings/identity keys are limited to 4,096 UTF-8 bytes, each identity map to 128 entries and each state object to 256 KiB. Unknown fields, invalid types, control characters, non-finite/out-of-range transforms and invalid camera vectors are rejected before applying state.

The desktop restores object filters, before/after display choices, raw/typed inspection, the selected export by object path, and the current inspector's search/change filter and selected property path on each side. Blueprint graphs use their stable graph identity; material documents and texture sources use object paths. Their pan/zoom and the mesh's orbit camera are restored after model preparation and preview binding. The texture difference switch only applies when a comparable difference preview exists. Unfitted views retain their normal fit behavior. Captures keep at most 128 graph/image transforms, prioritizing the selected item. Asset property inspectors are saved for the current object on each side, rather than for every export ever visited.

Container state includes member/object filters, package/change filters, highlighted member, cooked object identity and the inspector filters/selected property for byte details, object metadata, mapped values, comparison and schema provenance. Restoring a highlighted member does not inspect or decode it: the saved source request still determines which member's bytes are loaded. Missing or hidden property/container selections clear; missing asset/graph choices use the current data's default. No stale numeric export/row index can select an unrelated item. Existing semantic coverage and unknown-peer rules remain unchanged.

Restoration runs on the owner thread outside drawing. The desktop retains pending state for documents that have not yet been displayed, including failed reads, so saving again does not erase their state. Selecting a ready tab prepares/binds its view and consumes that pending state once. Selecting a loaded tab still does not decode again. The explicit workspace save/open flow remains separate from preferences and session-local closed-tab history.

The embedding APIs expose `view_state()` and `restore_view_state(Json)` on the individual views and `AssetView`; bind data and GPU previews first, then restore. The header-only contract in `cyuediff/view_state.hpp` has no ImGui/CyUI dependency. It never contains resource handles or invokes readers/codecs. Hosts still own decoding, scheduling and resources. `--document-action view-state:JSON` exercises these same APIs in hidden validation; reports expose each document's current or pending `viewState`.

This is bounded presentation restoration, not a complete ImGui layout dump. Table column widths, expanded diagnostic trees, general panel scroll, selected nodes, active drag/edit gestures and content-browser/history dialogs are not persisted. Diff split/search/vertical scroll are recorded, but numeric hunk/row selections are not. Camera/canvas coordinates retain the saved pose rather than automatically refitting for another window size; the existing Fit controls remain available. No automatic last-session restoration is enabled.
