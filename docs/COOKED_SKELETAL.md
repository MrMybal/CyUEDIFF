# Cooked skeletal render geometry

CyUEDiff Core reads explicit **UE 5.3 Windows editor-stripped inline and streamed SkeletalMesh render LODs** without launching or linking Unreal. This reader is separate from modern source MeshDescription skinning, source skeleton/animation inspection and native Chaos physics.

## Contract and explicit producer facts

`read_zen_skeletal_mesh` requires `CookedSkeletalProfile::unreal_5_3_windows`, a matching Zen 5.3 profile/global script table and a successfully decoded explicit `/Script/Engine.SkeletalMesh` property binding. It selects one non-template export, or accepts an explicit export index. The native window begins at the exact mapped property end; no byte search or guessed boundary is used.

`keep_mobile_min_lod_setting` specifies whether the producer serialized the desktop MinMobileLOD field. The wire has no presence marker; it must be supplied even when false. This is the **skeletal** producer setting, independently of the static-mesh CVar.

A stored `bHasVertexColors` BoolProperty determines whether a color buffer exists. When that property is omitted, `has_vertex_colors` must supply the producer fact. Omission is not interpreted as false. An explicit setting conflicting with a stored property is rejected. The result records which source supplied the color setting.

## Decoded values and geometry

- Object GUID presence, imported double bounds, native material records/slot names/UV densities and exact render-LOD boundaries.
- Reference bone names, parents and double quaternion/translation/scale poses, with matching name-index map and hierarchy validation.
- Required/active bone arrays and section material/index/vertex ranges, section bone maps, maximum influences, tangent flags, visibility and disabled state.
- uint16/uint32 indices, float3 positions, signed packed 8/16-bit tangent bases, half/full UVs and conditional BGRA colors.
- Fixed and variable influence streams, 8/16-bit bone IDs and weights, and variable per-vertex offset/count lookup. Source buffer hashes and encoding metadata remain available.

Each render vertex has its own `MeshVertex`/`MeshInstance`; the output does not merge seams. Section-local influence indices resolve through the section bone map to reference bones. The shared `MeshSkin` retains nonzero raw weights, with uint8 weights losslessly expanded by 257 to uint16. No weight normalization, skin evaluation or bind-pose correction is applied. Repeated zero padding is omitted from the semantic map and retained in the raw buffer fingerprint. White is the identified display fallback for an unstored color channel.

The shared mesh comparison includes named bone/pose and weight changes alongside geometry. It compares the selected decoded LOD projection; opaque-tail differences and every other property are outside that comparison. Use Zen inspection/diffs as well when reviewing the complete package coverage.

## CLI and SDK

```text
cyuediff zen-skeletal BUILD.utoc PACKAGE_ID --cooked-profile ue5.3 --script-objects global.utoc --schema context.json --skeletal-profile ue5.3-windows --keep-mobile-min-lod 0 --has-vertex-colors 0
cyuediff zen-skeletal-diff BEFORE.utoc ID AFTER.utoc ID [same explicit context] --lod 0 --after-lod 0
```

`[same explicit context]` abbreviates the complete context flags, not literal syntax. `zen-skeletal` includes all loaded LOD geometry. `--export-index` selects an object. Diff accepts independent `--after-schema`, `--after-script-objects`, `--after-cooked-profile`, `--after-skeletal-profile`, `--after-keep-mobile-min-lod`, `--after-has-vertex-colors` and `--after-export-index`. Unspecified after settings inherit the explicit before settings; conflicting stored colors still fail. Add `--codec MODULE` only for a required host decompressor. Output destinations cannot overwrite containers, mappings or global tables. Exit codes are 0 for a successful inspection/identical projection, 1 for changed geometry, and 2 for a refusal/error.

```cpp
cyuediff::CookedSkeletalOptions mesh;
mesh.profile = cyuediff::CookedSkeletalProfile::unreal_5_3_windows;
mesh.keep_mobile_min_lod_setting = false; // Explicit skeletal producer fact.
mesh.has_vertex_colors = false;          // Needed for an omitted property.
auto data = cyuediff::read_zen_skeletal_mesh(container, entry, zen, read, mesh);
auto geometry = data.lods.at(0).geometry;
```

The installed `examples/cooked_skeletal_consumer` links only `CyUEDiff::Core`; its color argument is `0`, `1` or `stored`. `geometry = false` validates supported buffers without constructing geometry. Budgets cover package reads, aggregate LOD vertices/indices/triangles/sections/influences, bones, UVs and working/output allocations. Cancellation propagates through I/O and vertex/skin loops. JSON serialization is a separate caller-owned allocation.

## Evidence and remaining work

Original fixtures cover 128 combinations of fixed/variable skinning, bone/weight/index widths, tangent precision, UV precision and color presence. They exercise native truncations, bad hierarchy/maps/indices/weights, inconsistent byte counts, budgets, cooperative cancellation, source integrity and protected output paths.

