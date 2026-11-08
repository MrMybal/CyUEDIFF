# Nanite cluster geometry — post-0.181 development

`nanite_geometry.hpp` provides an immutable Core decoder for complete **UE 5.3 Windows stored Nanite pages**. It reconstructs quantized/float positions, oriented triangle indices and numeric material partitions across local and parent-page vertex references. It needs no Unreal, GPU, Git or UI runtime. The page-set API accepts explicit producer metadata. The opt-in whole StaticMesh path now binds that metadata and its stored pages directly from a verified cooked native boundary; both APIs expose the same immutable geometry.

## Model and native meaning

Each input contains its complete fixup chunk and disk page after container decompression. Complete page SHA-256 remains provenance. The selected layout checks fixup/page/cluster counts, raw SOA packed clusters, GPU segment sizes/offsets, UV decode-table bounds, index streams and strip block prefix counts, cluster maps, split reference streams, vertex masks and byte-aligned position/attribute windows. Page/cluster fixup targets and dependency ranges are checked; a hierarchy node table and runtime installation are outside this primitive.

The independent sequential strip decoder retains triangle orientation. The GPU may rotate a triangle cyclically to put its minimum index first; that does not change its oriented surface. Repeated indices and index deltas outside the native five-bit constraint are refused. Inline three-material partitions and slow explicit material ranges must cover every triangle. Material values remain numeric resource indices, not guessed slot names.

Local references address a source cluster's **coded non-reference vertices**. Parent references address that dependency's fully reconstructed original vertex table. An iterative dependency graph establishes the required order without recursive stack growth or repeated page rescanning. Missing, duplicate, cyclic, out-of-range and undeclared relative references fail. Referenced absolute integer positions must fit the destination's stored quantization range. `quantized_position` retains exact int32 values; `position` exposes the selected native float32 scaling.

Every stored cluster remains available, including coarser hierarchy representations. `fullLeaf` identifies the stored full-leaf flag; it is not a runtime camera/error-dependent LOD selection. Combining all clusters as one surface would duplicate hierarchy levels. Normals, tangents, colors and UV **values remain opaque** in this milestone. Their windows remain checked and hashed; no placeholder attributes are claimed as decoded. Full attribute decoding and desktop projection remain required follow-up work. The whole-package path and scoped full-leaf surface differences are described below.

## API and budgets

```cpp
cyuediff::NaniteGeometrySource source;
source.root_pages = verified_root_count;
source.pages = complete_pages_in_resource_order;
cyuediff::NaniteGeometryOptions options;
options.profile = cyuediff::NaniteProfile::unreal_5_3_windows;
auto model = cyuediff::decode_nanite_geometry(source, options);
auto metadata = model.cluster_metadata(0, 0);
auto integer_position = model.quantized_position(0, 0, 0);
auto native_position = model.position(0, 0, 0);
auto triangle = model.triangle(0, 0, 0);
```

Defaults limit 16,384 pages, one million clusters, four million vertices, eight million triangles, 512 MiB of source bytes and 512 MiB of accounted decoded/working allocations. Native per-page limits and 256-vertex/128-triangle clusters apply separately. Hashing, parse loops, graph ordering and focused queries support cancellation. Copies share immutable results; later source mutation cannot alter them. Caller source buffers and requested JSON catalog serialization are separate allocations. The catalog omits per-vertex/triangle arrays; focused accessors cannot silently clamp invalid indices.

The installed [Core-only consumer](../examples/nanite_geometry_consumer/README.md) loads an explicit data-only page manifest, protects all input endpoints and queries every selected cluster after discarding its input buffers. The sample caps vertex and triangle output at 200,000 each. `validate_nanite_geometry.py` additionally compares explicit actual decoder references and source API geometry, with protected input hashes. Page extraction must use the matching resource's real native boundary, page states and whole verified bulk source; scanning for magic values does not establish that boundary.

## Independent evidence

