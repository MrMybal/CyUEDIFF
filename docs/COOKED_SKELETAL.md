# Cooked skeletal render geometry

CyUEDiff Core reads explicit **UE 5.3 Windows editor-stripped inline SkeletalMesh render LODs** without launching or linking Unreal. This reader is separate from modern source MeshDescription skinning, source skeleton/animation inspection and native Chaos physics.

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

Streaming LOD payloads, nonempty cloth mappings/bindings, unstripped duplicate-vertex tables, alternate cooked weight profiles, compressed morph buffers and custom skeletal vertex attributes currently fail with `cooked-skeletal-feature`. Cooked-out LODs remain explicit unavailable states. Offline ray-tracing bytes are fingerprinted without interpreting BLAS contents. Later object-native bytes retain offset/size/hash as an opaque tail. Compressed animation, full Nanite decoding and other engine/platform profiles remain open. The new skeletal reader currently has a Core/CLI/SDK path; its dedicated desktop integration remains to be completed.

The optional [Unreal reference adapter](../tools/unreal_reference/README.md) is original preparation/measurement tooling, separate from the runtime reader. Engine-owned samples, generated schemas/containers, host codecs and private reports are never committed or bundled.
