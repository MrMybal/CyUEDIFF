# Nanite cluster geometry — post-0.181 development

`nanite_geometry.hpp` provides an immutable Core decoder for complete **UE 5.3 Windows stored Nanite pages**. It reconstructs quantized/float positions, oriented triangle indices and numeric material partitions across local and parent-page vertex references. It needs no Unreal, GPU, Git or UI runtime. The page-set API accepts explicit producer metadata. The opt-in whole StaticMesh path now binds that metadata and its stored pages directly from a verified cooked native boundary; both APIs expose the same immutable geometry.

## Model and native meaning

Each input contains its complete fixup chunk and disk page after container decompression. Complete page SHA-256 remains provenance. The selected layout checks fixup/page/cluster counts, raw SOA packed clusters, GPU segment sizes/offsets, UV decode-table bounds, index streams and strip block prefix counts, cluster maps, split reference streams, vertex masks and byte-aligned position/attribute windows. Page/cluster fixup targets and dependency ranges are checked; a hierarchy node table and runtime installation are outside this primitive.

The independent sequential strip decoder retains triangle orientation. The GPU may rotate a triangle cyclically to put its minimum index first; that does not change its oriented surface. Repeated indices and index deltas outside the native five-bit constraint are refused. Inline three-material partitions and slow explicit material ranges must cover every triangle. Material values remain numeric resource indices, not guessed slot names.

Local references address a source cluster's **coded non-reference vertices**. Parent references address that dependency's fully reconstructed original vertex table. An iterative dependency graph establishes the required order without recursive stack growth or repeated page rescanning. Missing, duplicate, cyclic, out-of-range and undeclared relative references fail. Referenced absolute integer positions must fit the destination's stored quantization range. `quantized_position` retains exact int32 values; `position` exposes the selected native float32 scaling.

Every stored cluster remains available, including coarser hierarchy representations. `fullLeaf` identifies the stored full-leaf flag; it is not a runtime camera/error-dependent LOD selection. Combining all clusters as one surface would duplicate hierarchy levels. Normals, optional stored tangents, RGBA colors and UV values now have explicit opt-in decoding, described below. The default continues to check/hash attribute windows without allocating decoded attributes. Runtime LOD selection and desktop projection remain follow-up work. The whole-package path and scoped full-leaf surface differences are described below.

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

`compare_nanite_surfaces(before, after, options)` compares exact object-local native float32 positions, oriented full-leaf triangles, triangle multiplicity and numeric material indices. Cyclic rotation of triangle vertices is equivalent; reversed winding is a change. Vertex IDs, cluster order and quantization encodings need not match. Negative zero is normalized. The default does not compare shading values. An explicit attribute comparison is available below. Approximate moved-vertex matching, material-slot names and runtime camera-dependent LOD cuts remain outside this scope.

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

The selected producer also permits a zero-bit normal channel. The reader accepts its serialized 0..15 precision fact. Explicit attribute queries retain its packed identity with `normal: null` and `normalStatus: zero-bit-unavailable`; a tangent requiring that normal also stays unavailable. Other stored channels can still decode. An original zero-bit-normal resource retains the same inspected surface while its attribute encoding differs.

## Opt-in shading attributes

Set `NaniteGeometryOptions::attributes = true` for a page set, or `CookedMeshOptions::nanite.attributes = true` for whole-package binding. `attributes_decoded()` and catalog/cluster `storedAttributesDecoded` identify whether encoded attribute reconstruction was requested and completed. The default remains false. A focused `attributes(page, cluster, vertex)` query returns `NaniteVertexAttributes`; a query without the opt-in fails explicitly.

The model owns reconstructed packed normals, optional tangent angle/sign, native RGBA8 colors and up to four packed UV channels. Focused values also retain normal/tangent precision, uncompressed UV integers and native float32 UV values. Nonzero normal precision yields octahedral unit normals. Stored tangents yield an angle-derived basis vector and handedness; absent tangents remain null, and zero-bit normals remain explicitly unavailable. Color values are native byte channels with no display color-space transform. White/constant modes use the serialized minimum; variable modes retain exact component deltas. No unstored tangent or UV channel is synthesized.

