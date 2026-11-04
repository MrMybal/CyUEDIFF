# Workspace files

The optional desktop host has a data-only `cyuediff.workspace/1` JSON format. It records one to eight documents in order, the active document index, source and project/engine/schema contexts, preview options and initial asset/container view. It does not serialize asset bytes, loaded views, GPU resources, caches, callbacks, closed-document history, appearance preferences or codec modules. A workspace file does not certify that its source files or schemas still match a build.

Local filesystem paths are saved as absolute paths. Relative paths in a file being opened resolve against that file's parent directory. Git asset/project paths remain repository-relative, and mounted names such as `/Game/Example` remain mounted when a project context is present. Git revision expressions are retained as entered, so a later opening can resolve a moving branch differently. No checkout, network fetch or source modification is performed.

The reader limits a file to 1 MiB, eight documents, 32,768 UTF-8 bytes per string and JSON nesting depth 12. Unknown versions/fields, duplicate fields, malformed UTF-8, control characters, invalid types/ranges and ambiguous drive-relative paths are rejected. It reads context data without opening packages, mappings or codecs. Missing files produce an error instead of creating a workspace.

Saving validates the complete document first, reserves a unique file in the destination directory, flushes it and atomically replaces the destination. Existing files must already be valid workspace files; unrelated, malformed and future-version files are preserved. The native host additionally checks output paths against source packages, companions, container partitions, mappings, Git/LFS storage and engine metadata.


## Open and save

Use **File → Open workspace...** (`Ctrl+Alt+O`) to replace the open tabs with a saved workspace. The complete format, feature support and output guards are checked before the old documents are cleared. All restored ready contexts are queued for a fresh read; a later read error belongs to its own tab. Opening a workspace clears the session's closed-document history and cancels old work. Save the current workspace first if you want to return to it.

**Save workspace** (`Ctrl+S`) writes the current workspace path, or opens a native file chooser for a new workspace. **Save workspace as...** (`Ctrl+Shift+S`) always chooses another path. Saving is explicit; the desktop does not automatically restore or save source paths through the user preference profile. Current asset/container tabs and the cooked property/object mode are recorded. Filters, canvas positions and selected rows are retained only while a document stays open.

```powershell
cyuediff_desktop.exe --workspace D:/Work/Review.json
cyuediff_desktop.exe --smoke --save-workspace Review.json --before Before.uasset --after After.uasset --new-document --container Build.pak
cyuediff_desktop.exe --smoke --workspace Review.json --report restored.json
```

`--workspace` cannot be mixed with source options or `--new-document`. `--document-index` can override the saved active tab; `--previews`/`--no-previews` override the saved preview options. Appearance/preferences options remain independent. An explicit `--codec DLL` can supply a local codec when launching a workspace; workspace data cannot select a codec. A desktop build without Git rejects a workspace containing Git documents before replacing existing tabs. `--save-workspace FILE.json` requires smoke mode; the interactive host uses the File menu.

The smoke harness also accepts `workspace-save:PATH` and `workspace-open:PATH` through `--document-action`; these call the same helpers as the menus. Reports add `workspaceFile` with the current path, whether the run imported/saved a file and a data-only context snapshot. This context is distinct from the existing `workspace` report of decoded documents and status. Capture/report/preferences outputs cannot overwrite the workspace input. Workspace saves retain the existing source/companion/partition/mapping/Git protection and may explicitly replace the current valid workspace file.
