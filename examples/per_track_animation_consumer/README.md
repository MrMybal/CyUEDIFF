# Core-only compressed animation consumer

Configure against a separately installed SDK; no Unreal or CyUI installation is needed:

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH=ABSOLUTE_SDK_PATH
cmake --build build --config Release
per_track_animation_consumer INPUT.json BUFFER.bin OUTPUT.json
```

This example deliberately selects the UE 5.3 Windows per-track profile. The caller must verify the codec class and serialized metadata before supplying a buffer. It does not locate native package boundaries, select codecs by name, bind Skeleton names or decode curves. See [the format contract](../../docs/ANIMATION_COMPRESSION.md) in a source checkout; the installed manual contains the same document.

```json
{
  "frames": 5,
  "trackToSkeleton": [9],
  "trackOffsetCount": 2,
  "scaleOffsetCount": 0,
  "scaleStripSize": 2,
  "keyBytes": 0,
  "sampleFrames": [0, 0.5, 4],
  "step": false,
  "includeKeys": false
}
```

For this implicit-channel example, BUFFER.bin contains two signed int32 values of -1, little-endian. Its complete size is eight bytes. No names are inferred from the Skeleton index 9. With an absent scale view, samples have zero translation, identity rotation and unit scale.

For a real buffer, supply its exact frame count, compressed track map, serialized view counts and complete bytes. Output contains immutable decoder metadata plus sampled numeric bone-local poses. Choose a new output path. The example limits metadata to 16 MiB, input bytes to 256 MiB and output to 200,000 track samples; the decoder has additional limits.

The private validator accepts a `cyuediff.per-track-animation-validation/1` manifest with `cases` entries containing absolute `input`, `buffer` and independent compressed-codec `reference` JSON paths. Optional `protectedSources` entries contain absolute `path` and expected `sha256` for authorized originals. References come from `ExportCompressedAnimationReference`, not source-model key exports. Its output directory must be new:

```text
python validate_per_track_animation.py --consumer ABSOLUTE_EXE --manifest ABSOLUTE_MANIFEST --output NEW_PRIVATE_DIRECTORY
```
