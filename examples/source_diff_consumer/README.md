# Borrowed source comparison and streamed JSON

Build this independent C++17 consumer against the matching installed 0.191 Core SDK. It does not link CyUI, Git, Unreal or a plugin binary.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build build --config Release
source_diff_consumer before-layouts.json Before.uasset after-layouts.json After.uasset 2000000
```

Each source uses its explicitly selected type/native catalogue. `MAX_WORK` is optional and retains the ordinary finite source default when omitted. The result owns changed values after source inspection; `write_json` serializes its existing arrays directly. It emits the same report fields as `to_json()` without materializing another report tree or full serialized string. Stdout can contain a partial document if writing fails. CLI output-file handling additionally uses a flushed sibling file and atomic replacement.

Exit status follows the general source comparison contract: 0 identical within the stored projection contract, 1 known changes/binary-only, 2 error/incomplete/unverified. Opaque data, unresolved defaults and incompatible decoded types remain explicit; this example does not discover native serializers or reconstruct a running asset. See [the comparison contract](../../docs/SOURCE_AVAILABILITY.md) and [memory/report behavior](../../docs/COMPARISON_MEMORY.md).
