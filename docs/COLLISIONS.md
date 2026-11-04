# Stored collision inspection

The projection reads reflected properties already decoded by the package reader. It does not execute Unreal or physics code. No engine-owned source or asset data is redistributed.

## Simple reflected shapes - 0.73

The Core exposes bounded BodySetup/SkeletalBodySetup snapshots for stored spheres, boxes and capsules. Center/rotation, full box dimensions and capsule segment length retain their serialized meaning. Missing geometry is unresolved rather than defaulted. Opaque aggregate properties and native fingerprints remain explicit. Limits cover bodies, shapes, copied nodes/depth and text, with cooperative cancellation. No collision simulation, Chaos bulk decode or visual geometry is implied.

## Convex source data and ownership - 0.74

Stored tapered capsules retain both radii and segment length. Convex elements expose finite source vertices, validated stored triangle indices and explicit transforms, with independent vertex/index budgets. Missing topology is preserved unresolved; no hull is reconstructed. StaticMesh BodySetup and PhysicsAsset SkeletalBodySetups references resolve within the package, with external/missing references explicit. Bone names and other body properties retain their stored values. Native level-set/Chaos data remains opaque.

## Stable collision comparisons - 0.75

The collision-diff projection matches unique exact stored shape names within their kind. Reordering those shapes changes their inspection sourceIndex but does not produce a collision geometry change. Anonymous/duplicate names remain explicitly index-based and order-sensitive. Radius/dimension/transform/topology changes, opaque fingerprints and local body references use escaped JSON-pointer paths. Incomplete projections report incomplete rather than equality. General package comparisons expose collisionChanges, and inspection exposes a collisions section.