Coded attributes are byte-aligned on disk; reconstructed GPU attributes use the exact packed stride. Local references address raw coded attributes; parent references address fully transcoded original vertices. Destination color ranges and UV ranges can differ. UV reconstruction applies native float32 exponent scaling, nearest-even rounding, removed-gap midpoint selection, native unsigned arithmetic and destination bit-width clamping. Copying already-decoded UV floats between clusters would lose these semantics. Normal/tangent layouts, component widths, counts, strides, finite UV results and matching whole-resource normal precision are checked. Supported native UV exponents are -126 through 14; invalid/non-finite scales and integer conversions are refused. Normal precision is 0..15, tangent angle precision is 0..12, colors use 0..8 bits/component and UV components use 0..15 bits.

Attribute working arrays, raw coded references and immutable output share the decoder byte budget. Temporary coded arrays are released after iterative dependency reconstruction. Focused result vectors/JSON are caller allocations. Attribute values survive source-buffer disposal; cancellation applies during decode and focused access.

The CLI enables this path with `--nanite-attributes 1` on `zen-nanite`, `zen-nanite-cluster` or opt-in `zen-mesh`. Comparisons accept an independent `--after-nanite-attributes 0|1`. Focused cluster output includes `attributes` only when requested. `compare_nanite_surfaces` retains its position/topology/material scope by default. `NaniteSurfaceDiffOptions::attributes` explicitly adds common-surface shading comparison; decoding attributes alone does not expand it or claim full material/render equivalence.

Both installed consumers now request shading attributes and emit their focused values. The installed validator optionally accepts `attributeReference` per case, pointing to a `cyuediff.nanite-shader-attributes-reference/1` document with `profile`, exact `pageHashes` and page/cluster/vertex `pages`. Each vertex supplies `packedNormal`, nullable `packedTangent`, precisions, RGBA `color`, nullable decoded `normal`/`tangent`, `packedUVs` and `uvs`. It requires complete cardinality and stored-page identity, exact packed/color/UV correspondence and at most 3e-6 normal/tangent component error. Add all independently used shader/reference inputs to the protected hashes. No GPU or Unreal runtime is needed to run the consumer or compare an existing reference.

## Independent attribute evidence

An isolated private Direct3D 11 WARP harness compiles and dispatches the unchanged matching Unreal `TranscodeVertexAttributes`, UV unpacking, octahedral normal and tangent functions with the actual native bitstream include. Standalone buffer/field setup and shader-entry wrappers supply original/authorized data; no Core decoder is linked. Selected function and complete source SHA-256s are retained. WARP executes the actual compute shaders as a software device; this is not a claim of rendering through the full Unreal application.

On the same two real meshes, all 45,316 packed normals/colors and all 90,632 packed/decoded UV pairs agree with the actual shader. Native UV float values agree exactly; maximum normal component delta is 5.960464477539063e-8. Those meshes have no stored tangents, so real tangent coverage is not claimed. The whole-package and page-set installed consumers are checked against the explicit native shader outputs as well as the independent triangle/source references.

The portable original numeric fixture contains 76 independently dispatched shader cases and 255 vertices. Its 5,662 checks cover 0/1/2/5/8/10/15-bit normals, 0/1/3/7/12-bit tangent angles and both signs, absent tangents, white/constant/variable/max-byte colors, zero through four UV channels, zero/one/eight/fifteen-bit UV components, negative/positive UV exponents, local coded references, parent rescaling, positive/negative half-even ties, gap-midpoint/native wrap/clamp behavior and constant/variable color transitions, north/folded normal bases and 36 seeded original combinations of normal/tangent/color/UV bit layouts. It distributes original numeric page bytes and shader outputs, not Engine function source or assets. Additional checks exercise every original page prefix, malformed field widths/strides/exponents/colors, unavailable queries, ownership, byte budgets, cancellation and a 384-page reversed dependency chain. This evidence establishes the selected stored attribute path; runtime interpolation/material evaluation, broader producers, broader approximate/runtime correspondence and desktop integration remain open.

