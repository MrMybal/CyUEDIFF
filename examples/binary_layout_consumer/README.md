# Installed binary layout consumer

This example uses only the installed `CyUEDiff::Core` target. It has no Unreal, Git, CyUI or source-checkout include dependency.

```powershell
cmake -S PATH/TO/EXAMPLE -B build-consumer -DCMAKE_PREFIX_PATH=PATH/TO/SDK
cmake --build build-consumer --config Release
build-consumer/Release/binary_layout_consumer.exe
```

With no arguments it checks a nested layout and explicit Windows-1252 conversion using original bytes. Additional modes read bounded host inputs:

```text
encoding NAME RAW_FILE
raw LAYOUTS_JSON RAW_FILE
package LAYOUTS_JSON ASSET [AFTER_LAYOUTS_JSON AFTER_ASSET]
cooked CONTEXT_JSON TOC ENTRY_ID GLOBAL_TOC
```

The `raw` mode selects the named `Record` layout. `package` returns snapshots and an optional comparison; `cooked` uses the context's explicit profile, USMAP and inline descriptions. Inputs are read-only. The example limits raw inputs to 16 MiB and uses the public reader defaults for packages/containers. It is a minimal host integration, not automatic schema or plugin serializer discovery.

The repository's `scripts/validate_binary_layouts.py` compares this independently built consumer with CLI outputs, a Python fixture decoder and Python's codepage implementations.


Source catalogues may select `prefix: "tagged-properties"` for certified struct decoders. The existing `package` form reads those prefixes and native tails, including independent before/after catalogues, without Engine/plugin runtime dependencies. See [hybrid structures](../../docs/HYBRID_STRUCTS.md) and the installed `validate_tagged_hybrids.py` reference validator. Missing legacy map struct identities and class defaults remain unavailable.
