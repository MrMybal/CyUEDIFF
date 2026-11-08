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
- Animation exports use `IAnimationDataModel` names and integer-frame transforms. These are **source model samples**, not decoded compressed/cooked tracks. Duration, frame rate and sample counts are explicit.
- Static mesh exports expose Nanite resource statistics and page states. `residentStreamingBulkBytes` is only the currently resident bulk allocation; a zero value does not imply absent streamed pages. `nonRootStoredPageBytes` sums non-root page-state bulk sizes. Page states and counts are evidence for future readers, not decoded Nanite geometry.
- `BindPrivateSkeleton(asset, skeleton, physics)` can bind copied skeletal meshes or animations to copied skeletons and optional copied physics assets. Every supplied object must be under `/Game/Corpus/`; the method marks the private asset dirty but does not save a file.
- `CreatePrivateGrid(cells)` constructs original procedural surface geometry in `/Game/Corpus/NaniteGrid`, with Nanite enabled. It accepts 2..256 cells per side and refuses an existing object. It returns an unsaved asset; the caller must explicitly save its private project. A 128-cell grid has 32,768 source triangles, providing multiple Nanite pages in the validated build.

Cooking the isolated project for Windows and packaging its loose cooked output with the matching UnrealPak/IoStore producer supplies independently produced runtime bytes. The adapter's editor render resources may differ from cooked resources if platform reduction or build settings differ; validators must compare actual values and reject mismatches.

Use `scripts/validate_cooked_skeletal.py` with the separately installed Core-only `cooked_skeletal_consumer` to compare cooked geometry/skin against these API exports. Engine assets and generated `.usmap`, cooked containers, reference JSON and private paths stay outside Git.
