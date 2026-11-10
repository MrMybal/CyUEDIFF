# Default snapshot Core consumer

Reads an explicitly prepared class-default capture using only the installed CyUEDiff Core. No Unreal executable, module or asset is opened by the consumer. See [capture preparation and limits](../../docs/DEFAULT_SNAPSHOTS.md).

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/installed/CyUEDiff
cmake --build build --config Release
```

On Windows, match the SDK's MSVC runtime; statically linked distribution SDKs require `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` at configuration.

For a capture produced by the tested exact UE 5.3.2 archive:

```sh
./build/Release/default_snapshot_consumer class-default.json "5.3.2-29314046+++UE5+Release-5.3" 522 1009 --text-ue5-history --native /Script/CoreUObject.IntPoint=int-point --native /Script/CoreUObject.Quat=quat-f64 --native /Script/CoreUObject.Vector=vector3-f64
```

Select `--filtered` only for an editor-filtered capture. `--sha256 <64 lowercase hex digits>` optionally checks the exact capture bytes. Native layout choices are explicit evidence-based bindings for this memory archive, not generic Engine name aliases.

Exit 0 means every captured field wire decoded; 1 retains unavailable fields; 2 rejects a capture or argument. JSON reports `binding: "unbound"` and `defaultsApplied: false`. Stored asset overrides, archetype/dependency binding, map/set equality and effective asset reconstruction remain separate work; this example does not apply default values to a package.