An independently produced private UE 5.3.2 Windows IoStore corpus contains three selected skeletal meshes: 3,970 render vertices, 6,142 triangles and 71 reference bones in total. An independent Python wire reader matches 118,533 values against Unreal API exports. A separately installed Core-only consumer matches 91,299 projected values against the same independent exports. Counts differ because the common geometry model excludes zero influence padding and the unused tangent-X W component. All original sources, container partitions, mappings and reference inputs retain their hashes. These real files have fixed 8-bit skin streams and inline LODs; other width/mode combinations are fixture evidence.

Unstripped duplicate-vertex tables and custom skeletal vertex attributes currently fail with `cooked-skeletal-feature`. Cooked-out LODs remain explicit unavailable states. Offline ray-tracing bytes are fingerprinted without interpreting BLAS contents. Later object-native bytes retain offset/size/hash as an opaque tail. Further animation codecs, runtime Nanite evaluation and other engine/platform profiles remain open. The desktop integration described below is available for this same explicit profile.

The optional [Unreal reference adapter](../tools/unreal_reference/README.md) is original preparation/measurement tooling, separate from the runtime reader. Engine-owned samples, generated schemas/containers, host codecs and private reports are never committed or bundled.


## Desktop review

In the container source panel, enable **Cooked skeletal settings** for each side. Its profile, producer mobile-LOD presence and omitted-color fallback are separate from the StaticMesh controls. A stored color property remains authoritative; keep **Vertex colors** unknown when the property itself supplies that fact. The desktop CLI accepts `--skeletal-profile ue5.3-windows`, `--skeletal-keep-mobile-min-lod 0|1` and optional `--has-vertex-colors 0|1`, with corresponding `--after-...` settings. It does not infer the after-side reader from the before-side settings.

**Cooked meshes** presents each decoded skeletal object/LOD and side through the existing orbit wireframe and skin-aware changed-edge classification. Enable **Bones and weights** to browse named reference bones and render-vertex weight profiles. The list is clipped for large skins; selecting a bone or vertex prepares only that entry's typed details. Search includes stored names and weight values; changed-entry navigation and before/after details preserve raw values without normalization or skin evaluation. A restored or navigated selection scrolls into view. The list header remains visible while scrolling.

An inspected peer with no available geometry projection produces an explicit comparison-unavailable notice. Its current geometry and skin remain inspectable, without false removed/modified colors. A known absent container member retains actual removal semantics.

Workspace format 9 retains independent native profiles/producer facts, selected mesh/LOD/side, orbit camera, skin mode, selected bone/vertex, filters and typed inspector state. Formats 1–8 remain readable with the new skeletal reader disabled and producer facts unknown. EN/FR labels and all five themes cover this path.


## Streamed LOD storage

A streamed LOD reads its Zen bulk-map entry and cached availability record, then validates the complete render/skin window with the same buffer decoder used for inline LODs. Cached index/vertex/UV/color/influence dimensions must agree with loaded buffers. A cached record is availability information, never replacement geometry.

Bulk payloads can be embedded in the package or stored in ordinary, optional, duplicate-optional or memory-mapped chunks. Used external chunks receive mandatory whole-chunk IoStore integrity verification, including bytes outside the selected LOD window. Shared chunks are decoded once and charged once against the aggregate read budget within a mesh read. Every LOD retains its own offset, byte count, SHA-256 and chunk-source hash under `bulk`; inline LODs have `bulk: null`.

Missing, unused and empty bulk payloads remain explicit unavailable LODs beside any valid loaded LODs. Unused nonzero declarations consume no inline bytes. Discarded zero-byte streamed payloads have no availability record. Geometry diffs compare decoded data rather than storage: identical inline/streamed geometry stays identical, while an external-only vertex/weight change is visible even when export-bundle bytes match. A missing selected LOD cannot be silently substituted by another one.

Additional real UE 5.3.2 Windows evidence comes from a private three-LOD copy generated through Unreal's public editor API with explicit streaming overrides, two allowed streamed LODs and producer `r.MeshStreaming=1`. LODs contain 3,922/1,300/752 vertices and 6,118/1,528/764 triangles. The first two occupy disjoint 178,043-byte and 56,111-byte windows in the same 234,154-byte external chunk; the third is inline. A separately installed Core-only consumer matches 134,930 projected values against independent Unreal API exports. An independent Python wire reader matches 175,902 values. All three LOD geometry comparisons against the separately cooked all-inline version are identical.

Original fixtures additionally cover width/mode/precision/color combinations in mixed streamed/inline LODs, storage variants, duplicate fallback, shared-chunk caching, whole-chunk corruption outside the selected range, missing/unused/empty data, availability mismatches, budgets, cancellation and every truncated mixed native window. The same desktop path presents all loaded LODs and preserves unknown-peer semantics when a streamed payload is missing.

## Named alternate cooked weight profiles — post-0.181 development

