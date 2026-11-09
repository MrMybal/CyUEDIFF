# Core-only physical cloth consumer

Build against the installed 0.184 SDK using `CMAKE_PREFIX_PATH`, then supply an explicit UE 5.3 Windows member, global script TOC and version-four schema context with the certified physical fields/native tails:

```sh
physical_cloth_consumer before.utoc MEMBER before-global.utoc before-context.json after.utoc MEMBER after-global.utoc after-context.json
```

The example reads both inputs independently, projects physical geometry/bone/weight/tether/configuration values, interprets native transition records, evaluates the selected Chaos reference-pose transition algebra and reports named stored differences. Missing defaults remain unavailable. Only an explicit same-object pair is compared by default. It executes no Unreal process and uses no Git, CyUI or optional codec.

Append `--render-facts 0 0` only when the producer is independently known to omit mobile-minimum-LOD data and vertex colors. This additionally reads the native skeletal render mapping and certifies base-LOD GUID/physical-index bounds. The explicit facts apply to both supplied inputs; run sides separately when producer facts differ. Biased mapping sources are not inferred by adding LOD indices. See [PHYSICAL_CLOTH.md](../../docs/PHYSICAL_CLOTH.md).
