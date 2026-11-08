# Cooked static render geometry

The Core reads an explicit **UE 5.3 Windows** StaticMesh render-buffer profile without Unreal libraries, a graphics device, Git or the Editor. This is the render-LOD prefix of native serialization. Nanite, distance-field and later object data remains an identified opaque tail; a successful render read does not claim those formats are decoded.

## Explicit context

Use a matching Zen UE 5.3 property mapping and script-object table. A successfully decoded StaticMesh property stream establishes the native start. No byte scanning or inferred native boundary is used. `mappingVerified: false` means the supplied mapping is not independently certified by the file itself.

Set `CookedMeshOptions::profile` to `unreal_5_3_windows` and supply `keep_mobile_min_lod_setting`. This is the producer value of `r.StaticMesh.KeepMobileMinLODSettingOnDesktop`. Its presence is not marked in serialization. A missing profile or setting is refused. Each comparison side can use its own context and setting.

The selected profile is an editor-stripped, non-template `/Script/Engine.StaticMesh` in a cooked Zen export-bundle chunk. Other native target/version layouts require a separate reader. Server-stripped buffer layouts are not accepted.

## Stored projection

Each LOD retains its strip flags, cooked-out/inlined status, sections, material indices, index windows, deviation, buffer sizes and payload hashes. Available render geometry uses the public `MeshGeometry` model:

- Float32 positions, signed packed 8-bit or 16-bit tangent/normal basis and stored binormal sign. Samples are not renormalized; the most-negative signed packed value may be slightly below -1.
- Half or full float UV channels, optional Windows BGRA8 vertex colors and 16/32-bit primary, reversed and depth index buffers.
- Material-grouped oriented triangles, with one instance per split render vertex. Render vertex IDs can differ from source MeshDescription IDs.
- Imported material-slot labels when stored, otherwise runtime slot labels or an explicit `$material-index:N` label. Full mapped material records remain available separately. White is a display fallback for an unstored color channel, explicitly identified in attributes.
- Area-sampler table boundaries and counts. Offline ray-tracing BLAS bytes are fingerprinted, without a geometry interpretation of the BLAS.

Availability flags and metadata for streamed LODs are retained. Advertised index channels are checked against their payload. Producer support settings can clear inactive metadata even when the serialized buffer is present; the payload remains the stored projection.

Inline LODs and inline, ordinary, optional, duplicate-optional or memory-mapped bulk windows are supported. Used external chunks receive mandatory whole-chunk IoStore BLAKE3 verification before publishing geometry. Missing chunks, unused/empty payloads and cooked-out LODs are explicit unavailable states, not empty replacement meshes. The later native tail retains offset, size and SHA-256.

## CLI and desktop

```text
cyuediff zen-mesh BUILD.utoc PACKAGE_ID --cooked-profile ue5.3 --schema context.json --script-objects global.utoc --mesh-profile ue5.3-windows --keep-mobile-min-lod 0
cyuediff zen-geometry BUILD.utoc PACKAGE_ID [same context] --lod 0
cyuediff zen-export-mesh BUILD.utoc PACKAGE_ID [same context] --lod 0 --out mesh.obj
cyuediff zen-mesh-diff BEFORE.utoc ID AFTER.utoc ID [same context] --lod 0
```

`[same context]` above abbreviates all explicit context options; it is not literal CLI syntax. Add `--codec MODULE` when the container needs a host-supplied decompressor. Use `--after-schema`, `--after-script-objects`, `--after-cooked-profile`, `--after-mesh-profile`, `--after-keep-mobile-min-lod`, `--after-export-index` and `--after-lod` for independent after-side selection. The default after context inherits the explicit before context. Differences include each side's native provenance and external-buffer hash, so an external-only change is visible even with identical export-bundle bytes. Output destinations cannot overwrite inputs, mappings or container partitions.

In CyUI, choose the native profile and producer mobile-LOD setting under **Cooked mesh settings**, then inspect the member. **Cooked meshes** supplies object/LOD and before/after selection, orbit navigation and colored geometry changes. EN/FR labels and workspace retention cover the profile, producer setting, tab, selected LOD/side and camera. Unsupported/native-tail diagnostics remain visible. Only loaded geometry can be selected; missing LODs do not silently fall back to another LOD.

## Bounded Core integration

```cpp
cyuediff::CookedMeshOptions mesh;
mesh.profile = cyuediff::CookedMeshProfile::unreal_5_3_windows;
mesh.keep_mobile_min_lod_setting = false; // Explicit producer fact.
auto result = cyuediff::read_zen_static_mesh(container, entry, zen, read, mesh);
auto geometry = result.lods.at(0).geometry;
```

Limits cover aggregate package/external reads, decoded working/output allocations, LODs, sections, vertices, all index buffers, drawn triangles and UV channels. Array footprints, finite samples, triangle/section ranges, buffer-size accounting and external range ends are checked. Cancellation propagates through reads and inner loops. `geometry = false` validates/inventories the supported buffers without constructing the returned geometry. JSON serialization is a separate caller-owned allocation.

The installed `examples/cooked_mesh_consumer` links only `CyUEDiff::Core`. `scripts/validate_cooked_meshes.py` compares every available LOD with CLI output and matches LOD 0 against source oriented material triangles. The validator intentionally refuses to certify pairs whose cooking applies reduction or transforms that break exact correspondence.

## Evidence and limits

Original fixtures exercise inline/streamed storage, ordinary/optional/mapped chunks, half/full UVs, both tangent precisions, vertex colors, both index widths, producer settings, corruption, cancellation, budgets and every truncation of the supported native prefix. A controlled external-only position edit changes the geometry diff while package bytes remain identical.

The selected real UE 5.3 corpus contains 84 static meshes and 141 inline LODs. All LOD 0 oriented material triangles match their loose sources: 106,627 triangles. Every render position belongs to the source; one source has an additional unused position. This is real inline/static evidence. Streamed/optional/mapped storage is fixture evidence; it is not a claim of independent real streamed or skeletal coverage.

This reader does not decode cooked skeletal buffers, cooked morphs, compressed animation, physics blobs, Nanite clusters or other Unreal/platform versions. Those workstreams remain open in [completion criteria](COMPLETION.md).
