# Stored collision inspection

The projection reads reflected properties already decoded by the package reader. It does not execute Unreal or physics code. No engine-owned source or asset data is redistributed.

## Simple reflected shapes - 0.73

The Core exposes bounded BodySetup/SkeletalBodySetup snapshots for stored spheres, boxes and capsules. Center/rotation, full box dimensions and capsule segment length retain their serialized meaning. Missing geometry is unresolved rather than defaulted. Opaque aggregate properties and native fingerprints remain explicit. Limits cover bodies, shapes, copied nodes/depth and text, with cooperative cancellation. No collision simulation, Chaos bulk decode or visual geometry is implied.

## Convex source data and ownership - 0.74

Stored tapered capsules retain both radii and segment length. Convex elements expose finite source vertices, validated stored triangle indices and explicit transforms, with independent vertex/index budgets. Missing topology is preserved unresolved; no hull is reconstructed. StaticMesh BodySetup and PhysicsAsset SkeletalBodySetups references resolve within the package, with external/missing references explicit. Bone names and other body properties retain their stored values. Native level-set/Chaos data remains opaque.

## Stable collision comparisons - 0.75

The collision-diff projection matches unique exact stored shape names within their kind. Reordering those shapes changes their inspection sourceIndex but does not produce a collision geometry change. Anonymous/duplicate names remain explicitly index-based and order-sensitive. Radius/dimension/transform/topology changes, opaque fingerprints and local body references use escaped JSON-pointer paths. Incomplete projections report incomplete rather than equality. General package comparisons expose collisionChanges, and inspection exposes a collisions section.

## Collision CLI and tagged package integration - 0.76

collision ASSET emits the stored collision model; collision-diff BEFORE AFTER emits stable named-shape changes (exit 0 identical, 1 changed, 2 incomplete/error). Shared project/engine contexts and input/output protection apply. Independent classic UE 5.3/5.5-style tagged fixtures exercise all five supported shape kinds through the real package reader, including named reorderings, missing geometry, generic inspect/diff integration and source preservation.

CLI output protection also handles missing companion paths under Windows short-name temporary directories without the MSVC weakly_canonical failure. Output aliases through hard links are rejected. The regression exercises collision-diff, ordinary diff and material-diff; input and companion bytes remain unchanged.

## CyUI collision review and persistent state - 0.77

The separate CollisionView and desktop Collisions tab inspect body/shape unions, typed before/after geometry, settings, unresolved data and native fingerprints. Named shape navigation, body/shape selection and property filters/previews survive explicit workspace save/restore. Missing identities cannot silently select unrelated shapes. English/French and all five themes are covered. This milestone adds serialized-property review, not a physics solver or a 3D collision renderer.

## Usage

    cyuediff collision Content/SM_Example.uasset --out collision.json
    cyuediff collision-diff before/SM_Example.uasset after/SM_Example.uasset

The desktop --view collisions and Collisions tab inspect the union of bodies and shapes. All bodies and references includes owner associations; selecting a shape isolates its typed geometry/settings/extra data. The property filter and selected value preview remain in explicit workspace files. Unknown body/shape identities clear rather than selecting another shape.

The collision consumer example builds against the installed Core SDK. With one or two package paths it reads the actual assets; without arguments it demonstrates projection/diff on a small host-populated decoded model. Core has no CyUI or Git dependency.

Completeness describes the supported stored projection, never simulated collision behavior. Field defaults are not reconstructed. Indices are validated as stored triangles; no convex hull, skeleton pose, material collision, Chaos mesh or level-set data is generated. Positive dimensions, finite components and valid references do not certify a physically valid body. Explicit empty level-set arrays need no native element decoder.

Shape identity uses the exact stored name when unique within its kind, otherwise the collection index. Body identity uses its object path. The ordinary package diff retains serialized order changes while collisionChanges omits inspection-only sourceIndex changes for unique named shapes. Unsupported raw properties and native payload hashes remain visible.

Export outer containment is reported separately from bodyReferences. A child BodySetup can be discovered through its exact export outer while an owner reference stored in unread native data remains unavailable. This does not reconstruct that reference or resolve external assets.

## Bounded body-local primitive wireframes - 0.78

The Core exposes collision_wireframe and a dependency-free line/bounds model for stored spheres, boxes, capsules and tapered capsules. Box dimensions are halved before the stored Unreal pitch/yaw/roll rotation; capsule length excludes the end caps. Tapered previews sample the convex envelope of the two stored end spheres, including containing-sphere degeneracy. Sampling and edge budgets are explicit; missing geometry is diagnosed, and cancellation or coordinate overflow aborts without returning partial geometry. No bone, component, archetype or world placement is inferred. Convex preview is still unavailable in this milestone.

## Transformed convex source wireframes - 0.79

Convex previews deduplicate the edges of stored triangles and apply signed scale, a stored unit quaternion and translation in that order. Missing indices or transforms remain unavailable; no hull or identity transform is generated. Quaternions outside a 1e-4 unit-length tolerance are diagnosed rather than repaired; accepted rounding error is normalized for display. Per-shape bounds allow independent framing, while aggregate bounds remain body-local. Tests cover tetrahedral edges, negative scale, rotated bounds, missing topology/transforms, zero quaternions, cancellation and per-shape progress.

## CyUI before/after collision geometry - 0.80

The Collisions tab now draws prepared body-local wireframes beside the typed properties. Both sides share camera scale/orientation; stored shape changes color the geometry, and a changed-shapes filter and fit-changes action isolate differences. Body selection prevents overlaying unrelated skeleton/body coordinate systems. Missing geometry stays labeled rather than reconstructed. Geometry is prepared outside draw; English/French and five themes were checked with 140 UI frames and thirteen actual desktop captures. This preview is not a Chaos simulation.

## Collision picking and persistent orbit cameras - 0.81

Clicking a visible wire within six themed pixels selects its stored shape and synchronizes the typed inspector. Alt-orbit does not select a shape, and split panes apply navigation deltas only once. Perspective/front/right/top presets, changed-only filtering, target/up/orientation/distance survive explicit workspace files. Nested camera values are validated before any state mutation. Actual ImGui mouse-event tests exercise picking in the after pane and a single ten-pixel orbit delta; desktop save/reopen tests retain camera and property selection.
