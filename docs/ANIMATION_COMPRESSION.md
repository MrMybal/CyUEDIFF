# Explicit per-track compressed animation

`animation_compression.hpp` supplies an original, bounded **UE 5.3 Windows per-track compression decoder** in `CyUEDiff::Core`. It reads bytes without linking or launching Unreal. This is the compressed-buffer API. The higher-level [cooked animation reader](COOKED_ANIMATIONS.md) now supplies explicit Zen AnimSequence extraction, verified registered codec/Skeleton binding and named fractional frame diffs. Compressed curves and a dedicated desktop path remain open.

## Selection and input

The host must select `PerTrackAnimationProfile::unreal_5_3_windows` explicitly and supply the exact serialized view metadata. A codec instance handle such as `AnimCompress_PerTrackCompression_3` is an instance identity, **not proof of a codec class**. A package integration must verify the referenced compression settings export, its selected codec/outer chain and exact supported class before calling this decoder. Plugin subclasses are not automatically compatible.

`PerTrackAnimationSource` contains the complete `FUECompressedAnimData` buffer, in this order:

1. `track_offset_count` signed little-endian int32 offsets, two per compressed track: translation and rotation.
2. `scale_offset_count` signed little-endian int32 offsets. A present per-track scale view requires strip size one and one offset per compressed track.
3. Exactly `key_byte_count` encoded key bytes.

The views must cover the buffer exactly. `track_to_skeleton` retains unique nonnegative numeric Skeleton indices in compressed track order. Names and reference poses require a separately verified Skeleton; no names or defaults are guessed here. `frame_count` is the serialized compressed key/frame-domain count, including the final endpoint.

## Channels and timing

Supported vector channels use Float96, per-track Fixed48, IntervalFixed32 or Identity. Supported rotation channels additionally use Fixed32 and custom 11/11/10 floating-point Float32. Component masks, legacy zero-mask footprints, interval bounds, four-byte alignment and explicit byte/uint16 frame tables follow the selected wire profile. Partial-component Float96 rotations require another profile and are refused. Quaternion W is reconstructed from stored XYZ, and sampled rotations are normalized.

A missing translation is zero and a missing rotation is identity. **An absent scale view yields unit scale; a present scale view with an offset of -1 yields zero scale.** Encoded Identity vector keys also yield zero. These are the selected codec's behaviors, not reconstructed Skeleton defaults.

`sample(frame, interpolation)` accepts a finite explicit frame in `[0, frame_count - 1]`. It supports fractional frames, linear vector interpolation, shortest-arc normalized quaternion interpolation and step interpolation. Explicit tables retain their actual frame values, including repeated entries. Endpoints and unanchored tables follow the selected codec's lookup/extrapolation behavior. No time wrapping, sample-rate inference, additive base, retargeting, actor/component playback or curve evaluation is performed.

The immutable result exposes source buffer SHA-256 and BLAKE3-160/IoHash, decoded stored keys on request, source channel offsets/lengths/formats/flags, frame tables and the count of unreferenced key bytes. Shared exact windows are accepted and counted once; partial overlap is refused. Unreferenced bytes, including padding or other opaque content, remain covered by the complete buffer hash. Equality of sampled transforms does not imply equality of complete asset data.

## SDK usage

```cpp
#include <cyuediff/animation_compression.hpp>

cyuediff::PerTrackAnimationSource source;
// Supply verified serialized views, compressed track map and complete buffer.
cyuediff::PerTrackAnimationOptions options;
options.profile = cyuediff::PerTrackAnimationProfile::unreal_5_3_windows;
auto animation = cyuediff::decode_per_track_animation(source, options);
auto pose = animation.sample(12.5);
auto metadata = animation.to_json();
```

The installed `examples/per_track_animation_consumer` links only Core. Its README describes its explicit JSON metadata and binary buffer inputs. JSON serialization and caller-owned source allocations are separate from decoder output budgeting. Read bytes, track/frame counts, aggregate decoded keys and decoded/sample working output are bounded; cancellation/progress propagate through hashing, key/table loops and sampling. Malformed views, invalid offsets/counts, non-finite values, unordered/out-of-domain tables, unsupported formats and partial overlaps fail before a decoded result is published. Invalid sample domains or overflowing interpolation fail before a pose is returned.

`catalog(control)` exposes channel byte fingerprints without large timing/key arrays. Focused `sample_track` and `stored_key` queries avoid full-frame/key serialization. Bounded `key_preview` returns original indices/times/endpoints, omitted counts and explicit implicit values. The whole cooked reader exposes the same focused queries for verified named tracks; the source-built UI uses them on its cancellable worker.

## Independent evidence

The optional original Unreal reference adapter calls `BoneCompressionCodec->DecompressBone` directly, bypassing an editor source/raw-data fallback. For two separately cooked animations, the Core input buffer's complete IoHash equals Unreal's reported compressed buffer. All 68 tracks at every integer and half frame agree within 2e-6: 377 + 111 sampled poses per track, **331,840 numeric components**, largest measured component delta about **9.46e-7**. These are stored bone-local compressed-codec samples, not runtime retargeted or additive-applied poses.

An independent original numerical corpus covers **424** format/mask/timing/channel cases, including 8/16-bit tables, scales and both interpolation modes. Its 59,360 evaluated components match Unreal API output; it contains no Engine asset content. This corpus is committed as a portable regression fixture, with its producer/adapter provenance. Unit checks additionally cover corruption, truncation, exact/shared windows, opaque padding hashes, implicit channels, shortest-arc rotation, output/read/key budgets, cancellation and immutable ownership.

`scripts/validate_per_track_animation.py` repeats private real-corpus correspondence through a separately built installed Core-only example, checks complete buffer identity and protects declared originals by SHA-256. Generated real assets, schemas, buffers, references and private paths stay in ignored build directories. This primitive evidence does not certify other engine versions, compression plugins or package/Skeleton/curve binding by itself. Additional whole-package and Skeleton evidence is recorded in [COOKED_ANIMATIONS.md](COOKED_ANIMATIONS.md).
