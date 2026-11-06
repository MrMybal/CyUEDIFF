# MeshDescription source morph targets

Version 0.181 projects stored modern MeshDescription morph attributes into the reusable Core. `Morph-<name>` position deltas belong to sparse vertex IDs; optional normal deltas belong to vertex-instance IDs, so split normals remain distinct. This is source-data inspection and comparison. It does not decode cooked morph buffers, apply blend weights, deform the displayed geometry, evaluate skinning or play an animation.

## Core API

```cpp
#include <cyuediff/core.hpp>
#include <cyuediff/mesh_morphs.hpp>

const auto package = cyuediff::Package::open("Face.uasset");
cyuediff::MeshDecodeOptions options;
// options.virtualized_payload_root = "D:/PayloadStore";
const auto mesh = cyuediff::decode_mesh(package.mesh_source("", 0), options);
if (mesh.morphs) {
    for (const auto &[name, target] : mesh.morphs->targets) {
        // target.positions: live vertex ID -> exact stored FVector3f delta.
        // target.normals: live vertex-instance ID -> stored normal delta.
        // target.normals_present distinguishes absent and present-empty attributes.
    }
    const auto report = mesh.morphs->to_json();
}
```

`MeshGeometry::morphs` is an optional immutable `MeshMorphs` snapshot. Include the separate public header when accessing typed `MeshMorphTarget` maps. `MeshMorphs::validate(mesh, control)` checks names, finite values, complete live ID coverage and instance-to-vertex references, with cooperative cancellation. The decoder shares the existing bounded element/attribute budgets and verified local or explicit virtualized payload backend. The installed `examples/mesh_morph_consumer` requires only `CyUEDiff::Core`.

Positions register a target. A corresponding normal attribute is optional; normal-only targets are rejected. Each recognized attribute requires one fixed single-element FVector3f channel. Empty names, `None`, embedded NULs, ASCII case-colliding target names, duplicate normal attributes, invalid layouts and non-finite live deltas are errors. IDs and original spelling are retained; normal names bind using ASCII case-insensitive target keys. Broader Unicode case equivalence is not inferred. Stored zero vectors remain present. Inactive storage never creates a semantic vertex, and its bytes still affect raw attribute fingerprints. A present normal attribute with no live instances serializes as an empty object; an absent attribute serializes as null.

## CLI and JSON differences

```powershell
cyuediff geometry Face.uasset --out geometry.json
cyuediff mesh-diff Before.uasset After.uasset --out morph-changes.json
cyuediff geometry Face.uasset --payload-root D:/PayloadStore
```

The existing geometry and mesh-diff commands include this data automatically. Geometry JSON adds `morphs`, using `cyuediff.mesh-morphs/1`, only when at least one recognized position attribute is present. `targets` maps names to `positions` and `normals` tables. A change such as `/morphs/targets/Smile/positions/0/0` identifies the target, live source vertex and X component; `/morphs/targets/Smile/normals/1/1` identifies a vertex instance and Y component. Unchanged geometry without morph attributes retains its previous JSON shape. Target inventory changes remain visible even when every stored delta is zero. Raw storage changes remain independently fingerprinted.

OBJ export continues to export the original base geometry. OBJ cannot carry these named morph tables. Output protection and independent before/after payload roots follow the existing geometry command contracts.

## CyUI and desktop

The English/French Meshes tab adds **Inspect morph targets**. The typed table supports name/path filtering, changed-only rows and selected before/after values. Explicit workspace state retains `asset.meshMorphInspection` and `asset.meshMorphs` using the existing property-view state contract. Skin and morph inspection share the tab; enabling either inspection mode selects it. Older workspace files omit the new optional fields.

The base wireframe highlights edges incident to vertices with changed position deltas or changed split-normal deltas. Added/removed targets and normal-attribute presence changes also participate, including stored zero values. Colors describe changed source data, rather than an evaluated deformed surface. A normal-only change uses the referenced instance's source vertex. The preparation step calculates these flags outside drawing; rendering performs no file reads. Native worker reports add `morphTargets` to decoded mesh summaries only when morph data exists.

## Validation and limits

Original encoded fixtures cover sparse IDs, signed and zero vectors, separate instance identities, absent/present-empty normals, duplicate names, missing position attributes, multiple channels, invalid references, non-finite values, every truncated byte prefix, budgets, cancellation and verified virtualized input. CLI, installed Core-only consumers, native worker/workspace restoration and five-theme English/French rendering exercise the public path. These fixtures are original test data, with no Unreal source or assets redistributed.

The implementation follows the host UE 5.5 `SkeletalMeshAttributes` registration contract inside the already supported MeshDescription wire profiles. An independently produced real modern morph-source corpus remains unavailable. Existing real static geometry is checked for regression; this does not certify cooked/legacy morph formats. Legacy skeletal import/render buffers, cooked morph buffers, morph compression, normals recomputation, blend-weight application and runtime evaluation remain separate work.
