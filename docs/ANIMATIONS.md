# Stored animation source tracks

No Unreal process or redistributed engine files are needed.

## Bounded raw animation key arrays - 0.108

Loose tagged RawAnimSequenceTrack values now decode the UE5 Release 39–51 bulk serializer for verified UE4 522 / UE5 1004 or 1013 profiles. Three element-size/count-prefixed float arrays retain position, quaternion XYZW and scale keys. Scalar work shares the property reader budget; wrong strides, counts, non-finite components, truncation and unknown profiles roll back the whole property to opaque bytes. Older custom versions retain tagged member decoding. Tests cover both file profiles, custom-version boundaries, atomic rollback, aggregate limits and cancellation. Engine source was consulted locally; no engine code or assets are included.

## Named animation source projection and comparison - 0.109

The independent animation.hpp API projects exact Engine.AnimDataModel objects into bone-name-keyed position, quaternion XYZW and scale streams, with explicit model selectors for pair comparisons. Storage order and selected object path differences do not hide per-bone changes. Legacy reflected and recent bulk tracks share the same model; Quat4f reflected values now decode as four float components. Duplicate or missing names retain indexed records and incomplete coverage. Omitted frame-rate components, opaque channels and missing input remain unknown. Limits cover model/track/key counts, copied nodes, text and depth, with cancellation. Two engine tutorial assets now decode all 68 legacy source tracks, including rotations; no runtime animation evaluation or defaults are implied.

## Stored source frames, CLI and SDK example - 0.110

animation_frame reads a selected integer source-key frame in bone-local space. Each channel must store one constant key or exactly NumberOfKeys entries. Empty, missing or mismatched channels and non-unit quaternions stay unavailable. No interpolation, hierarchy composition, root motion, retargeting, skinning or default transform is evaluated. Out-of-range frames fail; seconds are emitted only when both rational frame-rate terms are stored. New animation, animation-diff and animation-frame CLI commands share project resolution and protected output handling. Pair commands accept independent model object selectors and retain exit codes 0 identical, 1 changed, 2 incomplete/error. The separate examples/animation_consumer links only the installed Core SDK. Tests include two actual binary fixture profiles, 18 frame/file checks and 46 public CLI checks. The two real animations yield 68 ready local tracks at sampled frames while omitted frame-rate denominators remain explicit; their full metadata comparisons correctly report incomplete.

## Bilingual source-track review and retained frames - 0.111

The Animations desktop tab now owns normalized source projections, independent model choices, named track search and confirmed-change navigation. A shared integer frame selector displays typed stored local values before/after; shorter, missing or ambiguous channels stay unavailable. A selected channel component has overlaid source-key plots with click-to-select frames, capped at 2,048 sampled display points per side. The track union is capped at 4,096 rows. Model, track, filter, frame, component and typed inspector state round-trip through workspace files; missing model choices never fall back. Drawing performs no package loading or key projection. English/French and all five themes were checked, including narrow views. New checks cover 280 renderer-free UI frames and four native process/workspace scenarios; the extended native harness validates 1,080 hidden WARP frames and 36 scenes across themes/locales.

## Public commands

```powershell
cyuediff animation Before.uasset --out stored-tracks.json
cyuediff animation-diff Before.uasset After.uasset --out named-key-diff.json
cyuediff animation-frame Before.uasset --frame 20 --out local-frame.json
```

`animation-diff` automatically selects the sole model in each package. Use `--before-object` and `--after-object` for packages with multiple models. `animation-frame` accepts `--object` for explicit selection. Reports use `cyuediff.animation/1`, `cyuediff.animation-diff/1` and `cyuediff.animation-frame/1`. Model paths are context for paired comparisons; track keys use exact stored names prefixed by `name:` and JSON Pointer escaping. Ambiguous tracks use `index:` records and incomplete coverage.

A frame is an integer source-key index, not a time seek into compressed playback. One-key channels are constant; a channel with the full stored key count uses the selected index. Other counts remain unavailable. The snapshot preserves stored streams even when their cardinality prevents sampling. Quaternion components remain XYZW without normalization or sign canonicalization: sign-only or numerically different stored keys remain visible as changes.

Stored `NumberOfFrames` and `NumberOfKeys` remain separate fields. Seconds require both stored rational frame-rate terms; omitted denominators are not replaced by engine defaults. Full metadata comparisons can therefore be incomplete even while all local keys at a selected frame are available. Other source curves, attributes, notifications, control-rig models, compressed sequences and external skeleton evaluation are outside this API’s scope. No Unreal-owned assets are distributed.

## SDK

```cmake
find_package(CyUEDiff CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE CyUEDiff::Core)
```

Include `<cyuediff/animation.hpp>`. The API provides `animation_snapshot`, `compare_animation_models` and `animation_frame` with aggregate resource limits and operation control. The independent `examples/animation_consumer` project runs on a host-created model without arguments and optionally reads one or two loose assets.

## Source animation corpora and installed SDK validation - 0.112

The source validator hashes all input packages and existing companions before and after inspection, verifies per-model self-comparison coverage and samples first, middle and last stored frames. A new independent installed SDK consumer constructs its own complete model, compares a changed key and validates source-frame timing without Git, CyUI or Unreal. The distribution validator builds this eighth Core consumer and keeps desktop smoke checks outside user preferences. Full configuration and corpus results are recorded in VALIDATION.md.

## Separate stored transform curve API

Stored AnimDataModel float and transform curves are available through curves.hpp and the Curves tab. Explicit transform scope exposes translation/Euler/scale XYZ components with named differences, independent raw evaluation and sampled comparisons. It does not bind those curves to bone tracks or evaluate an animated hierarchy. See [curve contracts](CURVES.md).

## Stable local transform difference metrics - 0.128

Local source-transform metrics now compute Euclidean translation distance, sign-invariant shortest quaternion orientation distance in degrees and maximum absolute scale-axis delta. The orientation metric uses stable atan2 differences after accepting only near-unit stored quaternions; source components remain untouched. Independent finite nonnegative tolerances, signed scales, tiny/half-turn/wrapped orientations, unknown channels, overflow and cancellation are checked. These are bone-local value metrics without hierarchy evaluation or retargeting.

## Named independent-frame transform comparisons - 0.129

compare_animation_frames pairs exact bone track names at independently chosen integer source frames. Reports retain raw before/after samples, Euclidean position, shortest sign-invariant orientation and maximum scale-axis differences, independent tolerances and changed/unavailable counts. Known track additions/removals require a complete identity table; duplicate or unresolved opposite names never confirm absence. Missing timing does not invalidate otherwise usable local values. Partial inputs, unknown channels, limits and cancellation stay explicit.

## Public independent-frame CLI and encoded transform cases - 0.130

animation-frame-diff accepts independent source frame indices and model contexts, position/rotation/scale tolerances and protected JSON outputs. Orientation tolerances use degrees; scale uses maximum absolute axis delta. Independently encoded profiles 1004 and 1013 now include antipodal/quarter-turn/half-turn/wrapped rotations, signed scales, empty/omitted/removed tracks, unknown names/quaternions and explicit timing changes. CLI exit codes preserve identical/changed/incomplete distinctions.