## Exact common-surface shading differences

Set `NaniteSurfaceDiffOptions::attributes = true` in addition to enabling attribute decoding on both readers. `compare_nanite_surfaces` then compares exact native decoded normals, optional tangents/handedness, RGBA colors and UV values on common full-leaf oriented surfaces. Packed normal/tangent codes, precision fields, color minima and UV gap encodings are not semantic value keys. A changed encoding can retain identical decoded values. Negative zero is normalized. No tolerance, renderer/material evaluation or arbitrary authored vertex identity is inferred.

Position/material keys retain oriented cyclic rotations and triangle multiplicity. Attribute corners are aligned to those rotations, including ties caused by coincident positions. Identical complete shading signatures match first within each geometric bucket; reordering overlapping triangles therefore does not create changes or select the wrong surviving exact shading identity. Remaining duplicate signatures pair in deterministic lexical order. Such residual pairs have `ambiguous: true` and contribute to `ambiguousAttributePairs`: the reported comparison is reproducible, while the original authored identity of an overlapping triangle remains unknown. It does not claim an optimal per-channel assignment.

`NaniteSurfaceComparison::modified` contains before/after native triangle references, changed and unavailable channel masks, canonical before/after corner rotations, and the ambiguity flag. The public `NaniteAttributeChannel` bits identify normals, tangents, colors and UVs. JSON exposes named `channels`/`unavailable` arrays and aligned `beforeCornerOrder`/`afterCornerOrder` indices into each native triangle. Separate summary fields report geometry completeness, encoding/surface/attribute statuses, per-channel changed/unavailable triangle counts and requested attribute completeness. `to_json(false)` omits focused references while retaining the summary. Counts are per matched triangle, so a normal change may also change the tangent derived from its basis.

Unrequested or zero-bit semantic channels cannot prove equality. Known color/UV changes are still returned when normals/tangents are unavailable; `complete` remains false and the overall status is `incomplete`. Attribute status can be `changed` for those known changes. Verified absent tangents compare as known absence. With no common surface, attribute status is `not-applicable`: added/removed triangles retain geometry references and no cross-surface attribute identity is invented. Missing geometry continues to yield no fabricated additions/removals. The `complete` flag always refers to the documented requested scope, not the entire StaticMesh asset or later opaque native data.

```cpp
cyuediff::NaniteSurfaceDiffOptions diff;
diff.attributes = true;
auto result = cyuediff::compare_nanite_surfaces(before, after, diff);
for (const auto &change : result.modified) {
    // change.before / change.after locate native triangles for highlighting.
    // change.changed and change.unavailable use NaniteAttributeChannel bits.
}
```

```sh
cyuediff zen-nanite-diff BEFORE.utoc ID AFTER.utoc ID [explicit matching context/profile flags] --compare-nanite-attributes 1
```

The explicit CLI comparison flag automatically requests attribute decoding; a per-side `--nanite-attributes 0` or `--after-nanite-attributes 0` can deliberately retain unknown coverage. The default remains position/topology/material comparison. Exit codes remain 0 identical in scope, 1 supported changes and 2 incomplete/error. Bounds cover aggregate triangles, geometric keys, per-bucket duplicate signatures, transient vector growth and output references. Duplicate matching uses ordered maps rather than quadratic candidate scans; cancellation applies during indexing, signature matching and focused queries.

The installed [comparison consumer](../examples/cooked_nanite_comparison_consumer/README.md) links only `CyUEDiff::Core`, reads two explicit complete resources with independent global/schema/mobile-LOD context and emits all scoped differences. Original whole-package tests cover each channel, cyclic/coincident-position correspondence, duplicate permutation/removal, residual ambiguity, color/UV repacking with identical values, mixed known/unknown channels, budgets and cancellation. The installed consumer additionally checks four original changed packages against newly dispatched actual shader outputs, incomplete requested coverage, real resident/streamed self-comparisons and a source-API-checked real geometric difference. Both source/dependency sets and hard-link aliases are protected. Dedicated Nanite desktop review remains in development.
