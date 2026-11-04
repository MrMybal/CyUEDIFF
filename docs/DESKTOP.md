# Desktop application — 0.10

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

The Git controls are present when `CYUEDIFF_BUILD_GIT=ON`. A desktop build with Git disabled remains functional for loose files and rejects Git requests explicitly. References and paths are entered as text; there is no commit history or asset browser yet.

## Views and document lifetime

Properties inspects either present side, with object filtering, native JSON and opaque ranges. Blueprint and Materials show serialized graphs; Textures shows available source previews; Meshes provides a navigable source wireframe. Comparison shows snapshots side by side, focused on the first change, with CyUI search, next-change and unified display controls.

One worker loads packages, comparisons and CPU previews. One pending request is retained; newer requests supersede pending work and stale results. Both sides publish atomically. A failed load keeps the previous document visible. The worker never calls ImGui or Direct3D.

The main thread prepares the views and uploads images before the next frame. Replacing or closing a document clears image bindings before releasing GPU resources. Views perform no file reads or decoding during `draw`. View preparation can still cost a frame for large documents. Active decoding is not cancellable; shutdown waits for the active worker operation.

The host prepares the first texture source and first mesh source/LOD 0 per side. Texture budgets are 16 million pixels and 128 MiB for payload/decoder data; mesh payloads are limited to 128 MiB with the Core's element limits. **Load previews** can be disabled. An optional codec must be supplied explicitly with `--codec DLL`; it is trusted host code and is never discovered from an asset or engine directory. No Oodle SDK or binary is distributed. See [CODECS.md](CODECS.md).

Content browsing, commit history, multiple documents, configurable decoder profiles, persistent preferences and independent desktop contexts per side remain future work. Material evaluation, skinned mesh rendering and Nanite rendering retain the Core's documented limitations.

## Native validation

```powershell
build-native/Release/cyuediff_desktop.exe --smoke --before Content/BP_Player.uasset --view blueprint --locale fr --capture build-native/preview.png --report build-native/preview.json
python scripts/validate_desktop_git.py --asset D:/Owned/BP_Player.uasset --after D:/Owned/BP_Player_changed.uasset --texture D:/Owned/T_Water.uasset --mesh D:/Owned/SM_Rock.uasset
```

`--smoke` renders at least twelve settled frames in a hidden DX11/WARP window. `--view` accepts `properties`, `blueprint`, `materials`, `textures`, `meshes` and `comparison`. Capture/report require smoke mode. Outputs overlapping input assets, companions, Git metadata, LFS storage or selected engine context are rejected. Exit code `0` means the document loaded, even when changed; `2` means loading/capture failed. The document report exposes `status`, the package `diff` and the optional complete `git` comparison, including project contexts and immutable commit IDs.

The Windows Release configuration passes thirteen CTest suites with 571 checks, thirty renderer-free UI frames and a native renderer smoke test. `desktop_git` covers 27 integration checks; the UI frames also exercise additions/removals. The desktop configuration without Git passes ten suites.

`scripts/validate_desktop_git.py` produced 22 native captures, six option/output refusals, desktop/CLI Git diff parity and unchanged index/checkout/source fingerprints. It uses a fresh owned repository with generated local LFS pointers and copies of explicitly selected UE 5.5.4/5.3.2 assets. Captures were reviewed in all five themes, both languages and the relevant views. The existing loose-file validator also covers 21 captures and seven refusals. Generated assets, repositories, reports and images are ignored and never committed.
