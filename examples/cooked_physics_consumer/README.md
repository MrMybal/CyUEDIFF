# Installed Core cooked physics consumer

This example links only `CyUEDiff::Core`. It reads explicit UE 5.3 Windows Chaos
cooker data from BodySetup exports, with matching schema/global-script contexts.
Unreal Engine, Git and a graphics runtime are not linked or launched.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/installed/sdk
cmake --build build --config Release
cooked_physics_consumer INPUT.utoc ENTRY GLOBAL.utoc SCHEMA.json [CODEC]
```

The result contains native convex/triangle geometry, mass parameters, topology,
BVH data, UVs and face/vertex maps, plus a prepared `review` model/wireframe. Status 2 denotes a missing format/chunk or an
error. Every used container chunk is integrity checked. A platform format label
such as `PhysXPC` is retained as metadata; the explicit profile selects the reader.
No defaults, simulation, owner transforms or arbitrary plugin serializers are inferred.
