# Optional Unreal reference adapter

This original, editor-only plugin exports independent Unreal API values for validation. It is **not a dependency of CyUEDiff Core, the CLI or the desktop**, and is never loaded when CyUEDiff reads a file. An installed matching engine is used only to prepare or measure a private corpus. No Engine code, binaries, codecs or sample assets are distributed here.

The adapter targets **Unreal Engine 5.3**, with a compile-time major/minor check. It was built and exercised with UE 5.3.2 on Windows. Other versions require a reviewed adapter and new evidence.

## Setup in an isolated project

1. Create a separate content project in an ignored build directory. Copy only this plugin's `.uplugin` and `Source` into `Project/Plugins/CyUEDiffReference`.
2. Enable `CyUEDiffReference` and `PythonScriptPlugin` in that project's `.uproject`. Build its Development Editor target with the matching UnrealBuildTool, `-NoEngineChanges -NoHotReload -NoXGE`. Keep intermediates, logs, user settings and DDC inside the private workspace. If an installed engine uses a read-only XML configuration cache, copy that existing cache locally and pass UnrealBuildTool `-XmlConfigCache=ABSOLUTE_LOCAL_CACHE`.
3. Use private copies of permitted source assets under `/Game/Corpus/`, and keep SHA-256 manifests of originals and companions before and after preparation. Do not save original assets or distribute copied Engine/project content with CyUEDiff.
4. Run a PythonScript commandlet with `-unattended -NullRHI -nosound -NoLiveCoding -NoSourceControl -NoAnalytics -NoTraceServer -NoMessaging`. Supply explicit local `-userdir`, `-LocalDataCachePath`, `-ShaderWorkingDir` and `-abslog` destinations.

The supplied `export_reference.py` reads `CYUEDIFF_REFERENCE_MANIFEST`, an absolute path to a private manifest. The manifest's `output` directory must be new, and all exported assets must be private `/Game/Corpus/` copies. For example:

```json
{
  "output": "D:/validation/build/reference",
  "assets": [
    {"object": "/Game/Corpus/SkinCube", "file": "SkinCube.json"},
    {"object": "/Game/Corpus/NaniteGrid", "file": "NaniteGrid.json"}
  ]
}
```

```powershell
$env:CYUEDIFF_REFERENCE_MANIFEST = 'D:/validation/build/reference-manifest.json'
& 'ENGINE/Binaries/Win64/UnrealEditor-Cmd.exe' 'PROJECT/Corpus.uproject' -run=pythonscript -script='TOOLS/export_reference.py' -unattended -NullRHI -nosound -NoSourceControl -NoAnalytics -NoMessaging -NoTraceServer
```

Replace the capitalized paths and include the local scratch/log arguments described above. This exporter never saves an asset. Its output is private validation evidence, not a distributable sample set.

## API and reference meaning

- `ExportReference(asset)` returns JSON with schema, engine build, object and class identity. Skeletal meshes include reference bones and poses, render LOD positions, packed tangent values, UVs, optional colors, indices, section bone maps, required/active bones and skin weights. Engine uint8 skin weights are exposed in their lossless uint16 expansion, without normalization by this adapter.
- Default animation exports use `IAnimationDataModel` names and integer-frame transforms. These are **source model samples**, not decoded compressed/cooked tracks. Duration, frame rate and sample counts are explicit.
- Static mesh exports expose Nanite resource statistics and page states. `residentStreamingBulkBytes` is only the currently resident bulk allocation; a zero value does not imply absent streamed pages. `nonRootStoredPageBytes` sums non-root page-state bulk sizes. Page states and counts are evidence for future readers, not decoded Nanite geometry.
- `BindPrivateSkeleton(asset, skeleton, physics)` can bind copied skeletal meshes or animations to copied skeletons and optional copied physics assets. Every supplied object must be under `/Game/Corpus/`; the method marks the private asset dirty but does not save a file.
- `CreatePrivateGrid(cells)` constructs original procedural surface geometry in `/Game/Corpus/NaniteGrid`, with Nanite enabled. It accepts 2..256 cells per side and refuses an existing object. It returns an unsaved asset; the caller must explicitly save its private project. A 128-cell grid has 32,768 source triangles, providing multiple Nanite pages in the validated build.

