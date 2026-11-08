# Cooked Nanite comparison consumer

This installed Core-only sample compares two complete UE 5.3 Windows StaticMesh resources with independent explicit mappings/global tables and producer mobile-LOD facts. It needs no Unreal, GPU, Git or UI runtime.

```sh
cmake -S <prefix>/share/CyUEDiff/examples/cooked_nanite_comparison_consumer -B build -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build --config Release
cooked_nanite_comparison_consumer CONFIG.json OUTPUT.json [CODEC]
```

```json
{
  "schema": "cyuediff.cooked-nanite-comparison/1",
  "compareAttributes": true,
  "before": {
    "container": "ABSOLUTE/before.utoc",
    "entry": "PACKAGE_CHUNK_ID",
    "global": "ABSOLUTE/global.utoc",
    "context": "ABSOLUTE/before-context.json",
    "mobileMinLod": false
  },
  "after": {
    "container": "ABSOLUTE/after.utoc",
    "entry": "PACKAGE_CHUNK_ID",
    "global": "ABSOLUTE/after-global.utoc",
    "context": "ABSOLUTE/after-context.json",
    "mobileMinLod": false
  }
}
```

Inputs must be absolute existing files; replace every example value with actual matching producer data. Each side may explicitly set `decodeAttributes: false` to inspect incomplete attribute coverage. Attribute decoding otherwise follows `compareAttributes` (default true). Each read caps Nanite vertices/triangles at 200,000; Core aggregate byte/read/cancellation limits still apply. An optional codec is caller-owned and separately licensed.

Output separates stored encoding, exact full-leaf surface and exact decoded common-surface attributes. Rotations, duplicate multiplicity, numeric materials and native triangle references are retained. Identical shading matches first; remaining overlapping duplicates pair deterministically and report ambiguity. Zero-bit/unrequested channels remain unknown; known color/UV changes can still be returned in an incomplete comparison. No attribute identity is inferred for newly added/removed surfaces. Runtime LOD/material evaluation and approximate matching are excluded.

Exit codes: 0 identical in the supported requested scope, 1 supported change, 2 incomplete/error. Both source/dependency sets, config and codec are protected against output replacement, including existing hard links. See `docs/NANITE_GEOMETRY.md` for the exact contract.
