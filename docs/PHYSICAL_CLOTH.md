# Selected physical cloth data

Source version 0.184 adds an explicit UE 5.3 Windows physical-cloth projection to the independent Core and CLI. It consumes qualified properties and certified native tails from a matching version-four Zen schema context. It never starts Unreal while reading a container. The separate original reference adapter runs only to prepare independent validation evidence.

## Stored model and geometry

`read_physical_cloth` selects one exact `ClothingAssetCommon` instance or an explicit export index. The owned model retains its object/GUID, independent package/context/script fingerprints, LOD and bone maps, physical positions/normals/oriented triangle indices, inverse masses, twelve-slot bone records, self-collision indices, stored weight-map entries, Euclidean/geodesic tether batches and serialized parameters. It projects available complete position/normal/index arrays into the existing immutable `MeshGeometry`, with explicit unavailable UV/tangent/color channels and source-space bounds. No component/world transform, dynamics, collision response or normalization is inferred.

Only selected stored Chaos configuration fields currently have dedicated typed projection: damping, gravity, low/high edge stiffness and shared iteration count. Complete generic mapped configuration properties remain in the existing property inspector/Zen output. Missing defaults stay unavailable. A decoded class is not proof that its constructor defaults are known.

The Core's map values are canonical `stored-map-delta` objects. The model retains replacement/removal metadata and explicit entries. Unseen default entries are not reconstructed. Comparisons of nonreplacement maps mask unknown entry absence and report incomplete coverage; an absent peer or missing optional parameter never becomes a fabricated removed value. Inventory certified by an explicit replacement can be compared normally.

## Transitions and bindings

Modern native transition records are interpreted as three float4 coefficient vectors, four uint16 source indices, a float32 weight and uint32 alignment padding. Exact raw values/padding remain queryable; semantic changes omit padding. The record type is shared with existing render-cloth mappings, but its use differs: the selected Chaos CPU LOD transition consumes the position coefficients/source indices directly and does not use the renderer's fourth-index contribution flag or stored weight. The Core preserves that distinction.

Up transitions target the current physical LOD from its higher-detail neighbor; down transitions use its lower-detail neighbor. Present records must partition into one record per target vertex and their source indices must fit the adjacent physical mesh. Unused boundary records remain explicitly unbound. Active nonfinite coefficients fail; unused nonfinite words retain their exact bit representation and do not become usable coordinates.

`wrap_transition` applies the selected Chaos 5.3 reference-pose transition algebra to the stored source positions and normals. This is a geometric correspondence query, not a simulation. Missing sources/maps/normals and nonfinite outputs fail explicitly. Independent actual Engine CPU references validate the operation; platform/compiler float32 association can change its last bits, while the stored coefficients and native encodings are checked separately.

`verify_cloth_source_bounds` certifies the base render mapping's GUID/physical-LOD identity and active source-vertex limits. Renderer-inactive records do not dereference physical indices. A biased mapping can use another source asset; the API requires an explicit source and does not infer `asset_lod + bias`. It reports index bounds separately from unavailable biased-source identity. This does not certify runtime deformation or an arbitrary cross-asset bias binding.

## Public paths

The API is in `physical_cloth.hpp`; the [installed Core-only consumer](../examples/physical_cloth_consumer/) reads independent members/contexts, focused resources, transition positions, named differences and optional explicitly profiled native render bindings. Representative CLI commands are:

```sh
cyuediff zen-physical-cloth input.utoc MEMBER --cooked-profile ue5.3 --script-objects global.utoc --schema context.json --physical-cloth-profile ue5.3-windows --include-values 1
cyuediff zen-physical-cloth-vertex input.utoc MEMBER [same context] --lod 0 --vertex 1
cyuediff zen-physical-cloth-transition input.utoc MEMBER [same context] --lod 1 --transition up --vertex 1
cyuediff zen-physical-cloth-wrap input.utoc MEMBER [same context] --lod 1 --transition up
cyuediff zen-export-physical-cloth input.utoc MEMBER [same context] --lod 0 --out physical.obj
cyuediff zen-physical-cloth-diff before.utoc MEMBER after.utoc MEMBER [independent contexts]
```

The comparison command explicitly selects both package members and supports separate profile/script/schema/export choices. Exit status is 0 for a complete identical comparison, 1 for complete named changes and 2 for incomplete/unavailable scope or an error. Full inspection returns 0 for an available stored model without claiming resolved defaults. Standard protected-output aliases apply to JSON/OBJ exports.

Default aggregate budgets bound LODs, vertices/indices, tethers/transitions, model bytes, text, input work, report values and comparison changes/bytes. Cooperative cancellation propagates; damaged known data fails before returning a usable partial model. Focused queries remain useful when full reports are intentionally limited. The existing paged EN/FR inspector continues to expose the generic stored properties; dedicated physical geometry/transition desktop binding remains subsequent work. No new archive or public release is implied.


## Independent validation

The installed Core-only consumer agrees with actual Unreal 5.3.2 references across four before/after cases and eight physical LODs: 12,788 vertices, 24,520 triangles, 26,748 tethers and 12,788 transitions. Stored data/native bytes are checked separately from 38,364 float32 wrap-coordinate components and 7,892 render-binding records. The original validator rejects altered projections and records unavailable defaults explicitly. See [VALIDATION.md](VALIDATION.md#typed-physical-cloth--0184-development) for scope and limits. The optional Engine adapter prepares evidence; it is not a dependency of the installed reader.
