# Nanite cluster geometry — post-0.181 development

`nanite_geometry.hpp` provides an immutable Core decoder for complete **UE 5.3 Windows stored Nanite pages**. It reconstructs quantized/float positions, oriented triangle indices and numeric material partitions across local and parent-page vertex references. It needs no Unreal, GPU, Git or UI runtime. This is a page-set decoder; the host must supply matching resource page order, root count, dependencies and flags from the actual producer context. It does not yet replace the opaque Nanite tail of the whole StaticMesh reader.

## Model and native meaning

Each input contains its complete fixup chunk and disk page after container decompression. Complete page SHA-256 remains provenance. The selected layout checks fixup/page/cluster counts, raw SOA packed clusters, GPU segment sizes/offsets, UV decode-table bounds, index streams and strip block prefix counts, cluster maps, split reference streams, vertex masks and byte-aligned position/attribute windows. Page/cluster fixup targets and dependency ranges are checked; a hierarchy node table and runtime installation are outside this primitive.

The independent sequential strip decoder retains triangle orientation. The GPU may rotate a triangle cyclically to put its minimum index first; that does not change its oriented surface. Repeated indices and index deltas outside the native five-bit constraint are refused. Inline three-material partitions and slow explicit material ranges must cover every triangle. Material values remain numeric resource indices, not guessed slot names.

Local references address a source cluster's **coded non-reference vertices**. Parent references address that dependency's fully reconstructed original vertex table. An iterative dependency graph establishes the required order without recursive stack growth or repeated page rescanning. Missing, duplicate, cyclic, out-of-range and undeclared relative references fail. Referenced absolute integer positions must fit the destination's stored quantization range. `quantized_position` retains exact int32 values; `position` exposes the selected native float32 scaling.

Every stored cluster remains available, including coarser hierarchy representations. `fullLeaf` identifies the stored full-leaf flag; it is not a runtime camera/error-dependent LOD selection. Combining all clusters as one surface would duplicate hierarchy levels. Normals, tangents, colors and UV **values remain opaque** in this milestone. Their windows remain checked and hashed; no placeholder attributes are claimed as decoded. Full attribute decoding, whole-package binding, semantic geometry differences and desktop projection remain required follow-up work.

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

The page-set API does not imply a universal Nanite version reader, complete native StaticMesh extraction, runtime streaming/LOD evaluation, final UI integration, a new distributable release or overall product completion.
