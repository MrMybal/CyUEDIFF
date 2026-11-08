# Compressed scalar animation curves — post-0.181 development

`compressed_curves.hpp` exposes an immutable Core decoder for explicitly selected UE 5.3 Windows curve buffers. It operates without Unreal, Git or a UI runtime. Three exact built-in codec formats are implemented: CompressedRichCurve, UniformIndexable and UniformlySampled. The host supplies names in encoded curve order and the complete byte buffer; it must independently establish the actual registered codec and matching build context. Instance names or a codec's mere presence in a package do not establish that selection.

## Stored data and evaluation

RichCurve decoding covers Empty/Constant/Linear/Cubic/Mixed/Weighted descriptors, float32 and uint16 quantized times, aligned mode/time/value/tangent/weight regions, explicit pre/post extrapolation and the empty default sentinel. Constant descriptors retain their serialized value without inventing stored keys. Quantized times retain the decoded float-grid values. Mixed/weighted interpolation and weight modes are explicit; the selected profile uses the complete native tangent records. Exact shared key windows are accepted and counted once; partial overlaps are refused. Unreferenced bytes remain counted and covered by the complete buffer hashes.

UniformIndexable reads curve-major float samples. UniformlySampled reads the constant bitset, constant values and time-major animated sample rows, checking declared counts against actual valid curve bits. Stored sample rates/counts determine sampling independently of bone animation interpolation. Key inspection labels uniform times as derived from the sample grid. It preserves encoded values instead of claiming those samples were original editable source keys.

`sample_curve(index, seconds, empty_default, control)` samples one encoded-order curve. `sample` returns all encoded-order values with exact selected time and its float32 native representation. RichCurve follows the observed native profile, including segment endpoint, tangent-selector and weighted cycle-offset behavior; these are not interchangeable with editable source RichCurve evaluation. Uniform curves interpolate their stored samples; negative times follow the codec's first-segment extrapolation, and times beyond the last sample retain its value. Unsafe/non-finite time, cycle-count or arithmetic domains fail explicitly.

This scalar API does not reproduce process-local FName sorting, blended-curve filtering or animation runtime evaluation. UE 5.3 UniformlySampled has distinct single-curve and batch indexing paths. The original reference adapter avoids its potentially out-of-row single-curve access. Installed validation compares actual batch results only when the measured name permutation is the encoded order. Other permutations require additional host/runtime context; no universal runtime correspondence is inferred.

## API and budgets

```cpp
cyuediff::CompressedCurveSource source;
source.codec = cyuediff::CompressedCurveCodec::rich_curve;
source.names = names_in_encoded_order;
source.buffer = complete_curve_buffer;
cyuediff::CompressedCurveOptions options;
options.profile = cyuediff::CompressedCurveProfile::unreal_5_3_windows;
auto curves = cyuediff::decode_compressed_curves(source, options);
auto metadata = curves.catalog();
auto keys = curves.key_preview(0, 2048);
auto original_key = curves.stored_key(0, 3);
auto value = curves.sample_curve(0, 0.5);
```

`catalog` omits key arrays; `to_json(true)` includes decoded keys. Bounded `key_preview` preserves original indices/endpoints and reports omitted keys. Its default maximum is 2,048 points, with a permitted range of 2–4,096. Caller-selected keys are never clamped. Metadata retains explicit empty-default behavior, source offsets/tags, per-curve encoding fingerprints, complete SHA-256/IoHash and unreferenced byte counts. A per-curve fingerprint excludes relocation offsets; the global codec/layout context remains separate.

Defaults bound input bytes to 128 MiB, aggregate decoded keys to four million, curves to 100,000, name text to 16 MiB and decoded working/output allocations to 256 MiB. These include owned encoded buffers, key storage and bounded hashing scratch. Caller source allocations and requested JSON serialization are separate. Counts, windows, mode tags, names, finite values, temporal ordering and constant-bitset cardinality are checked before publishing a model. Cancellation/progress cover hashing, curve/key loops and sampling. Copies share immutable state; later mutation of caller buffers cannot alter results.

## Independent evidence and installed consumer

