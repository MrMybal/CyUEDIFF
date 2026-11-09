# CyUEDiff version notes

[README](README.md) · [Français](VERSION.fr.md) · [Issue tracker](https://github.com/MrMybal/CyUEDIFF/issues)

## 0.185.0 — Tagged source hybrids

The Core now combines an explicitly certified tagged-property prefix with a native struct tail through existing layout catalogues. Nested/repeated values, shared budgets, whole opaque fallback and a borrowed prefix context for conditional host decoders are available in CLI and EN/FR desktop review. Actual Engine-reloaded original plugin assets validate exact values and named differences through a fresh installed SDK. Legacy map/set tags with missing struct identities and class defaults remain separate work. Use matching 0.185 headers/libraries: DecoderContext and AssetDecoder were extended. See [HYBRID_STRUCTS.md](docs/HYBRID_STRUCTS.md).

## 0.184.0 — Local physical-cloth source development

The independent Core/CLI now project selected UE 5.3 physical cloth geometry, bone/weight/tether data and modern native transitions. Focused queries, named stored differences, reference-pose Chaos wrap algebra, OBJ output and explicitly bound render-source index checks are available through a fresh installed SDK example. Map deltas and missing constructor defaults retain incomplete coverage; the EN/FR desktop now binds independently selected physical LODs, focused vertex/tether differences, stored configuration and reference-pose wire previews. Workspace version 13 retains the independent profiles, objects, LODs, selection and camera; older formats migrate with the new decoder disabled. See [PHYSICAL_CLOTH.md](docs/PHYSICAL_CLOTH.md).

The last verified local Windows archive remains 0.182.0. This source milestone does not upload, publish or replace that archive.

## 0.183.0 — Local source development

This source/API milestone adds explicit hybrid struct bindings: a certified reflected property prefix followed by a bounded declarative native tail. Version-four schema contexts preserve layout provenance and independent before/after identities. The selected UE 5.3 Windows cloth profile exposes mapped physical properties, native tether values and raw native transition records. It does not infer missing class defaults, execute simulation or discover arbitrary plugin serializers.

The public C++ option/context structures gain `struct_tails`; rebuild hosts against the matching headers and library. The minor-version increase keeps this SDK contract distinct from 0.182 under CMake's same-minor compatibility rule. The latest verified local Windows archive remains 0.182.0; this source change does not replace, upload or publish that archive. See [the contract and evidence](docs/HYBRID_STRUCTS.md).

Paged source/cooked property inspection adds direct-child pages, nested/tag scopes, explicit index navigation and persistent EN/FR selection. Verified with original large fixtures, actual cloth/configuration captures and an independently installed Core consumer; the prior 0.182 archive is unchanged.

## 0.182.0 — Local development preview

This source version includes the post-0.181 native readers, comparisons and desktop inspection work. The Windows x64 CLI/desktop and reusable Core/Git SDK are verified as a local archive. This task does not upload binaries, publish a GitHub Release or claim a new hosted CI run. The public website is managed separately.

### Main additions

- Explicit UE 5.3 Windows cooked texture mips, static/skeletal render LODs, native Chaos collision geometry and compressed animation/curve values, with independent source contexts and verified bulk windows.
- Selected native Nanite geometry/shading inspection, scoped surface correspondence, hierarchy/cluster queries and EN/FR desktop review.
- Named cooked alternate weights and compressed render morphs, stored cloth deformer mappings, retained section overlap tables and float32/float16/UNorm8 custom vertex channels, with focused typed Core APIs and persistent desktop inspectors.
- Matching-source UHT metadata preparation, reflected declaration prefixes, required IoStore source integrity and bounded support for additional selected container layouts.
- A bundled bilingual offline HTML reader, current native/context guidance and original cooked attribute captures, with local styles/scripts/images.
- A versioned CMake package, machine-readable component/workspace metadata and a Core SDK that excludes source-only interface headers. The archive verifier builds its examples from the extracted package and audits all installed header dependencies.

### Availability and limits

The current Windows development build is checked locally with MSVC 2022 and Direct3D 11/WARP. The default archive requires the matching Microsoft Visual C++ x64 runtime and a separate Git installation for Git features. UI libraries are available from source with the host's CyUI checkout; the installed SDK exports Core and optionally Git. Linux/macOS desktop builds are unavailable. The earlier hosted Windows/Ubuntu evidence applies to 0.181, not to an unpushed 0.182 build.

Readers use explicit engine/build/platform profiles and matching mappings. Supported stored values and pixels/geometry differ from final shader, skinning, cloth simulation or animation graph evaluation. Arbitrary plugin serializers, physical cloth assets/configurations, additional native/producer profiles and the remaining coverage listed in [the completion record](docs/COMPLETION.md) are not declared finished. Private assets, Engine implementation, codecs owned by the host and generated reference data are excluded from the archive.

See [distribution checks](docs/DISTRIBUTION.md), [current validation](docs/VALIDATION.md), [desktop usage](docs/DESKTOP.md) and [format scope](docs/ROADMAP.md).

## 0.181.0 — Earlier source milestone


This was the preceding source project version; the notes below describe that historical milestone. No GitHub application release or public desktop download is available. The [website](https://mrmybal.github.io/CyUEDIFF/?lang=en) is published separately.

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
