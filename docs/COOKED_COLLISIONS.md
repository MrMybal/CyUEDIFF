# Mapped cooked collision shapes

The Core projects explicitly mapped cooked BodySetup properties into the same stored collision and body-local wire models as loose assets. It runs without Unreal, Git or a graphics device. This API projects reflected collision data. A separate native Chaos reader is described in [COOKED_PHYSICS.md](COOKED_PHYSICS.md); neither API runs a physics simulation.

## Context and class scope

Read a Zen package with matching global script objects and an explicit property schema context. Recognized non-template classes are `/Script/Engine.BodySetup` and `/Script/Engine.SkeletalBodySetup`; recognized owner records are StaticMesh and PhysicsAsset. Arbitrary derived/plugin collision classes are not inferred from their names. Unsupported or absent bindings retain unavailable bodies rather than empty replacement geometry.

The selected real evidence is UE 5.3. A matching schema must describe inherited slots and the actual struct serializers. Relevant reflected types include AggregateGeom, the selected shape elements, BodyInstance and its core base. In the checked engine, the aliased `/Script/CoreUObject.Transform` is **reflected**, with native double Quat/Vector children. Its C++ archive operator is not evidence of a native property serializer. Box uses its checked native layout. Additional nested types require their own matching binding; there is no automatic layout guessing.

An abbreviated context fragment illustrates those distinctions:

```json
{
  "bindings": {
    "/Script/Engine.BodySetup": {"schema": "/Script/Engine.BodySetup", "offset": 0}
  },
  "reflectedStructs": [
    "/Script/Engine.KAggregateGeom", "/Script/Engine.KSphereElem",
    "/Script/Engine.KBoxElem", "/Script/Engine.KSphylElem",
    "/Script/Engine.KConvexElem", "/Script/Engine.KTaperedCapsuleElem",
    "/Script/Engine.BodyInstance", "/Script/PhysicsCore.BodyInstanceCore",
    "/Script/CoreUObject.Transform"
  ],
  "nativeStructs": {
    "/Script/CoreUObject.Vector": "vector3-f64",
    "/Script/CoreUObject.Quat": "quat-f64",
    "/Script/CoreUObject.Rotator": "rotator-f64",
    "/Script/CoreUObject.Box": "box3-f64"
  }
}
```

This is a fragment, not a complete manifest. The enclosing version-two context also requires the selected profile, USMAP path and exact mapping SHA-256. See SCHEMAS.md and UHT_SCHEMAS.md.

## Stored projection and unknowns

Qualified mapped property fields are normalized to their stored member names for the shared projector. Colliding names are refused rather than merged. Type metadata, native layout identity and stored values remain in the projection; package/schema/global-table fingerprints remain in provenance, with `mappingVerified: false` and `nativePhysicsDecoded: false`.

Supported stored shapes are spheres, boxes, capsules, tapered capsules and convex vertex/index data. Complete canonical or reflected transform channels are accepted. Omitted geometry channels, unknown collections, missing references and archetype defaults stay unresolved. Body-local wireframes preserve stored transforms and topology; no hull, bone/component/world placement or identity transform is synthesized.

`bodyTableKnown` records whether the input's recognized class table is available. A missing body or explicitly empty collection can therefore be distinguished from an unavailable collection. Viewports do not paint unreadable peer geometry as deleted or modified. Actual known body removal remains visible even when unrelated owner references are incomplete.

Native remainder byte counts begin at a successfully decoded property boundary. When properties are unavailable, the entire body is marked `opaque-cooked-body`. The retained `exportBodySha256` hashes the complete export body; it is **not** presented as a native-tail-only hash. Native StaticMesh body references are not reconstructed from outer containment. Level-set data and broader derived-class roles remain open. Native convex/triangle physics and Chaos acceleration structures are outside this reflected projection; the separate reader is described in COOKED_PHYSICS.md.

## Core, CLI and desktop

```cpp
auto package = cyuediff::read_zen_package(container, entry, zen, read);
auto model = cyuediff::collision_snapshot(package);
auto wire = cyuediff::collision_wireframe(package);
auto difference = cyuediff::compare_collisions(beforePackage, afterPackage);
```

The overloads reuse existing collision bounds, budgets and cancellation contracts. Normalization and projection share the remaining node/text budgets; body/shape/vertex/index/edge limits remain explicit. There is no file I/O in the projection or viewport drawing. Reading the original IoStore member still performs mandatory whole-chunk source-hash verification before a decoded package is available.

```text
cyuediff zen-collision BUILD.utoc PACKAGE_ID --cooked-profile ue5.3 --schema context.json --script-objects global.utoc
cyuediff zen-collision-wire BUILD.utoc PACKAGE_ID [same cooked context]
cyuediff zen-collision-diff BEFORE.utoc ID AFTER.utoc ID [same cooked context]
```

The bracketed text abbreviates the context options; it is not literal CLI syntax. Add `--codec MODULE` for a host-supplied container codec, and use independent `--after-cooked-profile`, `--after-schema` and `--after-script-objects` when needed. Comparison provenance is reported independently from interpreted property changes. Incomplete data cannot certify an identical complete comparison. Output paths cannot overwrite source containers, partitions or mapping/context inputs.

In CyUI, inspect the member using the matching schema and open **Cooked collisions** (**Collisions cooked** in French). The worker prepares snapshots and wireframes; the view supplies body/shape selection, synchronized before/after camera, colored stored changes, changed filtering and fitting. Workspace format seven retains container chapter 7 plus the checked collision selection, viewport and inspector state. Unavailable collections or geometry do not become false deleted-shape colors.

The installed `examples/cooked_collision_consumer` links only CyUEDiff::Core and returns stored models plus prepared wire output. `scripts/validate_cooked_collisions.py` checks CLI/installed-SDK parity, selected loose/cooked geometry and body-local lines, and source hashes before/after. Report destinations reject links that alias a corpus input.

## Evidence boundary

Original fixtures exercise actual Zen member/schema reads, scalar/struct geometry, exact context fingerprints, named differences, absent bindings/templates, invalid values, property boundaries, duplicate normalized fields, budgets, cancellation and unchanged inputs. CLI/native integration covers damaged source hashes, invalid mapped floats, unavailable geometry, protected writes, EN/FR review, camera/shape retention and a report hard-link guard.

The selected real corpus contains 84 BodySetup packages. Their 77 available stored shapes (72 convexes, three boxes and two spheres) match all 235 stored geometry fields and all 3,552 prepared wire edges against loose sources. A separately built installed Core-only consumer matches CLI projections and wires. Seven inputs have no available stored shape in this domain; this does not certify that their native physics data is empty. Source/companion/container/context/mapping/codec/tool hashes remain unchanged. Native physics, enum-name resolution and omitted defaults are outside this reflected comparison. The separately scoped native reader and remaining work are tracked in COOKED_PHYSICS.md and COMPLETION.md.
