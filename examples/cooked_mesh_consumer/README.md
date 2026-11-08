# Cooked static mesh Core consumer

Build from an installed Core SDK with `cmake -S . -B build -DCMAKE_PREFIX_PATH=SDK`, then `cmake --build build --config Release`. On MSVC, use the runtime setting matching that SDK. This target links only CyUEDiff::Core.

```text
cooked_mesh_consumer INPUT.utoc PACKAGE_ID GLOBAL.utoc SCHEMA.json MOBILE_MIN_LOD_0_OR_1 [CODEC]
```

The selected native profile is explicitly UE 5.3 Windows. Supply the producer KeepMobileMinLODSettingOnDesktop value as 0 or 1, a matching property/schema context and matching global script objects. Optional CODEC is a host-supplied compatible decoder path. The program returns render LOD geometry and native/bulk/tail provenance on stdout. It does not write input files, execute the Editor or interpret the opaque Nanite/physics tail. See the installed docs/COOKED_MESHES.md for limits and supported layouts.