The selected UE 5.3 Windows reader now supports the native `FSkinWeightProfilesData` map. Each `CookedSkeletalLod::weight_profiles` entry retains its exact stored FName, replacement slot count, vertex-to-record map, typed replacement slots and bounded byte/hash provenance. Shared records may be referenced by several vertices. Section-local bone IDs resolve separately through each owning section's bone map. Both 8/16-bit bone indices and 8/16-bit weights use the widths declared by the base stream. Eight-bit weights expand exactly by 257; no normalization or mesh deformation is applied.

The base render stream keeps its existing `MeshSkin` key `default`. Named cooked profiles use `profile:<stored FName>` to avoid a collision with that base identity. Materialized stored-slot profiles retain every unchanged base vertex and every base influence slot after the replacement prefix. A zero replacement removes that slot's contribution without erasing untouched later slots. Typed raw replacements retain zero padding; semantic `MeshSkin` entries omit it. Empty replacement maps still retain a named profile with the complete base weights. This describes stored data; runtime automatic-default selection, component profile switching, streaming policy and skin deformation are not evaluated.

Inline profile names use package FNames. Streamed buffer names use the memory archive's FString encoding, even when the bulk window is physically embedded in the package. Cached availability names remain package FNames and must match the loaded profile set. UTF-16 names validate surrogates/terminators; narrow names use the selected schema context's explicit encoding, or strict ASCII when unspecified. `CookedSkeletalOptions` additionally exposes that narrow encoding override and a 4,096-byte profile-name limit.

The native `BuffersSize` counting archive does not serialize FNames. Consequently its declared size excludes physical inline FName bytes and streamed FString bytes. The reader verifies the exact counted size after subtracting only those parsed name windows. It separately records `serializedBytes`, `declaredBuffersSize` and `profileNameBytes`; the complete physical bulk boundary still has to be consumed. It does not accept a general size mismatch.

Defaults bound aggregate loaded profiles to 64, overridden vertices to 1,000,000 and accounted output to the existing reader budget. Base-profile copying is accounted and cancellable per vertex. Duplicate names/vertices, invalid record offsets, inconsistent byte widths/counts, missing section bones, nonzero duplicate influences, oversized names, cached-name mismatches and invalid size counters fail before a model is published. Missing payloads continue to expose cached names under bulk availability without inventing profile values.

`to_json(true)` includes typed sparse replacements and fully resolved skin profiles. Default metadata output retains names, dimensions and provenance without expanding replacement slots. Existing CLI `zen-skeletal`/`zen-skeletal-diff`, installed Core consumers and desktop **Bones and weights** review use the new values. The desktop can compare different object names when the caller explicitly selects both package members and each package has exactly one mesh export: equal LOD indices then identify the selected comparison scope. Reports retain both source keys and the explicit pairing rule. Implicit pairs and packages with several mesh exports require their existing exact object/LOD identity; unknown projections do not become removed weights.

Independent validation uses seven newly cooked private mesh copies, including unchanged and modified named profiles, 8/16-bit weights and a three-LOD case with two streamed windows. The optional original reference adapter prepares alternate imported influences through the Engine's own build utilities and exports raw profile records plus complete results from the actual native override API. Core does not run that adapter or require Unreal. Original bounded fixtures additionally exercise partial replacement prefixes, shared records, zero padding, empty maps, 16-bit indices, narrow/UTF-16 streamed names, invalid counters, damaged input, limits and cancellation.

## Compressed render morphs — post-0.181 development

Loaded LODs now decode their native compressed morph buffer and bind target names through the mapped `MorphTargets` references to native owned `UMorphTarget` exports. Sparse decoded position/normal contributions, preserved empty targets, exact native metadata and an EN/FR desktop inspector are available. Unknown names produce incomplete morph comparisons, with no invented removals. See [the full Core/CLI/SDK/UI contract and evidence](COMPRESSED_MORPHS.md). Subsequent cloth and duplicate-vertex additions are described below; custom attributes remain outside this decoder.

## Stored cloth deformer mappings — post-0.181 development

The reader now retains typed section/LOD-bias cloth mappings and asset binding indices/GUID words, validates the separate 64-byte render buffer and streaming metadata, and compares active mapping changes. Core/CLI/SDK and the EN/FR desktop expose focused section/render-vertex inspection and persistent navigation. Padding/inactive unused data remain in raw reports and fingerprints. Physical cloth asset data, simulation bounds and runtime deformation are not inferred. See [the complete cloth contract](CLOTH.md).

## Stored duplicate-vertex tables — post-0.181 development

Unstripped section-local duplicate-vertex arrays are now typed, bounded and checked against their render section. Core/CLI/SDK compare resolved relations independently of buffer packing, and desktop wireframes mark known relation changes. Stripped tables remain unavailable instead of becoming empty relations. See [the contract, validation and limits](MESH_OVERLAPS.md).