The portable original numerical fixture contains 53 cases evaluated by the matching actual Engine CPU strip decoder, compiled unchanged in a private standalone harness with an integer/POD shim. It covers left/right strips, new/reference heads, start reference counts, 32-triangle boundaries and four-range material data. It records the actual selected function fingerprint and Engine build; no Engine function source or asset data is distributed in this fixture. Unit checks also exercise every prefix, malformed offsets/counts/maps, local versus parent identities, cyclic/out-of-order/long reversed graphs, immutable ownership, cancellation and output/read/count budgets.

Two independently cooked meshes supply eight pages, 536 clusters, 45,316 stored cluster vertices and 67,450 hierarchy triangles. All oriented indices match the actual Engine CPU strip decoder. Through an installed Core-only consumer, 32,816 full-leaf quantized oriented triangle positions match actual source MeshDescription API exports exactly. Another 135,948 coordinate queries check native integer-to-float consistency across all hierarchy clusters. This covers a seven-page, 535-cluster procedural grid with six streamed pages and a resident one-page mesh. Original asset/companion/container/context/mapping/reference/page inputs remain hash-protected and private.

The optional original Unreal adapter's `nanite-source` mode exports source positions, source vertex/triangle identities and quantized positions using the actual resource precision. It does not substitute fallback render geometry for Nanite source geometry, save an asset or become a Core runtime dependency. Source correspondence in this validator is by oriented quantized triangle positions, independent of native cluster order and attribute seam splits. It does not prove vertex-attribute correspondence.

The selected APIs do not imply a universal Nanite version reader, complete native StaticMesh extraction, runtime streaming/LOD evaluation, final UI integration, a new distributable release or overall product completion.

## Whole cooked StaticMesh binding

`CookedMeshOptions::nanite.profile = NaniteProfile::unreal_5_3_windows` opts the selected whole StaticMesh reader into `Nanite::FResources` immediately after the render LOD prefix. The default preserves the earlier opaque-tail contract. This requires the explicit mesh profile, producer mobile-LOD boolean, matching Zen mapping and global script objects described in [COOKED_MESHES.md](COOKED_MESHES.md). It does not scan for page magic or launch Unreal.

```cpp
cyuediff::CookedMeshOptions options;
options.profile = cyuediff::CookedMeshProfile::unreal_5_3_windows;
options.keep_mobile_min_lod_setting = false; // explicit fact from this producer
options.geometry = false;                  // skip fallback-LOD geometry allocation
options.nanite.profile = cyuediff::NaniteProfile::unreal_5_3_windows;
auto mesh = cyuediff::read_zen_static_mesh(container, entry_index, zen_context,
                                         container_reads, options);
auto information = mesh.nanite->information();
auto hierarchy = mesh.nanite->hierarchy_node(0); // only if present
if (auto geometry = mesh.nanite->geometry()) {
    auto vertex = geometry->position(0, 0, 0);
}
```

The reader consumes strip/resource flags, streamable bulk binding, resident root bytes, all page states, packed four-child hierarchy nodes, root offsets, dependencies, the opaque imposter atlas and serialized precision/input/cluster facts. Every root/stream page window must exactly partition its storage. Page dependencies are bounded, unique and acyclic even when payloads are unavailable. Root-offset segments and internal node references form checked forests; finite active bounds, fixup targets and leaf groups are validated. With complete payloads, stored leaf groups must cover each cluster exactly once. Focused hierarchy queries expose stored bounds/errors and fixup targets; these are pre-installation resource facts, not a simulated GPU hierarchy.

The same verified `ZenBulkReader` serves render LODs and Nanite. A shared external chunk is read and counted once, with mandatory stored whole-chunk integrity before publication. Ordinary, optional and memory-mapped storage retain their actual identities. Missing or unused stream payloads retain page/hierarchy metadata with `geometryKnown: false`, `status: unavailable` and no invented geometry. `empty` is a verified serialized empty resource with known absence. Server-stripped resources remain `stripped` and unknown. A null `geometry()` can therefore mean known empty or unknown; inspect `geometry_known()` and the status before interpreting it.

