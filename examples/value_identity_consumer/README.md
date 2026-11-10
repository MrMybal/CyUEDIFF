# Typed name identity Core consumer

Reads a loose source asset and prints its ordinary semantic snapshot alongside opt-in typed FName observations. JSON Pointer paths address values in `snapshot`, while `base` and `number` retain the actual name components even when display strings coincide. The consumer links only the installed Core.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/CyUEDiff
cmake --build build --config Release
./build/Release/value_identity_consumer asset.uasset
```

Match the SDK's C++ runtime. On Windows a static MSVC SDK requires `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`. Exit 0 means the selected identity journal completed, 1 means its metadata limit was reached, and 2 is a read/argument error. A complete journal does not certify complete asset coverage, name/key equality or default reconstruction. See [the contract](../../docs/VALUE_IDENTITIES.md).

From0.201 the output also includes the functional `nameValues` index, independent of optional observations. Pass a second source asset to run the ordinary Core comparison: `value_identity_consumer Before.uasset After.uasset`. Same-display replacements include `beforeName`/`afterName`; exit2 retains known changes beside unavailable scopes. Both modes link only the installed Core and support native Windows Unicode paths.
