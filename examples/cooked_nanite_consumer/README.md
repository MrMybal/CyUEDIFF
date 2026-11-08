# Whole cooked Nanite consumer

This installed example links only `CyUEDiff::Core`. It reads explicitly mapped UE 5.3 Windows cooked StaticMesh packages from IoStore, binds resident/streamed Nanite resource pages and queries immutable positions, oriented triangles, numeric materials and stored hierarchy facts. It needs no Unreal, UI or Git runtime.

```sh
cmake -S <prefix>/share/CyUEDiff/examples/cooked_nanite_consumer -B build -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build --config Release
cooked_nanite_consumer INPUT.utoc PACKAGE_ID GLOBAL.utoc CONTEXT.json MOBILE_0_OR_1 OUTPUT.json [CODEC]
```

Supply the actual producer `r.StaticMesh.KeepMobileMinLODSettingOnDesktop` as 0 or 1, a matching explicit Zen schema manifest and matching global script-object table. `CODEC` is an optional caller-owned separately licensed block-decoder module; none is loaded by engine-root discovery. Containers, partitions, context/mapping and codec files are protected from output replacement, including existing hard links. The sample caps decoded/output vertices and triangles at 200,000 each. Bounds/cancellation defaults still apply to Core parsing.

Output separates the Nanite catalog, focused stored hierarchy nodes and page/cluster geometry queries. Unavailable/stripped data stays unknown; a serialized empty resource stays known empty. The sample can return metadata for either case. The example explicitly enables stored normals, optional tangents, RGBA colors and up to four UV channels, with packed identities and native values. Zero-bit normals and absent tangents retain explicit unavailable/absent status. Runtime streaming/LOD cuts, rendering/animation and later native object data remain outside this example.

The installed `validate_nanite_geometry.py` accepts a whole-package manifest with schema `cyuediff.cooked-nanite-validation/1`; see `docs/NANITE_GEOMETRY.md`. Independent Engine CPU decoder and source MeshDescription API references stay private and are explicit inputs. The validator checks full stored page identity/dependencies, source package identity, all oriented triangle indices, integer/float position consistency and full-leaf oriented quantized source-surface correspondence. An optional `attributeReference` binds independent actual shader outputs to exact page hashes and validates all packed/decoded shading channels. Other native profiles remain outside this example.

The attribute path is optional in Core and enabled by this sample. See `docs/NANITE_GEOMETRY.md` for exact transcode/precision semantics, memory bounds and the independent D3D11 WARP shader reference contract. Existing validation manifests remain supported; `attributeReference` adds explicit shader validation.
