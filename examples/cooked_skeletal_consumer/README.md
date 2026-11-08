# Installed Core-only cooked skeletal example

This standalone program links only `CyUEDiff::Core`. Configure it from an installed SDK:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=D:/SDK/CyUEDiff
cmake --build build --config Release
build/Release/cooked_skeletal_consumer.exe BUILD.utoc PACKAGE_ID GLOBAL.utoc CONTEXT.json 0 stored > mesh.json
```

Arguments select an explicit UE 5.3 Windows package member, matching script-object container and schema context, producer mobile-min-LOD presence (`0|1`) and color fallback (`0|1|stored`). Use `stored` when a decoded mapped `bHasVertexColors` is available; an omitted property requires an explicit producer fact. An optional final codec-module argument belongs to the host and is never bundled by this sample. Output is JSON on stdout; exit 2 reports an error.

The result retains bounded native metadata, reference bones, inline/streamed render geometry and raw skin values. Named alternate weight profiles include sparse typed stored replacements and complete stored-slot profiles in `MeshSkin`, using `profile:<name>` beside the base `default`. No normalization, component default-profile switching or deformation is evaluated. See [the reader contract](../../docs/COOKED_SKELETAL.md).

The installed `validate_cooked_skeletal.py` script compares this program's output to explicit matching Unreal reference-adapter exports. Optional profile references check original record bytes/hashes, sparse vertex identities, mapped section bones and every result from the actual native override API. Reference preparation and Unreal execution are separate optional tools; the consumer itself requires no Unreal, UI, Git or GPU runtime.

The consumer also includes named compressed render morphs when present in supported cooked LODs. Optional validator manifest `morphReference` paths point to matching `ExportCompressedMorphReference` JSON and check exact whole-buffer hashes, all quantized indices/components and decoded float32 deltas. Unknown target bindings remain unavailable; no runtime blending or source/render correspondence is inferred. See the installed `COMPRESSED_MORPHS.md` contract.
