# Native cooked Chaos physics

The Core reads the selected **UE 5.3 Windows** Chaos cooker streams without
launching or linking Unreal. This is a native physics-data reader, distinct from
[the mapped reflected collision projection](COOKED_COLLISIONS.md). It does not
run a simulation or rebuild omitted physics/default data.

## Explicit profile and boundaries

`read_zen_cooked_physics` requires matching Zen global script objects, a successful
explicit BodySetup property binding and `CookedPhysicsProfile::unreal_5_3_windows`.
It accepts exact non-template Engine BodySetup/SkeletalBodySetup exports in an
editor-stripped cooked package. Derived/plugin roles are not inferred from names.
The complete native export must match the selected profile; unexpected tails are
refused. The per-body object GUID, BodySetup GUID, cooked-data flag and named
format-container/bulk references remain visible.

A stored platform label such as `PhysXPC` is metadata, **not** proof that the
payload uses PhysX. The explicitly selected producer profile chooses this Chaos
reader. Other engine/platform/custom-version layouts require their own readers.

Inline bytes use the actual archive cursor. External ordinary, optional and
memory-mapped chunks use their checked bulk-map offsets, including duplicate
optional fallback. Every used complete chunk retains mandatory container source
integrity verification. Unused, empty and missing format payloads remain unavailable;
they do not become empty decoded geometry. A false has-data flag still has a
serialized format-count field.

## Decoded data

The stream begins with its float32 cooker precision marker. Nullable convex and
triangle-mesh arrays refer to graph tags; shared objects are decoded once, with
checked type identity. Sparse tags do not allocate tag-sized arrays.

Convex objects retain their planes, vertices, local bounds, volume, center of
mass, margin, half-edge faces/twins/vertex links/unique edge set, unit-mass inertia
and mass rotation. Small unsigned 8-bit, signed 16-bit and signed 32-bit topology
indices are supported. Stored empty edge lists remain empty; no hull is invented.
The selected Chaos vector archive overload stores float32 inertia components,
while mass quaternion components are float64.

Triangle meshes retain float positions, 16/32-bit triangles, bounds, BVH nodes and
face bounds, materials and external face/vertex maps. Hit-result UV indices,
float64 positions/UV channels and the cooker face-remap array are also read.
Coordinates are the stored cooker coordinates; no bone, component or world
placement is inferred.

Invalid booleans/counts, truncation, non-finite values, out-of-range geometry,
invalid convex twin/face topology, cyclic/unreachable BVH nodes and unexpected
trailing bytes are refused atomically. Read/output/object/reference/vertex/triangle/
topology/BVH/format/body limits apply across the operation, with cancellation in
reading, projection, verification and comparison loops.

## Core, CLI and SDK

```cpp
cyuediff::CookedPhysicsOptions options;
options.profile = cyuediff::CookedPhysicsProfile::unreal_5_3_windows;
auto data = cyuediff::read_zen_cooked_physics(container, entry, zen, read, options);
auto difference = cyuediff::compare_cooked_physics(beforeData, afterData, options);
// For a separately obtained, explicitly identified raw cooker stream:
auto raw = cyuediff::read_chaos_physics(bytes, options);
```

```text
cyuediff zen-physics BUILD.utoc PACKAGE_ID --physics-profile ue5.3-windows --cooked-profile ue5.3 --schema context.json --script-objects global.utoc
cyuediff zen-physics-diff BEFORE.utoc ID AFTER.utoc ID [same explicit context]
```

Bracketed text abbreviates flags. A host-supplied `--codec MODULE` can decompress
containers. The comparison accepts independent `--after-cooked-profile`,
`--after-schema`, `--after-script-objects` and `--after-physics-profile` overrides.
Exit codes are 0 for complete unchanged data, 1 for complete changed data and 2
for unavailable/incomplete data or an error. Inspection returns 0 for a complete
read. Output cannot overwrite protected container/context/mapping inputs.

Comparisons match exact body paths and format name/number pairs. They report
native field paths and before/after values, including geometry, mass and
acceleration data. Package/bulk hashes, offsets, GUIDs, raw graph tag numbers and
lossless index-width choices do not create native-data changes. Missing peer
payloads remain unresolved instead of falsely removed geometry. Neither a
successful decode nor an unchanged comparison certifies a caller's schema mapping;
its fingerprint and `mappingVerified: false` remain in provenance.

The installed `examples/cooked_physics_consumer` builds against `CyUEDiff::Core`
without Unreal, Git or graphics dependencies. Native physics currently has a
Core/CLI/SDK path. The desktop's **Cooked collisions** chapter still reviews the
separate reflected shape projection; native physics visualization remains open.

## Evidence and limits

Original fixtures cover graph sharing/nulls, all topology/index widths, UVs,
material/remap/BVH data, every truncated prefix, corrupt topology/floats, aggregate
budgets/cancellation and verified inline/ordinary/optional/mapped bulk storage.
CLI checks include a one-scalar native vertex change, equal data in different
storage, explicit profile/context failures and protected outputs.

`scripts/validate_cooked_physics.py` compares an independently implemented Python
wire reader, the CLI and an installed Core-only consumer. The selected real
corpus has 84 packages and 81 inline physics streams, containing 142 convex
objects and 72 triangle meshes: 42,771 native geometry vertices and 96,095
collision triangles. All native fields and stream/export end offsets agree.
Original assets, companions, containers, mappings, codec and tools remain unchanged.
The independent reader uses the supplied mapped property boundary; it does not
independently certify mappings or simulate the engine.

Real external physics storage, nonempty real hit-result UV data, skeletal runtime
physics behavior, other producer versions/platforms, custom implicit types, level
sets and plugin-native serializers still need their own evidence/implementation.
The installed/current-development reader does not replace the 0.181 archive or
imply that the overall product is finished or publicly released.
