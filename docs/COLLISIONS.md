# Stored collision inspection

The projection reads reflected properties already decoded by the package reader. It does not execute Unreal or physics code. No engine-owned source or asset data is redistributed.

## Simple reflected shapes - 0.73

The Core exposes bounded BodySetup/SkeletalBodySetup snapshots for stored spheres, boxes and capsules. Center/rotation, full box dimensions and capsule segment length retain their serialized meaning. Missing geometry is unresolved rather than defaulted. Opaque aggregate properties and native fingerprints remain explicit. Limits cover bodies, shapes, copied nodes/depth and text, with cooperative cancellation. No collision simulation, Chaos bulk decode or visual geometry is implied.
