# Cooked collision Core consumer

Build from an installed Core SDK with `cmake -S . -B build -DCMAKE_PREFIX_PATH=SDK`, then `cmake --build build --config Release`. On MSVC, select the runtime matching that SDK. Only CyUEDiff::Core is linked.

```text
cooked_collision_consumer INPUT.utoc PACKAGE_ID GLOBAL.utoc SCHEMA.json [CODEC]
```

This example explicitly selects the validated UE 5.3 Zen context. It returns mapped stored collision shapes, provenance and body-local prepared wireframes. Supply matching reflected/native struct bindings, global script objects and an explicit compatible decoder when needed. Native physics remains opaque and missing archetype defaults are not reconstructed. No Editor, Git or graphics runtime is used.
