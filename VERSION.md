# CyUEDiff version notes

[README](README.md) · [Français](VERSION.fr.md) · [Issue tracker](https://github.com/MrMybal/CyUEDIFF/issues)

## 0.181.0 — In development

This is the source project version declared in `CMakeLists.txt`. Core, CLI and the optional desktop use the same version. No GitHub application release or public desktop download is available. The [website](https://mrmybal.github.io/CyUEDIFF/?lang=en) is published separately.

### Changes in this source version

- Modern MeshDescription morph targets expose named source position deltas and optional split-normal deltas through immutable Core snapshots, geometry JSON and named mesh differences.
- The English/French Meshes tab adds searchable, persistent morph inspection and highlights base-mesh edges affected by changed source deltas. OBJ continues to export supported base geometry.
- Reflected collision/socket component readers own their field-name storage, correcting a lifetime issue found by GCC in the initial Ubuntu CI run.
- Positive angular comparison thresholds account for double-precision roundoff at inclusive boundaries. Zero-tolerance comparisons retain strict sensitivity; reported angular metrics are unchanged.
- The installed Core-only morph consumer demonstrates typed source access without Git or CyUI.

See [morph contracts](docs/MORPHS.md) and the [installed consumer example](examples/mesh_morph_consumer/).

### Earlier capabilities retained

- **0.180:** fixture-validated modern skeletal MeshDescription bones, stored poses and raw named weight profiles. See [source skinning](docs/SKINNING.md).
- **0.179:** explicit local virtualized editor payload stores with mandatory full raw BLAKE3 verification. See [virtualized sources](docs/VIRTUALIZED.md).
- **0.173–0.178:** bounded declarative native layouts and explicit ASCII, Latin-1, Windows-1252 and UTF-8 decoding. See [binary layouts](docs/BINARY_LAYOUTS.md).
- **0.158–0.172:** binary block differences, compact manifests, bounded composition and SHA-256-verified file-backed reconstruction. See [block deltas](docs/BINARY_DELTA.md).
- Earlier milestones include visual graph/image/geometry differences, typed properties, cooked schema contexts, Git/container browsing, multiple documents, persistent workspaces, window/session preferences and stored skeleton/animation/curve inspection. Their contracts remain in the [desktop guide](docs/DESKTOP.md) and [roadmap](docs/ROADMAP.md).

### Platforms and availability

| Component | Current availability |
| --- | --- |
| Windows x64 desktop | Built and checked locally with MSVC 2022 and Direct3D 11. Source build requires a separate CyUI checkout. No public desktop release yet. |
| Windows Core/CLI, Git on/off | Hosted CI passed on Windows 2022. |
| Linux Core/CLI, Git on/off | Hosted CI passed on Ubuntu 24.04. This does not establish arbitrary distribution/libc compatibility. |
| Linux/macOS desktop | No available build. |
| macOS Core/CLI | No validated build claimed. |

The [hosted 0.181 run](https://github.com/MrMybal/CyUEDIFF/actions/runs/42369077581) passes both operating systems with Git enabled/disabled, external installed-SDK consumers and the secret scan. Windows desktop validation remains local. CI CPack artifacts are distinct from a published application release; inspect their platform/runtime scope before use. [Distribution requirements](docs/DISTRIBUTION.md) describe archive contents and MSVC runtime choices.

### Documented limitations

- Format support is explicit. Selecting an engine root supplies context; it does not extract arbitrary C++ serializers from an installed engine or plugin, launch Unreal or make every format readable.
- Classic file-version branches cover UE4 518–522 and UE5 1000–1018. Real-file validation has included UE 5.3.2 and 5.5.4; this does not certify every asset or custom build in those ranges.
- Unversioned values require matching explicit USMAP bindings. The real cooked corpus lacks a corresponding `.usmap`; mapped value semantics remain fixture-validated. Unknown native regions retain opaque byte counts and fingerprints.
- Modern skeletal source geometry and morphs remain fixture-validated. An independent matching real source corpus is still needed. Cooked/legacy skin and morph buffers, runtime skinning/blend weights, compressed animation playback, runtime retargeting, native cooked physics and Nanite payload decoding remain unfinished.
- Blueprint bytecode is opaque. Material expressions and stored parameters are inspected without compiling shaders, resolving all external functions or rendering materials.
- Virtualized source resolution uses explicit local stores. Remote backends, additional container versions and encrypted containers require further work. Compressed payloads can require an explicitly supplied compatible codec.
- Git/LFS reads use local objects and verified local LFS content. Missing remote objects are not fetched. The binary-delta API does not provide a remote hosting/synchronization service.
- Package equality and semantic coverage are different: raw bytes may change without a difference in the inspected projection. Missing/truncated data produces an incomplete result. Source files, defaults and external dependencies are not loaded automatically.
- The engine reads and compares assets; writing/saving Unreal assets is not implemented.

See the [format roadmap](docs/ROADMAP.md), [schema contracts](docs/SCHEMAS.md) and [runtime bounds](docs/RUNTIME.md) for detailed scope.

### Known bugs and feedback

There is no published release-specific list of confirmed bug tickets. This does not imply the absence of bugs. Coverage limitations above are separate from filed defects. Report reproducible problems in the [issue tracker](https://github.com/MrMybal/CyUEDIFF/issues), with the source version, engine/build context, command or view and diagnostics. Share only samples you are authorized to redistribute.

[Application releases](https://github.com/MrMybal/CyUEDIFF/releases) · [Source history](https://github.com/MrMybal/CyUEDIFF/commits/main/)