Cooking the isolated project for Windows and packaging its loose cooked output with the matching UnrealPak/IoStore producer supplies independently produced runtime bytes. The adapter's editor render resources may differ from cooked resources if platform reduction or build settings differ; validators must compare actual values and reject mismatches.

Use `scripts/validate_cooked_skeletal.py` with the separately installed Core-only `cooked_skeletal_consumer` to compare cooked geometry/skin against these API exports. Engine assets and generated `.usmap`, cooked containers, reference JSON and private paths stay outside Git.

## Direct compressed-codec references

Set an asset row's `mode` to `compressed-animation` to call `ExportCompressedAnimationReference(asset)`. The adapter reports the actual selected codec class/handle and the complete compressed-buffer IoHash/size. It invokes `BoneCompressionCodec->DecompressBone` directly for all integer and half frames, bypassing `GetBoneTransform`/editor raw-data fallback. A maximum of 10,000 keys and 500,000 track samples bounds the export. Bone names/indices are obtained from the owner's actual Skeleton. This remains a read-only editor API measurement, not a cooked package decoder.

Set `mode` to `per-track-synthetic` to call `ExportPerTrackSyntheticReference(codecOwner)`. This requires an actual owner using the exact Engine per-track codec. It constructs 424 bounded original numerical wire cases, then uses the actual codec to evaluate translations/rotations/scales under linear and step interpolation. The exported bytes and numerical results contain no owner asset geometry or tracks. They cover component masks, identity, float/fixed/interval/packed quaternion formats and 8/16-bit temporal tables. The original numerical reference fixture in `tests/fixtures/per_track_ue53_reference.json` records this independently evaluated corpus.

The default row `mode` is `source`, preserving existing manifests. All modes require explicit `/Game/Corpus/` private owners, unique output filenames and a new output directory in the Python exporter. No mode saves an asset. Compare real compressed buffers with the installed Core-only example and `scripts/validate_per_track_animation.py`; see [the Core contract](../../docs/ANIMATION_COMPRESSION.md). Complete buffer hashes must agree before comparing sampled poses. Skeleton binding, additive-base application, retargeting and compressed curves are separate from this validation.

## Skeleton binding references

Default `source` exports now recognize an actual `USkeleton`: `bones` contains its raw reference names/parents and local poses, and `skeletonGuid` contains the Engine API GUID in standard digits format. The cooked reader retains serialized little-endian GUID bytes; the whole-animation validator compares their four uint32 words with this API representation. This is an independent Skeleton measurement, not a borrowed SkeletalMesh bone list. `validate_cooked_animation.py` combines these values with direct compressed-codec references to check whole package extraction, registered codec identity and exact named track binding through an installed Core-only consumer. See [COOKED_ANIMATIONS.md](../../docs/COOKED_ANIMATIONS.md).


## Original compressed-curve references

`compressed-curve-synthetic` calls `ExportCompressedCurveSyntheticReference(owner)` for an explicit private owner. It compresses 78 sets of original numerical scalar curves with the three actual built-in curve codecs, then measures the actual codec's batch API. Rich cases cover all descriptor families, both useful time formats and six extrapolation modes; uniform cases include 37 curves. Only unspecified alignment gaps are zeroed before measurement so the portable numerical buffers cannot contain uninitialized process bytes. The owner supplies no animation curve/geometry values. Actual codec classes, complete buffer hashes, encoded names and runtime-name permutations are recorded. The adapter acquires the required temporary memory-stack mark for batch curves.

`PreparePrivateAnimationCurves(asset, codec)` accepts an explicit `/Game/Corpus/Curve...` animation with no existing float curves and codec index 0/1/2 (Rich/Indexable/UniformlySampled). It adds original numerical curves through the animation data controller and creates an explicit private settings asset, refusing an existing settings package. It marks these private objects dirty without saving them; the caller explicitly saves/cooks the copies. Additional constructor-created codec exports may remain, so validators must inspect the actual mapped Codec property rather than selecting a class by presence.

