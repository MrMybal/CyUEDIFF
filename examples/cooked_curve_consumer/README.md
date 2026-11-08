# Whole cooked scalar curve consumer

This installed SDK example links only `CyUEDiff::Core`. It reads complete UE 5.3 Windows IoStore AnimSequence packages and their exact referenced curve settings, resolves the actual mapped Codec registration, and returns named scalar values plus bounded key previews. It does not launch Unreal or require bone settings/Skeleton/interpolation.

```sh
cmake -S <prefix>/share/CyUEDiff/examples/cooked_curve_consumer -B build -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build --config Release
cooked_curve_consumer INPUT.utoc ANIMATION_ID GLOBAL.utoc CONTEXT.json CURVE_SETTINGS_ID TIMES.json OUTPUT.json
```

`TIMES.json` is an explicit nonempty array of at most 4,096 finite native float32 seconds. The example limits total scalar output and total key previews to 200,000 values/points. Its settings dependency is in the main container; the reusable Core API and CLI also accept separate dependency containers and contexts. Context/mapping/global/container/time inputs are protected output endpoints, including existing hard links. See [the reader contract](../../docs/COOKED_ANIMATIONS.md) in the source tree.

`validate_cooked_curves.py --consumer EXE --manifest MANIFEST.json --output NEW_DIRECTORY` validates independently exported actual UE 5.3 codec references. The manifest uses schema `cyuediff.cooked-curve-validation/1`, a `cases` array with absolute existing `container`, `entry`, `global`, `context`, `curveSettingsEntry`, `reference`, and optional `protectedSources` entries with `path`/`sha256`. Reference files come from the optional original Unreal adapter's `compressed-curves` mode. The validator checks exact settings/class/path, encoded names, complete buffer SHA-256/IoHash and actual scalar values; UniformlySampled batch references require measured identity runtime permutation. Original project assets and reference outputs remain private.
