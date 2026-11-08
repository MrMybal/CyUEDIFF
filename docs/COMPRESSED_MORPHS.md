# Cooked compressed render morphs

This post-0.181 source feature reads the **UE 5.3 Windows** native compressed morph buffers inside cooked SkeletalMesh render LODs. Core, CLI and the desktop read files without loading Unreal. This is stored render data; it does not reproduce a posed character, animation blending, skinning or runtime normal recomputation.

## Complete package binding

Use the explicit [cooked skeletal reader](COOKED_SKELETAL.md) with the matching Zen profile, script table, USMAP context and producer mobile-LOD/color facts. The reader consumes the compressed buffer at the exact native field boundary in inline or streamed LOD storage. Used container chunks retain whole-chunk integrity verification.

Names bind **in the stored `SkeletalMesh.MorphTargets` array order**, through resolved references to non-template native `/Script/Engine.MorphTarget` exports directly owned by the selected mesh. Export table order is not a morph index. Array cardinality must match the native compression table; empty/duplicate names and malformed records are rejected. Missing properties, external references, unresolved identities or custom morph subclasses leave named comparisons unavailable. Omitted properties do not certify producer defaults. Raw indexed deltas and native fingerprints remain queryable in those cases.

`CookedSkeletalLod::morphs` and `MeshGeometry::render_morphs` share the immutable model. `lod.morphs` JSON includes precision, source extrema, per-target batch/count metadata and native/data SHA-256. Full `zen-skeletal` output also includes each indexed integer and decoded delta. Source extrema are pre-quantization metadata, not recomputed decoded bounds.

## Stored values and comparisons

The decoder supports the native 64-entry batches, cross-word fields, index minima/lane offsets, signed position/normal minima, zero-width fields, optional normal payloads, empty targets and empty buffers. Position widths are at most 28 bits and normal widths at most 16 bits. Every index, component range, table partition, padded data window and float is checked before publishing the model. Physical packing may share or reorder valid bit windows; raw hashes retain those distinctions.

Decoded vectors use the stored float32 position and normal precision. Small authored deltas can quantize to zero. Target names remain in the inventory even when no nonzero delta survives. A missing entry means zero in this **stored render** scope. Normals omitted by the producer for recomputation are zero stored contributions; they do not establish that the authored normal delta was zero.

Geometry JSON adds a separate `renderMorphs` projection with exact names and sparse render-vertex identities. Repeated entries for the same vertex retain their sequence and multiplicity, with no aggregation or blend-weight assumption. The existing dense source MeshDescription `morphs` contract remains distinct. Source/render vertex correspondence is not inferred.

`compare_meshes` compares names and decoded vectors; changing compression precision or batch packing without changing decoded vectors does not manufacture a geometry difference. Native hashes/metadata still report encoding changes. When either named binding is unavailable, morph differences are excluded, the comparison returns `status: incomplete`, `complete: false`, and a diagnostic. Other decoded geometry changes remain available. CLI exit code 2 covers this incomplete comparison. A verified absent native buffer is a known empty stored-buffer scope.

```text
cyuediff zen-skeletal BUILD.utoc PACKAGE_ID [explicit skeletal context]
cyuediff zen-skeletal-diff BEFORE.utoc ID AFTER.utoc ID [explicit skeletal context]
```

See [the full context flags](COOKED_SKELETAL.md#cli-and-sdk). Independent after contexts and output protection apply unchanged.

## Reusable primitive and limits

```cpp
#include <cyuediff/compressed_morphs.hpp>

cyuediff::CompressedMorphOptions options;
options.profile = cyuediff::MorphCompressionProfile::unreal_5_3_windows;
auto buffer = cyuediff::CompressedMorphBuffer::parse(bytes, renderVertexCount, options);
auto metadata = buffer.morph_information(0);
auto delta = buffer.delta(0, 0); // Original entry, quantized and float32 vectors.
```

`from_native_window` bounds reads to an enclosing offset/end and reports exact consumed bytes. Parsed words are owned independently of caller storage. The model does not expand every target to a full zero-filled vertex array. Metadata, batches and deltas are separately queryable with cancellation. Native words, output bytes, vertices, targets, batches and aggregate deltas have explicit budgets; batch parsing and delta validation report progress. Whole-reader defaults cap aggregate loaded morph targets at 4,096, batches at 1,000,000 and entries at 4,000,000, within the reader's overall read/output limits. SDK headers and original tests contain the precise API defaults.

## English/French desktop review

In **Cooked meshes**, enable **Render morph targets**. Select a target, filter its sparse render vertices or decoded values, show changed vertices, navigate previous/next changes and select a vertex for focused typed before/after position/normal values. The list is clipped and selecting a high vertex scrolls it into view. Empty targets remain selectable. The wire preview highlights edges incident to changed stored morph vertices. Unknown peer identities do not become removed vertices.

Target, vertex, filter, change-only mode and focused property state persist with the selected cooked mesh in the existing workspace format. Controls are English by default and translated into French. This inspector is available in source builds; the existing 0.181 archive predates it.

## Evidence and remaining scope

Original numerical tests cover packed extrema, duplicate indices, empty targets, constant batches, every truncated prefix, corrupt headers/windows, budgets and cancellation. Complete original cooked fixtures exercise reordered exports, explicit references, unavailable/custom bindings, inline/external storage, semantic packing equivalence, CLI output protection and bilingual desktop/workspace integration. Widget checks exercise five themes, wide/narrow frames and a 40,000-vertex list without flattening unrelated values into the selected inspector.

Independent private UE 5.3.2 copies provide five cooked meshes and seven LODs, with twenty-eight named target instances and both sparse and dense changes. Two LOD windows use external bulk storage. Their complete native compression hashes match fresh Engine serialization, and every stored render index and quantized component matches the Engine source-delta API's quantization. An independently built consumer uses only the installed Core SDK. The optional `validate_cooked_skeletal.py` manifest accepts `morphReference` beside its existing geometry reference. It checks target identities, complete native bytes, filtered quantized entries and decoded float vectors. References, private assets, Engine implementation and generated buffers are excluded from distribution.

Additional engine/platform layouts, custom morph compression, runtime deformation, cloth, duplicate-vertex tables and custom vertex attributes remain separate work. Passing this profile does not establish universal Unreal serialization coverage.
