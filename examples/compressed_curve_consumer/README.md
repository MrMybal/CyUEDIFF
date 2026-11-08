# Core-only compressed curve consumer

This installed example links only `CyUEDiff::Core`. It needs no Unreal, Git or UI runtime. The explicit wire profile is UE 5.3 Windows. The caller selects `rich`, `indexable` or `uniform` and must independently verify the actual codec/build and source buffer.

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH=ABSOLUTE_SDK_PATH
cmake --build build --config Release
compressed_curve_consumer rich INPUT.json BUFFER.bin OUTPUT.json
```

The JSON input has `schema: "cyuediff.compressed-curve-input/1"`, `names` in encoded curve order and finite numeric `times` in seconds. Output contains metadata, bounded original-key/sample previews and scalar samples. Uniform key times are identified as derived. This example limits input metadata to 16 MiB, raw data to 128 MiB, times to 4,096, total sample values to 200,000 and total preview points to 200,000. Existing source inputs, including hard-link aliases, cannot be output destinations. Choose a private result path.

`scripts/validate_compressed_curves.py` takes `--consumer`, `--manifest` and a new `--output` directory. Its manifest uses `schema: "cyuediff.compressed-curve-validation/1"` and 1..100 `cases`, each containing an absolute `reference` path. References come from the optional original adapter's `compressed-curves` mode and include the actual codec class, complete buffer identity, encoded names, measured runtime-name permutation and batch values. Optional `protectedSources` contain absolute `path`/`sha256` pairs. The validator compares all measured scalar values and complete buffer hashes, then rechecks protected inputs.

This API samples encoded-order curves. It does not apply process-local FName ordering, runtime filtering/blending or a guessed curve codec. In particular, UniformlySampled validation requires the measured runtime-name permutation to equal encoded order; its unsafe single-curve path is not used as a reference. Full package dependency binding and desktop integration are separate work. See [the full contract](../../docs/COMPRESSED_CURVES.md).
