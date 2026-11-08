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
