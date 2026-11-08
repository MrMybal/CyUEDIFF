# Nanite page geometry consumer

This installed SDK example links only `CyUEDiff::Core`. It decodes an explicitly selected UE 5.3 Windows page set and queries immutable cluster positions, oriented triangles and numeric material indices. The example explicitly enables stored shading attributes; runtime LOD selection remains outside its scope.

```sh
cmake -S <prefix>/share/CyUEDiff/examples/nanite_geometry_consumer -B build -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build --config Release
nanite_geometry_consumer PAGES.json OUTPUT.json
```

```json
{
  "schema": "cyuediff.nanite-pages/1",
  "rootPages": 1,
  "pages": [
    {"file": "ABSOLUTE/stored-page-0.bin", "flags": 0, "dependencies": []},
    {"file": "ABSOLUTE/stored-page-1.bin", "flags": 1, "dependencies": [0]}
  ]
}
```

Use actual absolute file paths. Each file includes the fixup chunk and the full disk page. Resource page order, root count, dependency indices and flags must come from the matching producer context. The example does not search for page magic, extract arbitrary containers or infer dependencies. It accepts at most 4,096 explicit pages, bounds source bytes and caps output at 200,000 vertices and 200,000 triangles. Manifest/page files, including existing hard links, cannot be overwritten by the result.

`validate_nanite_geometry.py --consumer EXE --manifest VALIDATION.json --output NEW_DIRECTORY` validates complete page sets. The validation manifest has schema `cyuediff.nanite-geometry-validation/1`, `cases` with absolute existing `pages`, `triangleReference` and `sourceReference` paths, and optional `protectedSources` entries (`path`, `sha256`). A triangle reference is a nested page/cluster/triangle/three-index array from an independently executed matching decoder. The `nanite-source` mode of the optional original Unreal adapter supplies the source-reference schema. Validation checks exact triangle indices, native integer/float scaling and full-leaf oriented quantized triangle-position correspondence; it does not certify attributes without an explicit `attributeReference`, or runtime LOD cuts. Inputs and references stay private. See `docs/NANITE_GEOMETRY.md` in the source tree.

The attribute path is optional in Core and enabled by this sample. See `docs/NANITE_GEOMETRY.md` for exact transcode/precision semantics, memory bounds and the independent D3D11 WARP shader reference contract. Existing validation manifests remain supported; `attributeReference` adds explicit shader validation.
