# Installed Core effective-default consumer

This independently configured example links only the installed `CyUEDiff::Core` target. It reads an explicit [prepared native default context](../../docs/EFFECTIVE_DEFAULTS.md), resolves one matching root export and optionally compares two effective property states. It does not load an Unreal module or discover/execute an Engine installation.

```sh
cmake -S path/to/installed/share/CyUEDiff/examples/effective_defaults_consumer -B consumer-build -DCMAKE_PREFIX_PATH=path/to/installed
cmake --build consumer-build --config Release
effective_defaults_consumer context.json before.uasset
effective_defaults_consumer context.json before.uasset after.uasset
```

The context is pinned and verified when opened, then reused for the two reads. Name component collection is mandatory for this workflow. Stored packages remain unchanged. Exit codes: 0 complete inspection or identical effective properties, 1 changed effective properties, 2 unavailable/rejected inputs. The example intentionally requires one root export; the full Core function and CLI expose explicit selection and independent before/after contexts.

Initial prepared profiles cover the documented UE5.3.2 Windows native/source workflow. Blueprint/archetype/subobject contexts, cooked effective defaults, arbitrary native equality, runtime post-load behavior and desktop context selection remain separate work. An inspection scope being complete does not mean the whole asset format has been decoded.
