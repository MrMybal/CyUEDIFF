# Skeletal MeshDescription source data

Version 0.180 adds skeletal editor MeshDescription sources to the reusable Core. It reads `SkeletalMeshDescriptionBulkData`, resolves the selected SkeletalMesh source-model reference, and decodes source geometry, sparse bone IDs, parent links, stored poses and per-vertex skin weights. A package containing one source bulk object can also be opened directly. Local compressed buffers and the explicit filesystem virtualized store use the existing bounded bulk backend.

```cpp
#include <cyuediff/core.hpp>
#include <cyuediff/mesh_skin.hpp>

const auto package = cyuediff::Package::open("Mesh.uasset");
cyuediff::MeshDecodeOptions options;
options.virtualized_payload_root = "D:/PayloadStore"; // optional for embedded data
const auto mesh = cyuediff::decode_mesh(package.mesh_source("", 0), options);
if (mesh.skin) {
    for (const auto& bone : mesh.skin->bones) {
        // Original sparse ID, name, parent ID, quaternion, translation and scale.
    }
    const auto& weights = mesh.skin->profiles.at("Default");
    const auto report = mesh.skin->to_json();
}
```

`MeshGeometry::skin` is an optional immutable shared snapshot. Include `<cyuediff/mesh_skin.hpp>` when accessing its typed `MeshBone`, `MeshInfluence` and `MeshSkin` data. Static geometry keeps the existing JSON shape when no source bone/weight attributes are present. The installed `examples/mesh_skin_consumer` builds with `CyUEDiff::Core` alone.

Source `SkinWeights` and `SkinWeights-<profile>` attributes are unbounded int32 arrays. Each packed value stores a 16-bit bone ID and an exact 16-bit raw weight. The reader retains source order and raw values without rescaling, sorting or inventing influences; divide by 65535 when a fractional display value is desired. Inactive vertex and bone slots stay excluded from the projection. Multiple 256-element storage chunks and spare capacities are checked against their declared ranges and budgets. Empty live-vertex influence lists remain visible.

`BonesElementName` supplies the bone names, parent indices and type-7 transform attributes. Parents may follow children and IDs may be sparse. Validation rejects missing parents, cycles, duplicate names/IDs/influences, missing influence targets, non-finite poses and zero-length quaternions. Poses are retained as stored; non-unit quaternions and non-normalized weight sums are not silently changed. The public `MeshSkin::validate` supports cancellation. Names and transforms are also available when the source has a bone table but no weight profiles.

The modern MeshDescription backend accepts the checked UE5 Main 51–119, Editor 40 and UE5 Release 44–51 profiles. The added type-7 reader requires an explicit supported UE archive version: pre-LWC float components and LWC double components have separate layouts. A present Fortnite Main version below 133 selects the historical transform-array compatibility path, which accepts only empty bulk-serialized transform arrays and preserves their explicit size marker. It never copies a native in-memory FTransform layout. Unknown versions and unsupported arrays remain explicit errors. The on-disk SkeletalMesh source-model member is `MeshDescriptionBulkData`; source objects can be selected explicitly with `--object` when an owner does not provide a usable source-model reference.

```powershell
cyuediff geometry Mesh.uasset --out geometry.json
cyuediff mesh-diff Before.uasset After.uasset --out changes.json
cyuediff geometry Mesh.uasset --payload-root D:/PayloadStore --codec Codec.dll
```

Geometry JSON adds a `skin` object using `cyuediff.mesh-skin/1`. Bone entries are keyed by name, their parent is another name or null, and per-vertex/profile weight maps use bone names and raw integers. Typed data retains the original numeric IDs. Semantic weight paths such as `/skin/vertices/0/Default/arm` identify the affected vertex, profile and bone. Raw attribute fingerprints still report storage changes, including inactive slots and array order. Consequently a bone renumbering can leave named skin values unchanged while its raw attribute fingerprints differ. OBJ export contains geometry only; OBJ does not carry these source weights or poses.

The English/French Meshes view includes **Inspect source skinning**, a searchable property table with changed-only filtering, selected values and persisted inspection state. The wireframe highlights edges attached to vertices whose weights changed. A changed source bone pose or parent link also marks vertices influenced by that bone or its descendants. This is a source-data comparison; the preview does not evaluate an animation, apply bind matrices or display a runtime skinned pose.

Validation uses original bounded fixtures for modern source packages, native class routing, named comparisons, float/double transforms, chunk boundaries, corruption, cancellation, virtualized integrity, CLI, worker-prepared Desktop previews and workspace restart. The available real older SkeletalMesh has no modern source bulk object, so it remains explicitly unsupported for geometry; its stored object projection is unchanged. Real static source geometry was compared against 0.179 with no change. A matching independently produced modern SkeletalMesh source corpus remains needed before claiming broad real-project coverage.

Legacy `FSkeletalMeshLODModel` import data, cooked render/skin buffers, morph-target decoding, CPU skinning, bind-pose deformation, animation playback and Nanite remain outside this backend. Reading the source MeshDescription does not implement those separate formats.