`compressed-curves` calls `ExportCompressedCurvesReference(asset)` and measures 257 times through the actual compressed curve codec. It includes complete curve bytes/IoHash, actual class/path, encoded names and the runtime FName permutation. No source-model fallback is used. UE 5.3 UniformlySampled's potentially out-of-row single-curve indexing path is deliberately not invoked; actual batch values are reported. Other codecs also report single-curve values. See [compressed scalar curve contracts](../../docs/COMPRESSED_CURVES.md) and `validate_compressed_curves.py`. These helpers remain optional preparation/validation tools, not a runtime dependency of Core.


## Nanite source-geometry references

`nanite-source` calls `ExportNaniteSourceReference` for an explicit private `/Game/Corpus/` StaticMesh. It reads the actual source MeshDescription vertex/triangle IDs and positions, then applies the actual resource's stored precision with the matching Engine quantization operation. Resource cluster/page counts and producer identity are recorded. This reads source geometry for correspondence; it does not claim those source arrays were decoded from Nanite pages. No fallback render mesh is used and no asset is saved. Limits are one million source vertices and two million triangles. `validate_nanite_geometry.py` compares full-leaf quantized oriented triangle positions from an installed Core-only page decoder against this API evidence. Cluster ordering and attribute seams may differ; attributes remain a separate validation requirement. See [the page geometry contract](../../docs/NANITE_GEOMETRY.md).

## Native skin-weight profile references

`PreparePrivateSkinWeightProfiles(asset, variant)` accepts only a caller-prepared `/Game/Corpus/Weight...` SkeletalMesh copy with no existing profiles and at least two reference bones. It adds original sparse `CyWeights` influences and an unchanged `CyOriginal` profile through the actual imported-data/build utilities. Variant 0/1 selects two weight ratios; 2/3 uses the corresponding ratios with 16-bit weights. The helper marks/builds the private object in memory and never saves a file. The caller explicitly owns duplication, save and cook destinations.

Skeletal `ExportReference` results now include each declared runtime profile's raw ID/weight byte arrays, slot count and vertex-to-record map. For available fixed-influence CPU buffers, the adapter calls the actual exported protected override API through an original derived wrapper and then measures every resulting vertex with native getters. It copies profile data into a temporary reference object and never changes the source buffer or initializes GPU resources. Missing/variable runtime API results remain unavailable; validators must not substitute source weights. `validate_cooked_skeletal.py` checks these optional references against a separately installed Core-only consumer, including stored record hashes, exact bone identities and all resolved profile vertices. The adapter is an optional validation tool, independent of Core runtime.

## Compressed render morph references

`PreparePrivateMorphs(asset, variant)` accepts only a morph-free private `/Game/Corpus/Morph*` SkeletalMesh copy and variant 0/1. It adds original source-import shapes, rebuilds render data through Engine utilities and marks only the supplied copy dirty; the caller owns saving. Creating render-index deltas without matching source-import records is insufficient: a subsequent Engine rebuild can discard them.

`ExportCompressedMorphReference` creates a fresh native compression buffer through Engine APIs and exports complete serialized bytes, target names, source render deltas, producer precision and quantized components. The configured tangent-recomputation mode and per-delta normal retention are recorded. Schema `cyuediff.unreal-morph-reference/1` describes compression-input quantization, not GPU blending or final deformed output. Use `morphReference` in the installed skeletal validator manifest to compare these values to the Core consumer's complete cooked extraction. No codec implementation or Engine source is copied into Core.

## Cloth mapping references

`PreparePrivateCloth` accepts private `/Game/Corpus/Cloth*` copies and variants 0–3. Even variants create a real clothing asset through the Engine factory; odd variants require its independently duplicated owned asset, preserve its GUID and modify original physical test points before rebinding. Variants 2/3 use five influences. The helper maintains the persistent source-section binding metadata within a scoped edit, so an Engine rebuild does not silently discard the mappings. It marks only the supplied copy dirty and never saves a file.

`ExportClothReference` exports structured section/LOD-bias data, render buffer/index mapping and actual `FMeshToMeshVertData` operator serialization. The installed Engine does not export the complete cloth-buffer operator; the reference therefore labels its bytes `nativeRecordBytes`, not complete-buffer bytes. It does not reconstruct that Engine implementation. Use `clothReference` in a skeletal validator manifest. Generate render LODs before binding a new cloth test asset; regenerating an already bound sample can remove its mappings, and empty reference counts cannot establish cloth streaming coverage.

