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

Nonempty cloth mappings/bindings, unstripped duplicate-vertex tables, alternate cooked weight profiles, compressed morph buffers and custom skeletal vertex attributes currently fail with `cooked-skeletal-feature`. Cooked-out LODs remain explicit unavailable states. Offline ray-tracing bytes are fingerprinted without interpreting BLAS contents. Later object-native bytes retain offset/size/hash as an opaque tail. Compressed animation, full Nanite decoding and other engine/platform profiles remain open. The desktop integration described below is available for this same explicit profile.

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