`information()` omits large page/cluster tables. `catalog()` includes them but omits per-vertex arrays. `hierarchy_node()` and geometry accessors check exact indices and cancellation. Resource defaults additionally cap 65,536 hierarchy nodes, four million dependency entries and four million imposter values. Native/decoded working allocations share the Nanite output budget and the enclosing mesh aggregate; full used container chunks share the enclosing mesh read budget. The decoder separately limits copied page source bytes. Caller JSON, container buffers and retained decoded objects remain distinct allocations. The later distance-field/object tail remains an offset/size/SHA-256 opaque window.

## Surface differences and CLI

`compare_nanite_surfaces(before, after, options)` compares exact object-local native float32 positions, oriented full-leaf triangles, triangle multiplicity and numeric material indices. Cyclic rotation of triangle vertices is equivalent; reversed winding is a change. Vertex IDs, cluster order and quantization encodings need not match. Negative zero is normalized. This deliberately does not match approximately moved vertices or certify equal normals, tangents, colors, UVs, material-slot names or runtime camera-dependent LOD cuts.

The result separates `encodingStatus` from `surfaceStatus`. Encoding uses the actual native resource and needed stream-window fingerprints; a quantization-only change may preserve the surface. `removed`/`added` contain stable native page/cluster/triangle references for highlighting. A verified empty peer can prove removal. Any unknown peer produces an incomplete comparison with no fabricated added/removed triangles. Defaults bound four million aggregate triangles and 256 MiB of accounted comparison allocations; cancellation applies during traversal and matching.

```sh
cyuediff zen-nanite BUILD.utoc ID --cooked-profile ue5.3 --schema CONTEXT.json --script-objects global.utoc --mesh-profile ue5.3-windows --keep-mobile-min-lod 0 --nanite-profile ue5.3-windows
cyuediff zen-nanite-cluster BUILD.utoc ID --page 1 --cluster 0 [same explicit context flags]
cyuediff zen-nanite-diff BEFORE.utoc ID AFTER.utoc ID [same explicit context flags]
```

Comparisons accept independent `--after-schema`, `--after-script-objects`, `--after-cooked-profile`, `--after-export-index`, `--after-mesh-profile`, `--after-keep-mobile-min-lod` and `--after-nanite-profile`. The selected profiles currently have one supported value each. Inspecting a known unavailable resource returns its metadata; focused unavailable/empty cluster queries fail. Difference exit codes are 0 for identical supported surface and encoding, 1 for a supported change and 2 for incomplete/unavailable/error. Output guards protect both sides' containers/partitions, contexts and mappings. `zen-mesh --nanite-profile ue5.3-windows` includes the opt-in catalog; legacy LOD commands retain their original meaning.

## Whole-package installed SDK evidence

The installed [cooked consumer](../examples/cooked_nanite_consumer/README.md) links only `CyUEDiff::Core` and starts at complete IoStore packages. The installed validator accepts `cyuediff.cooked-nanite-validation/1` in addition to the original page-set schema. Whole cases explicitly supply absolute `container`, `entry`, `global`, `context`, `packageReference`, `pages`, `triangleReference`, `sourceReference`, producer `mobileMinLod` boolean and an optional separately provided `codec`. `protectedSources` retains all source, companion, container-partition and mapping hashes to be checked before/after.

Both real whole-package cases reproduce the same independent 67,450 oriented CPU-decoder triangles and 32,816 actual source-API full-leaf triangles, with identical complete page hashes/order/dependencies and native source-package identity. All 135,948 native integer/float coordinates agree and 39 protected inputs remain unchanged. This evidence covers the seven-page streamed grid and one-page resident mesh, not arbitrary Nanite producers or attributes. Original fixtures additionally cover inline/external/optional/mapped/shared-chunk storage, known-empty versus unknown data, every native-window prefix, damaged hierarchy/fixups/counts/offsets, bounded allocation/read limits, cancellation and independent-context CLI differences.

The selected producer also permits a zero-bit normal channel. The position/topology reader accepts its serialized 0..15 normal-precision fact; it does not divide by a zero normal mask or invent decoded normals. An original zero-bit-normal resource retains the same inspected surface while its attribute encoding differs. Attribute decoding will need to retain this explicit unavailable/degenerate channel case.