`PreparePrivateOverlaps` rebuilds only caller-prepared `/Game/Corpus/Overlap*` copies, optionally changing one original wedge UV; the caller owns saving. `ExportOverlapsReference` reads raw CPU section overlap arrays and serializes the actual `FDuplicatedVerticesBuffer` into `nativeBytes`. Keep skin-cache shaders enabled for the private cook when testing retained tables: the selected Engine normally strips these arrays without a supported consumer. No Core runtime call uses this editor adapter. `overlapsReference` in the skeletal validator checks exact raw/serialized and resolved integer identities.

`PreparePrivateVertexAttributes` rebuilds original scalar float/half/normalized-byte channels only on caller-prepared `/Game/Corpus/Attribute*` copies; no file is saved. Attribute transfer runs through the normal Engine build after its vertex/import map exists. `ExportVertexAttributesReference` reads source-model values and invokes the actual native collection serializer. The private context explicitly binds `SkeletalMeshVertexAttributeInfo` from matching source metadata. `ExportVertexAttributesSyntheticReference` encodes original CPU-only channels with one to four components, half/signed-zero/range examples and normalized-byte clamping through the actual Engine API, without an asset or GPU. The selected source-import utility transfers one scalar per vertex, so real asset and procedural vector evidence remain distinguished. The optional validator accepts `vertexAttributesReference`.


`PreparePrivatePhysicalCloth(asset, variant)` prepares only a caller-owned `/Game/Corpus/ClothPhysical*` copy with an owned clothing asset and configurations. It accepts variant 0/1 and one/two physical LODs, creates original fixed/moving test masks, native tethers, two-LOD transitions and explicit Chaos parameter values. It clears inherited transition arrays before rebuilding them, so copied boundary records do not survive from another LOD. The helper marks only that private copy dirty; the caller owns saving and before/after snapshots. It never changes an original source mesh.

`ExportPhysicalClothReference(asset)` reads actual physical mesh/API fields, all twelve bone slots, source configuration values and actual native tether/transition serializer bytes. The manifest exporter accepts mode `physical-cloth` for this read-only operation. Configuration/class/archetype defaults absent from a cooked stream remain unresolved by Core. The [physical reference validator](../../scripts/validate_physical_cloth.py) checks decoded mapped values/tethers and exact raw transition bytes through the installed Core-only [hybrid consumer](../../examples/hybrid_struct_consumer/). Its selected factory references use the actual class-name configuration keys. The Core reader and its public example do not depend on this editor plugin or Chaos runtime.


The read-only `physical-cloth-geometry` mode adds direct physical bounds, modern transition fields, actual Chaos 5.3 `WrapDeformLOD` results, source vertex-color facts and native render-mapping references. It extends the earlier physical reference under its own schema and saves no source assets. Reference source revisions can be byte-identical private copies with explicit object-prefix correspondence; retain both paths and source hashes. The selected 5.3 legacy mesh API is deliberately version guarded and is not a portable later-engine API claim.


The original `CyUEDiffHybridFixture` types intentionally serialize tagged properties followed by native records. `CreatePrivateHybridSource` creates only new unsaved `/Game/Corpus/SourceHybrid185/` assets; it rejects existing destinations and does not save. Save original before/after fixtures explicitly in a private test project, then use a fresh commandlet with mode `tagged-hybrid` to export actual reloaded values. `ExportTaggedHybridReference` is read-only. Large int64 values use decimal strings in the independent API JSON to avoid double-number precision loss. These optional validation classes are not linked into Core or copied from Engine implementation. The public source consumer/validator compares the known prefix/native field contract, not an arbitrary plugin's defaults or serializer.

`ExportStructIdentityReference(asset)` accepts only an exact `UUserDefinedStruct` named `IntPoint` under the caller-owned `/Game/Corpus/StructIdentity192/` directory. It is read-only and saves nothing. It calls the actual reflected serializer for that supplied instance and native `FIntPoint` controls, using a controlled name table and explicit 5.3 file versions. Results retain actual tag/payload bytes, API owner/GUID, serialization flags and declared field counts. Empty stored overrides do not represent reconstructed class defaults. The Core proof wraps these actual property streams in controlled package headers; it does not claim the wrapper is a complete Engine-produced package. See [the identity contract](../../docs/STRUCT_IDENTITY.md).
