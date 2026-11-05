# CyUEDiff

**English** · [Français](README.fr.md)

A reusable C++17 engine for inspecting Unreal Engine packages and comparing serialized asset data **without launching Unreal Editor**. A separate [CyUI](https://github.com/MrMybal/CyUI) library and an optional Windows desktop app provide inspection, graph, image, mesh and diff views.

Version **0.167** supports tagged properties, Blueprint editor graphs with structured pin texts, Texture2D source previews, static-mesh source geometry, material graphs and stored instance parameters. It also includes visual change highlighting, a searchable typed inspector with selected previews, source-float HDR comparisons, Pak/IoStore container byte reading and diffs, cooked Zen object tables, explicit USMAP property reads with module identities, supported extensions, map/set deltas, explicitly bound native math/color/GUID/temporal/bounds/matrix/plane structs, supported cooked text histories and recursive text transforms and named/ordered format arguments and versioned argument-data formats, stored numeric/currency and date/time metadata and opaque text generator fingerprints, persistent delegates, modern field paths, explicit UE 5.5 byte strings and optionals and soft/interface/lazy/weak references, opaque body diffs, project/Git asset catalogues, desktop Pak/IoStore browsing and decoded-byte comparisons with independent Zen object/schema contexts and named property inspection, paginated commit history and local Git/LFS revision reading, independent project/engine contexts, eight independent document tabs with duplication, session-local reopening and explicit workspace files with inspector, viewport and panel layout state, persistent desktop appearance/display preferences, monitor/DPI window placement and optional previous-session restoration, cancellable loading, a verified package cache and an application-defined decoder registry. Unsupported data remains visible as opaque byte ranges with SHA-256 fingerprints.

Desktop visual review includes searchable Blueprint/material node selection with typed before/after details, changed-node navigation, material-instance override filters, changed mesh framing and changed-pixel region framing for LDR/HDR textures. Selections and navigation persist in explicit workspace files. See [visual review](docs/VISUAL_REVIEW.md).

Stored collision review covers reflected spheres, boxes, capsules, tapered capsules and convex source data, with stable named-shape diffs and body references. Use collision/collision-diff for properties and collision-preview for body-local wireframe JSON. The desktop Collisions tab adds synchronized before/after geometry, picking, changed-shape filtering and persistent orbit cameras. Cooked physics payloads remain opaque; see [collision contracts](docs/COLLISIONS.md).

Stored rich curves now expose native keys, tangents, weights and named differences for supported CurveFloat/CurveVector/CurveLinearColor assets and AnimDataModel float curves. Use `curves`, `curves-diff`, `curve-value --time N` and `curve-value-diff --time-start A --time-end B`, or the **Curves** tab. Evaluation supports weighted cubic segments and explicitly stored extrapolation/default settings. Sampled comparisons report changed values, tolerance and unavailable coordinates; the desktop retains custom ranges and tolerance. Missing settings stay unknown. Animation transform curves add nine named translation/Euler/scale lanes through `--curve-family transform`; `curve-components --time N --curve-id ID` reports their independent raw values. The desktop retains family and component choices. See [curve contracts](docs/CURVES.md) and the [Core-only consumer](examples/curve_consumer).

Animation source review now reads supported stored `AnimDataModel` bone tracks from legacy tagged properties and verified recent bulk-key layouts. Use `animation`, `animation-diff` and `animation-frame --frame N`, or the **Animations** tab, for named key differences, source-frame values and overlaid channel plots. Use `animation-frame-diff --before-frame A --after-frame B` for named local position/orientation/scale differences with independent tolerances. The desktop retains independent frames and tolerances; quaternion sign-only changes remain visible in stored-key diffs while their orientation delta is zero. Model/track/frame choices persist in workspace files. Omitted frame-rate terms and unsupported channels stay explicit. This covers stored local source keys; compressed playback, blending, root motion, hierarchy evaluation and skinning remain future work. See [animation contracts](docs/ANIMATIONS.md) and the [Core-only SDK example](examples/animation_consumer).

Raw Skeleton inspection adds stored bone hierarchies, local reference transforms, named structural diffs, affine reference-joint export and a searchable CyUI hierarchy inspector. Native SkeletalMesh geometry, skin weights and compressed animation playback remain separate work. See [skeleton contracts](docs/SKELETONS.md).

USMAP support is deliberately scoped: explicit build/class/offset bindings decode supported unversioned values; native payloads and unsupported types retain their opaque fingerprints. See [schema contracts and limits](docs/SCHEMAS.md).

This is an asset reader and comparison engine. Format coverage is explicit; it is not a universal Unreal serializer or an asset writer.

SkeletalMesh reference prefixes now expose stored bounds, ordered material slots, raw bones and local transforms. The same skeleton commands accept Skeleton and SkeletalMesh assets; skeleton-pose-diff compares derived affine joint displacements with a position tolerance. CyUI adds synchronized 3D reference-pose panes with joint picking and saved camera state. Skeletal source geometry, skinning and animation remain outside this prefix. See [reference skeleton contracts](docs/SKELETONS.md).

Stored Skeleton retarget sources now have bounded native reads, named bone differences and explicit affine pose selection. CLI/SDK and CyUI can choose independent source keys before/after; missing or unbound sources stay unavailable. Workspace files retain the selections. These are stored poses, without running Unreal retargeting or animation.

Stored Skeleton animation metadata now includes supported native curve containers, material/morph-target flags, bone-name links, stored LOD limits and synchronization marker names. Use `skeleton-animation` and `skeleton-animation-diff` for scoped named reports, or the Skeleton tab’s Animation metadata mode for searchable typed review. Marker order is normalized while duplicate occurrences are retained. Unread or stripped metadata reports incomplete coverage; this does not evaluate animation curves or animation samples.

Stored skeletal attachments now have named socket and virtual-definition reports, explicit owner membership and affine socket-origin comparison. Use `attachments`/`attachments-diff` and `socket-pose`/`socket-pose-diff`, or the Skeleton tab’s Attachments mode with typed details and synchronized 3D origins. Missing local locations and unresolved virtual parents stay unavailable; external objects, class defaults and runtime poses are not reconstructed.

## Modules

| Target | Purpose |
| --- | --- |
| `CyUEDiff::Core` | Package/container reading, project/engine context, asset catalogues, snapshots, decoders and diffs. No Unreal, ImGui, CyUI or Git dependency. |
| `CyUEDiff::Git` | Optional local Git/LFS adapter: revisions, companion files, verified LFS content and temporary-file ownership. |
| `CyUEDiff::UI` | Embeddable CyUI views for objects, properties, Blueprint/material graphs, images, wireframe meshes and side-by-side diffs. |
| `cyuediff` | JSON command-line interface for inspection, specialized diffs, project resolution and Git revisions. |
| `cyuediff_desktop` | Optional Windows application using Win32, Direct3D 11 and CyUI, with background loading, a content browser, local Git/LFS revision comparisons, a commit history browser and a Pak/IoStore browser. |

The Core vendors nlohmann/json 3.12.0 and stb under their MIT licenses. Builds do not download dependencies. UI builds use a host-provided CyUI checkout. See [third-party notices](THIRD_PARTY_NOTICES.md).

## Language policy

English is the default language for the engine's API, JSON contracts, CLI diagnostics and desktop interface. French is the second built-in UI language. Interface labels use translation keys with English fallback; asset names, file paths, serialized properties and detailed reader diagnostics retain their original values.

The desktop application defaults to English and restores a previously saved language preference. Use **View → Language → Français**, or launch with `--locale fr`. Use `--locale en` to select English explicitly. Embedded hosts select a locale through CyUI; each ImGui context owns its language state. The reader itself has no UI or localization dependency.

The desktop saves theme, language, preview and engine-content options under `%LOCALAPPDATA%/CyUEDiff/preferences.json`. `--preferences FILE.json` selects another file; `--no-preferences` disables persistence. Explicit command-line options override saved values. Paths and previous documents are not stored. See [desktop preferences](docs/DESKTOP.md#persistent-preferences--022).

## Build

Requirements: CMake 3.20+, a C++17 compiler, and Python 3 for the helper scripts. Windows x64 with MSVC 2022 is the verified platform. Linux/macOS build and runtime validation is pending.

```powershell
# Core, CLI and optional Git adapter; run tests in Release:
python scripts/build.py

# Core/CLI without Git:
python scripts/build.py --no-git --build-dir build-core

# Embeddable CyUI views:
python scripts/build.py --cyui D:/path/to/CyUI

# Windows desktop application:
python scripts/build.py --desktop --cyui D:/path/to/CyUI
build-native/Release/cyuediff_desktop.exe
```

The script locates Visual Studio's bundled CMake when it is not in PATH. Standalone builds enable the Git adapter by default; its commands require Git 2.45+ at runtime. Reading loose files through the Core requires neither Git nor Git LFS.

Equivalent CMake commands:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

For UI builds, set `CYUEDIFF_BUILD_UI=ON` and `CYUEDIFF_CYUI_SOURCE_DIR` to your CyUI checkout. `CYUEDIFF_BUILD_DESKTOP=ON` also enables the UI library. Desktop support currently requires Windows.

An optional Oodle adapter can be built with `--oodle-sdk <host-owned SDK directory>` and loaded explicitly with `--codec <module>`. Neither the SDK nor the codec binary is included in this repository or installed with the Core SDK. No Unreal source code is compiled into the Core.

## Inspect and compare

```powershell
build-native/Release/cyuediff.exe catalog MyGame.uproject
build-native/Release/cyuediff.exe git-catalog D:/Projects/MyGame HEAD
build-native/Release/cyuediff.exe inspect Content/BP_Player.uasset
build-native/Release/cyuediff.exe inspect Content/BP_Player.uasset --metadata
build-native/Release/cyuediff.exe diff before/BP_Player.uasset after/BP_Player.uasset
build-native/Release/cyuediff.exe material Content/M_Water.uasset
build-native/Release/cyuediff.exe material-diff before/MI_Water.uasset after/MI_Water.uasset
build-native/Release/cyuediff.exe preview Content/T_Water.uasset --out water.png
build-native/Release/cyuediff.exe image-diff before/T_Water.uasset after/T_Water.uasset --heatmap mask.png
build-native/Release/cyuediff.exe project MyGame.uproject --engine-root D:/Unreal/UE_5.5
build-native/Release/cyuediff.exe inspect /Game/BP_Player --project MyGame.uproject --engine-root D:/Unreal/UE_5.5
```

`--metadata` reads tables without properties or fingerprints. Normal reads compute SHA-256 in 64 KiB blocks. Pixel and geometry decoding are explicit operations; package diffs do not automatically decode these large payloads. `--out report.json` writes a report and rejects paths overlapping inputs or their companion files.

A project context reads `EngineAssociation`, accepts an explicit engine root, and resolves available `/Game`, `/Engine` and plugin content mounts. Selecting an engine supplies context and paths; it never launches that engine or overrides the serialization versions recorded in the file. Plugin activation rules are only partially modeled.

To compare Git revisions, including locally available LFS objects:

```powershell
build-native/Release/cyuediff.exe git-diff D:/Projects/MyGame HEAD~1 HEAD Content/M_Water.uasset
build-native/Release/cyuediff.exe git-diff D:/Projects/MyGame HEAD~1 HEAD /Game/M_Water --project MyGame.uproject --no-engine
```

The desktop’s **Browse history...** dialog supplies before/after commit IDs and opens the chosen asset or revision catalogue. The CLI exposes the same metadata through `git-history REPO REV`; see [history usage and limits](docs/GIT.md#commit-history--023).

The adapter materializes each revision's own companions, verifies local LFS content and keeps temporary files alive through revision handles. It does not fetch missing objects, modify the checkout or configure the repository. Historical project/plugin descriptors and engine contexts can be selected per side through the API and CLI.

## Embed the engine

```cmake
add_subdirectory(path/to/CyUEDiff)
target_link_libraries(my_app PRIVATE CyUEDiff::Core)
```

```cpp
#include <cyuediff/core.hpp>
#include <iostream>

auto before = cyuediff::Package::open("before/BP_Player.uasset");
auto after = cyuediff::Package::open("after/BP_Player.uasset");
auto diff = cyuediff::compare(before, after);
std::cout << diff.to_json().dump(2);
```

Installed SDKs also support `find_package(CyUEDiff CONFIG REQUIRED)`. See [the schema consumer](examples/schema_consumer/), [the cooked consumer](examples/zen_consumer/), [the container consumer](examples/container_consumer/), [the catalogue consumer](examples/catalog_consumer/), [the Core consumer](examples/consumer/), [the Git consumer](examples/git_consumer/) and [the custom-decoder consumer](examples/decoder_consumer/).

Application-specific native structs and export tails can be decoded with `SerializationProfile`, `AssetDecoder` and `DecoderRegistry` from `<cyuediff/decoders.hpp>`. Callbacks receive bounded input, match explicit type/version requirements, and produce schema-identified JSON. Ambiguity or failure preserves the original opaque range and fingerprint. The same `ReadOptions` can be used by Git revision reads.

## Current coverage

| Data | Implemented scope |
| --- | --- |
| Containers | Pak 10/11 full directory indexes, IoStore TOC 3–5, UCAS partitions, decoded member reads and byte diffs; explicit codecs. |
| Cooked Zen | Explicit UE 5.3/5.5 layouts, names/imports/exports, script class resolution, bulk/dependency metadata, object catalogues, explicitly mapped unversioned property regions and opaque body diffs. Native payloads remain opaque. |
| Packages | Classic loose `.uasset` / `.umap`, name/import/export tables, custom versions and object references; monolithic or split `.uexp` exports. |
| Properties | Primitive values, names, strings, references, soft paths, supported localized/recursive/formatted text histories, GUIDs, vectors/colors, transforms/ticks, tagged structs, decodable arrays and stored map/set deltas; host-defined native decoders. |
| Blueprints | Serialized editor objects, positions, native pins, connections and GUID identities; bytecode remains opaque. |
| Textures | Texture2D source metadata, payload indexing, JPEG/PNG/raw LDR previews, raw FP16/FP32/RGBE HDR samples, mip selection and pixel/sample diffs; optional explicit Oodle codec. |
| Static meshes | Modern MeshDescription source geometry, vertices/instances, UVs, normals/tangents, colors, triangles/material groups, geometry diffs and OBJ export. |
| Materials | Serialized expression graphs, inputs, masks, constants, references, named reroutes and stored instance overrides identified by name/association/index. |
| Data assets | Tagged properties and registered application-specific decoders; native types without schemas remain partially opaque. |

Implemented file-version branches cover UE4 **518–522** and UE5 **1000–1018**. This does not certify every asset in those versions. Real-file validation has used UE **5.3.2** and **5.5.4**, with generated fixtures for format transitions.

Present companions `.uexp`, `.ubulk`, `.uptnl`, `.m.ubulk`, `.o.ubulk` and `.upayload` are fingerprinted. Unknown properties and native regions retain their byte counts and SHA-256 hashes. These hashes can change because internal indices or serialization change even when functional content is equivalent.

Remaining unversioned value types, further USMAP extensions/layouts, licensee serialization, package-level compression, virtualized resources, cooked native payload decoding and unsupported native layouts require additional readers or backends. Parent materials, external material functions and shaders are not evaluated. Skeletal meshes, render sections and Nanite rendering are not implemented. Source files and external dependencies are not loaded automatically.

## Understand a diff

A controlled edit on a copy of a real Blueprint produced this change:

```json
{
  "op": "replace",
  "path": "/objects/TEST.EventGraph.K2Node_CallFunction_0/properties/NodePosX/value",
  "before": 256,
  "after": 320
}
```

| Status | Meaning |
| --- | --- |
| `identical` | Matching package/present-companion fingerprints, with no observed changes. |
| `changed` | A property, object, dependency, specialized projection or opaque range changed. |
| `binary-only` | Different bytes without a change in the inspected representation. |
| `incomplete` | A required export is missing or truncated; equality cannot be established. |
| `unverified` | Fingerprints were disabled, so binary equality is unavailable. |

`completeInput` covers required export ranges; it does not certify that every external or virtualized bulk payload is available. `properties` / `partial-properties` describe property-stream coverage, while native bytes may remain opaque. Header or structural-reference errors stop reading; export-level failures appear in diagnostics.

Pak/IoStore use separate `container`, `container-read`, `container-hash` and `container-diff` commands. Container diffs compare decoded bytes; stored IoStore BLAKE3 hashes and signatures are not authenticated. See [container contracts and examples](docs/CONTAINERS.md).

Cooked IoStore packages have separate `zen-inspect`, `zen-catalog` and `zen-diff` commands with explicit `--cooked-profile` selection. Names, classes and object references are readable. Exact schema/class/offset bindings optionally decode supported property regions; complete body fingerprints remain available. See [cooked Zen contracts](docs/ZEN.md) and [schema contracts](docs/SCHEMAS.md).

The general diff also exposes `graphChanges`, `textureChanges`, `meshChanges` and `materialChanges`. `image-diff` explicitly compares supported decoded pixels, and `mesh-diff` compares source geometry. CLI diff exit codes are `0` for identical, `1` for changed, and `2` for errors/incomplete/unverified results. Paths use JSON Pointer escaping.

## Desktop and UI

```powershell
build-native/Release/cyuediff_desktop.exe before/BP_Player.uasset after/BP_Player.uasset
build-native/Release/cyuediff_desktop.exe Content/BP_Player.uasset --locale fr --view blueprint
build-native/Release/cyuediff_desktop.exe --repo D:/Projects/MyGame --before-revision HEAD~1 --after-revision HEAD --before Content/BP_Player.uasset --view comparison
```

The Windows app provides file selection, background loading, optional independent project/engine contexts, a typed property table with before/after values and a raw JSON option, graph views, images, wireframe navigation and JSON comparison focused on the first change. Blueprint and material graphs mark changed nodes by stable identity, including nodes affected by changed connections. Material instance tables compare stored parameter values and overrides. Mesh views color added, removed or modified edges, including changes to vertex-instance attributes or material groups. Texture comparisons display a pixel difference mask and statistics when decoded dimensions match. HDR sources retain floating-point samples for comparison before display tone mapping; see [HDR textures](docs/HDR.md). See [visual comparisons](docs/VISUAL_DIFFS.md) for identities and limits. It prepares the first texture source and the first mesh source/LOD of each side. Select **Git revisions** to enter a local repository, two commit-ish references and repository-relative asset paths. An empty after path reuses the before path. Historical project descriptors enable `/Game/...` resolution; additions/removals, resolved commits and context-only changes are displayed explicitly. LFS content must already be available locally. See the English [desktop guide](docs/DESKTOP.md) for the controls, CLI options and limits.

Select **Browse content...** (`Ctrl+B`) after choosing a project, or from Git mode after entering a repository and references. Search the metadata catalogue, filter assets/maps and open a package or compare its revisions. Git presence indicates whether a path exists on each side; semantic changes are computed when opened. Scanning runs in the background and does not decode packages or require LFS content. See [asset catalogues](docs/CATALOG.md) for the reusable Core/Git API, limits and embeddable `CatalogView`.

Package loading, hashing, bulk decoding, image conversion, mesh decoding and direct Git operations support cooperative cancellation with phase progress. The desktop keeps an in-memory LRU package cache; every lookup checks full package/companion hashes, including same-size/same-timestamp edits. See [runtime controls](docs/RUNTIME.md).

The desktop keeps up to eight loose/Git/container documents open with independent views, a shared decoding queue and session-local closed-tab history. Explicit workspace files restore document contexts, supported filters/selections, graph/image/mesh navigation, panel/column proportions and context/diagnostic sections across restarts; see [workspace files](docs/WORKSPACES.md). Native window geometry, dialog layout and cooked native visual previews remain to be implemented. The [development status](docs/ROADMAP.md) records the remaining cooked-object, schema, advanced geometry and desktop work. CPack builds distributable executable/SDK archives with all selected dependency notices; the [distribution guide](docs/DISTRIBUTION.md) describes the local build and the prepared Windows/Linux CI workflow.

The embeddable `CyUEDiff::UI` remains separate. Hosts own their window, graphics backend, asynchronous loading and GPU resources. Five CyUI themes and English/French controls are supported.

## Validation and documentation

Validation covers standalone Core, desktop without Git and desktop with Git, plus independently compiled installed SDK consumers. Binary delta checks include generated edits, deliberate rolling-hash collisions, malformed manifests, bounded work, wrong base/target identities, atomic new-file output protection and exact Git/LFS byte provenance. The independent Python interpreter checks reconstruction on original Unreal assets and controlled byte-edited copies, with original source hashes preserved. Existing property, container, curve, pose, visual and workspace suites remain in the test matrix. Matching-build real mapped values remain unverified without a USMAP. See [validation scope and evidence](docs/VALIDATION.md) for the current counts, corpus limits and any retried checks. Windows/Linux CI is prepared; hosted execution requires a push. Proprietary inputs, codecs and generated artifacts stay outside Git.

Detailed implementation notes are currently available in French:

[Architecture](docs/ARCHITECTURE.md) · [Blueprints](docs/BLUEPRINTS.md) · [Textures](docs/TEXTURES.md) · [Meshes](docs/MESHES.md) · [Materials](docs/MATERIALS.md) · [Git/LFS](docs/GIT.md) · [Projects](docs/PROJECTS.md) · [Decoders](docs/DECODERS.md) · [UI integration](docs/UI.md) · [Codecs](docs/CODECS.md) · [Validation](docs/VALIDATION.md)

See the English [GitHub publishing guide](docs/GITHUB.md) for the remote, repository description and reproducible Gitleaks checks.

## Licensing

CyUEDiff's original code is distributed under the [MIT License](LICENSE). Third-party components retain their own licenses. No Unreal Engine source, binaries, proprietary assets or Oodle SDK files are distributed here.

Extended tagged-property coverage and its remaining schema requirements are documented in [property decoding](docs/PROPERTIES.md).

Source animation hierarchy poses can now be computed against an explicitly supplied Skeleton, compared at independent integer frames and reviewed in a reusable CyUI 3D component. See [animation pose contracts](docs/ANIMATIONS.md#explicit-hierarchy-pose-api-and-commands) and the standalone [Core SDK example](examples/animation_pose_consumer). The native Animations tab accepts explicit loose or historical Git Skeleton bindings, retains per-revision blob/LFS provenance and supports independent source frames; see [desktop controls](docs/DESKTOP.md). Other applications can use the [installed Git pose example](examples/git_animation_pose_consumer).


Stored socket locations can now be placed in explicit source animation frames and compared independently of the raw reference pose. `animation-socket-pose` and `animation-socket-pose-diff` expose the Core results; the desktop offers a retained socket-origin view for loose and historical Git inputs. Missing locations or parent transforms remain unavailable. See [source socket contracts](docs/ANIMATIONS.md#source-socket-api-and-commands).

## Window and session restoration — 0.157

The desktop remembers normal window placement and maximized state, fits restored bounds to available monitors and adapts the size to their DPI. **View → Reset window placement** recovers a fitted default window.

**View → Restore previous session on startup** is off by default. When enabled, a clean close saves open document contexts and supported review state in a separate bounded session file. The next launch reloads those sources. Explicit source arguments or `--workspace` take priority; `--no-restore-session` skips restoration. Missing or unsupported inputs retain diagnostics. Invalid session files are preserved. See [desktop behavior](docs/DESKTOP.md) for custom paths and isolated validation commands.

## Binary block deltas — 0.167

The reusable Core computes binary deltas for complete asset or companion files, including opaque data, then reconstructs an exact target after checking the base and target SHA-256. Rolling matching retains reusable blocks after inserted/deleted bytes. Use `binary-diff`, `binary-apply` and the optional `git-binary-diff` adapter for local Git/LFS revisions. Compact raw-byte manifests avoid hexadecimal expansion. `binary-compose` combines A→B→C without intermediate files, `binary-info` inspects metadata, and `binary-apply` streams file-backed reconstruction before publishing a verified new target. The SDK exposes these operations with explicit budgets, cancellation and progress. See [contracts, limits and examples](docs/BINARY_DELTA.md).