The portable original fixture `tests/fixtures/compressed_curves_ue53_reference.json` contains 78 actual Engine codec cases. It covers all rich descriptor families, both temporal encodings for each nonconstant family, six extrapolation modes, weighted selectors, and uniform layouts with 1/5/37 curves crossing a bitset word. Only original numerical data is included. Undefined alignment gaps are normalized before calling the actual codec; the original adapter/build fingerprint is recorded. Unit checks also exercise every input prefix, malformed counts/tags/offsets/bitsets/non-finite data, partial/shared windows, output/read/key limits, cancellation, focused queries and immutable ownership.

Three separately prepared animations each supply six original scalar curves. Their complete cooked IoStore curve buffers match the independently exported codec buffers and stored codec paths. Mapped settings identify the actual registered Codec among additional unused codec exports. A separately built installed Core-only consumer matches 4,626 values at 257 times per animation; the largest measured difference is 4.7684e-7. These are actual curve-codec calls, not source-model curve samples. Private assets, mappings, references and generated evidence remain outside Git.

The installed `examples/compressed_curve_consumer` takes an explicit codec, data-only names/times JSON, a complete binary buffer and a protected output path. `validate_compressed_curves.py` validates actual codec references and optional protected-source hashes through that consumer. See the [example](../examples/compressed_curve_consumer/README.md) and the [optional original Unreal adapter](../tools/unreal_reference/README.md).

## Whole-package binding and scalar differences

The [cooked animation reader](COOKED_ANIMATIONS.md) now accepts an explicit `CookedAnimationOptions::curve_settings` dependency. It resolves the exact stored AnimSequence reference and the settings object's mapped `Codec` ObjectProperty. Package name/public export hash, class and non-template checks apply. The three supported built-ins use their verified base lookup behavior; an unused codec export or a suggestive instance name cannot select a decoder. Stored codec paths remain provenance even when different from the registered object's current handle. Unknown plugin/external lookup behavior remains opaque.

`compressed_curves()` exposes the immutable decoder, `curve_information()` the native coverage/provenance, and `sample_curves(seconds, empty_default)` named scalar values. Curve-only packages do not need a bone decoder, Skeleton binding or an invented bone-frame range. Confirmed empty names and bytes are known absence; unknown coverage never becomes a removal. Requested seconds/defaults and their float32 native representations are reported. `compare_cooked_animation_curves` reports encoding and selected scalar changes separately, with independent times, a scalar tolerance, bounded pairs/output and cancellation. This covers encoded-order scalar channels, independently of runtime blending or bone interpolation.

`zen-animation` includes bound curve metadata and optional stored keys. `zen-animation-curves --time` and `zen-animation-curves-diff --before-time --after-time --value-tolerance` expose focused scalar review. Before/after curve-settings TOCs and IDs are explicit, and every opened input remains protected against output replacement.

The installed [whole-package consumer](../examples/cooked_curve_consumer/README.md) and `validate_cooked_curves.py` additionally prove actual settings binding, complete cooked buffer SHA-256/IoHash, encoded identities and 4,626 direct Unreal codec values through the installed Core-only SDK. The largest measured delta is 4.7684e-7. Protected original/derived/context inputs remain unchanged. Original fixtures and CLI checks cover all three codecs, curve-only/empty/unknown peers, registration ambiguity, malformed dependencies/data, independent contexts/times, budgets, cancellation and output protection.

## Remaining integration

The source-built `CookedCurveView` now provides worker-prepared EN/FR scalar inspection and comparisons, integrated into **Cooked animations → Scalar curves**. Before/after curve-settings TOCs/IDs are independent, and workspace format 11 preserves scalar mode, names, independent seconds, tolerance, empty default, plot range, exact key indices, filters and property selection. It plots at most 257 samples and 2,048 stored markers per selected curve/side, clips lists and caps a review at 10,000 pairs. Unknown peers remain unavailable; superseded generations cannot publish as current.

Original UI tests cover five themes, EN/FR, wide/narrow frames, 5,000-row clipping/selection, cancellation and atomic state restoration. Six native real-package reviews match 3,084 plotted scalar values and 72 encoding/value decisions against actual Unreal codec references (maximum delta 4.7684e-7), with protected inputs unchanged. Further codec/plugin/build profiles and broader runtime correspondence remain open. No new release, public push or complete-product claim is implied.
