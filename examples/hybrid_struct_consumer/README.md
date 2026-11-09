# Hybrid struct consumer

This standalone C++17 example links only `CyUEDiff::Core` from one matching installed SDK. It reads explicit package members, global script objects and independent before/after schema contexts. Version-four contexts can bind a certified reflected property prefix followed by a declarative native tail. It does not launch Unreal, discover plugin serializers or load codecs.

```sh
cmake -S examples/hybrid_struct_consumer -B build-consumer -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build build-consumer --config Release
hybrid_struct_consumer INPUT.utoc ENTRY GLOBAL.utoc CONTEXT.json
hybrid_struct_consumer BEFORE.utoc ENTRY GLOBAL.utoc BEFORE.json AFTER.utoc ENTRY AFTER_GLOBAL.utoc AFTER.json
```

The JSON retains both complete package reports and optional named differences. Exit 2 indicates an error or a failed bound property projection; unbound exports and remaining native export suffixes keep their reported coverage. The selected containers must use supported unencrypted copy blocks. See [the hybrid structure contract](../../docs/HYBRID_STRUCTS.md) and supply matching explicit layouts/schema provenance. Do not mix headers and libraries from different SDK builds.
