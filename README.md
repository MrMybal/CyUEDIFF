# CyUEDiff

**English** · [Français](README.fr.md)

A reusable C++17 engine for inspecting Unreal Engine packages and comparing serialized asset data **without launching Unreal Editor**. A separate [CyUI](https://github.com/MrMybal/CyUI) library and an optional Windows desktop app provide inspection, graph, image, mesh and diff views.

Version **0.15** supports tagged properties, Blueprint editor graphs, Texture2D source previews, static-mesh source geometry, material graphs and stored instance parameters. It also includes visual change highlighting, a typed inspector, source-float HDR comparisons, Pak/IoStore container byte reading and diffs, project/Git asset catalogues, local Git/LFS revision reading, independent project/engine contexts, cancellable loading, a verified package cache and an application-defined decoder registry. Unsupported data remains visible as opaque byte ranges with SHA-256 fingerprints.

This is an asset reader and comparison engine. Format coverage is explicit; it is not a universal Unreal serializer or an asset writer.

## Modules

| Target | Purpose |
| --- | --- |
| `CyUEDiff::Core` | Package/container reading, project/engine context, asset catalogues, snapshots, decoders and diffs. No Unreal, ImGui, CyUI or Git dependency. |
| `CyUEDiff::Git` | Optional local Git/LFS adapter: revisions, companion files, verified LFS content and temporary-file ownership. |
| `CyUEDiff::UI` | Embeddable CyUI views for objects, properties, Blueprint/material graphs, images, wireframe meshes and side-by-side diffs. |
| `cyuediff` | JSON command-line interface for inspection, specialized diffs, project resolution and Git revisions. |
| `cyuediff_desktop` | Optional Windows application using Win32, Direct3D 11 and CyUI, with background loading, a content browser and local Git/LFS revision comparisons. |

The Core vendors nlohmann/json 3.12.0 and stb under their MIT licenses. Builds do not download dependencies. UI builds use a host-provided CyUI checkout. See [third-party notices](THIRD_PARTY_NOTICES.md).

## Language policy

English is the default language for the engine's API, JSON contracts, CLI diagnostics and desktop interface. French is the second built-in UI language. Interface labels use translation keys with English fallback; asset names, file paths, serialized properties and detailed reader diagnostics retain their original values.

The desktop application starts in English. Use **View → Language → Français**, or launch with `--locale fr`. Use `--locale en` to select English explicitly. Embedded hosts select a locale through CyUI; each ImGui context owns its language state. The reader itself has no UI or localization dependency.

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

Installed SDKs also support `find_package(CyUEDiff CONFIG REQUIRED)`. See [the container consumer](examples/container_consumer/), [the catalogue consumer](examples/catalog_consumer/), [the Core consumer](examples/consumer/), [the Git consumer](examples/git_consumer/) and [the custom-decoder consumer](examples/decoder_consumer/).

Application-specific native structs and export tails can be decoded with `SerializationProfile`, `AssetDecoder` and `DecoderRegistry` from `<cyuediff/decoders.hpp>`. Callbacks receive bounded input, match explicit type/version requirements, and produce schema-identified JSON. Ambiguity or failure preserves the original opaque range and fingerprint. The same `ReadOptions` can be used by Git revision reads.

## Current coverage

| Data | Implemented scope |
| --- | --- |
| Containers | Pak 10/11 full directory indexes, IoStore TOC 3–5, UCAS partitions, decoded member reads and byte diffs; explicit codecs. |
| Packages | Classic loose `.uasset` / `.umap`, name/import/export tables, custom versions and object references; monolithic or split `.uexp` exports. |
| Properties | Primitive values, names, strings, references, soft paths, supported localized text histories, GUIDs, vectors/colors, transforms/ticks, tagged structs, decodable arrays and stored map/set deltas; host-defined native decoders. |
| Blueprints | Serialized editor objects, positions, native pins, connections and GUID identities; bytecode remains opaque. |
| Textures | Texture2D source metadata, payload indexing, JPEG/PNG/raw LDR previews, raw FP16/FP32/RGBE HDR samples, mip selection and pixel/sample diffs; optional explicit Oodle codec. |
| Static meshes | Modern MeshDescription source geometry, vertices/instances, UVs, normals/tangents, colors, triangles/material groups, geometry diffs and OBJ export. |
| Materials | Serialized expression graphs, inputs, masks, constants, references, named reroutes and stored instance overrides identified by name/association/index. |
| Data assets | Tagged properties and registered application-specific decoders; native types without schemas remain partially opaque. |

Implemented file-version branches cover UE4 **518–522** and UE5 **1000–1018**. This does not certify every asset in those versions. Real-file validation has used UE **5.3.2** and **5.5.4**, with generated fixtures for format transitions.

Present companions `.uexp`, `.ubulk`, `.uptnl`, `.m.ubulk`, `.o.ubulk` and `.upayload` are fingerprinted. Unknown properties and native regions retain their byte counts and SHA-256 hashes. These hashes can change because internal indices or serialization change even when functional content is equivalent.

Unversioned properties, licensee serialization, package-level compression, virtualized resources, cooked UObject/Zen data and unsupported native layouts require additional schemas or backends. Parent materials, external material functions and shaders are not evaluated. Skeletal meshes, render sections and Nanite rendering are not implemented. Source files and external dependencies are not loaded automatically.

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

The app does not yet include a commit history browser or multi-document workspace. The [development status](docs/ROADMAP.md) records the remaining cooked-object, schema, advanced geometry and desktop work. CPack builds distributable executable/SDK archives with all selected dependency notices; the [distribution guide](docs/DISTRIBUTION.md) describes the local build and the prepared Windows/Linux CI workflow.

The embeddable `CyUEDiff::UI` remains separate. Hosts own their window, graphics backend, asynchronous loading and GPU resources. Five CyUI themes and English/French controls are supported.

## Validation and documentation

The verified Windows Release build passes **21 CTest suites**, with **1,011 checks**, 30 renderer-free UI frames and a native DX11/WARP smoke test. A separate run verified **21 native captures**, seven rejected invocations/output overlaps, matching desktop/CLI diff results and unchanged input assets. A separate Git/LFS run verified **22 native captures**, six output/option refusals, desktop/CLI Git diff parity and an unchanged checkout. Content-browser validation adds **21 native captures**, five output refusals, CLI catalogue parity, local/Git selection and unchanged source/checkout fingerprints. The desktop configuration also passes with Git disabled (18 suites), and an external consumer compiles and runs against the installed Core-only catalogue SDK. The Core-only configuration passes fourteen suites without Git, UI, Desktop or Oodle. The CyTrace UE 5.3 corpus adds 150 original packages, 12 rejected truncated headers and source/companion hash checks; partial/opaque exports remain explicitly reported. A CPack desktop archive was extracted and tested, including its CLI, DX11/WARP host and a consumer linked against the packaged Core SDK. The supplied cooked build adds 6,219 reconstructed container members (231,432,265 decoded bytes), matching an independent parser with unchanged original input hashes. The hosted CI workflow is prepared but has not run before a push. Proprietary assets, codec binaries, generated captures and build outputs are excluded from the repository.

Detailed implementation notes are currently available in French:

[Architecture](docs/ARCHITECTURE.md) · [Blueprints](docs/BLUEPRINTS.md) · [Textures](docs/TEXTURES.md) · [Meshes](docs/MESHES.md) · [Materials](docs/MATERIALS.md) · [Git/LFS](docs/GIT.md) · [Projects](docs/PROJECTS.md) · [Decoders](docs/DECODERS.md) · [UI integration](docs/UI.md) · [Codecs](docs/CODECS.md) · [Validation](docs/VALIDATION.md)

See the English [GitHub publishing guide](docs/GITHUB.md) for the remote, repository description and reproducible Gitleaks checks.

## Licensing

CyUEDiff's original code is distributed under the [MIT License](LICENSE). Third-party components retain their own licenses. No Unreal Engine source, binaries, proprietary assets or Oodle SDK files are distributed here.

Extended tagged-property coverage and its remaining schema requirements are documented in [property decoding](docs/PROPERTIES.md).
