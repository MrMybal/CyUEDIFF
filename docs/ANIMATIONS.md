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

## Retained independent frames and local comparison controls - 0.131

The Animations tab now selects shared or independent before/after source frames and retains position, orientation-degree and scale tolerances. Selected-track metrics distinguish usable changes, values within tolerance and unavailable comparisons without hiding serialized key differences. Raw plots retain both frame cursors, with chart clicks controlling the before coordinate in independent mode. English/French, five themes, narrow layouts, ambiguous opposite names and atomic state validation are covered. Drawing uses owned projections and prepared samples.

## Current local frame comparison contract

Include animation.hpp and call compare_animation_frames(before, after, before_frame, after_frame, before_object, after_object, options). AnimationFrameDiffOptions contains AnimationOptions, position_tolerance, rotation_tolerance_degrees, scale_tolerance and max_track_pairs (100,000 by default). Selectors may be omitted only when each side contains one model. Source frames are integer key indices; they do not imply aligned seconds between clips. The API does not clamp frames, interpolate, evaluate hierarchy, bind a Skeleton, retarget or skin geometry.

```powershell
cyuediff animation-frame-diff Before.uasset After.uasset --before-frame 10 --after-frame 20 --position-tolerance 0.01 --rotation-tolerance 0.1 --scale-tolerance 0.001 --out frame-difference.json
```

Reports use cyuediff.animation-frame-diff/1. Exact named tracks pair independently of storage order and selected object paths. Each entry retains raw before/after local channel values and a ready/changed/status result. Paired usable values include signed position/scale deltas, Euclidean positionDistance, shortest sign-invariant rotationDeltaDegrees and maximum absolute scaleMaxDelta. Position tolerances use stored translation units; scale tolerances use raw axis differences; rotation tolerance is 0..180 degrees. A metric changes only when it exceeds its tolerance. Norms must already be near unit; quaternion normalization is limited to orientation measurement and does not rewrite source keys. Tiny orientation distances use a stable atan2 form; q and -q have zero orientation difference.

comparedTracks counts usable paired tracks. changedTracks also includes confirmed additions/removals. unavailableTracks counts records without a usable comparison; any such record or unknown input/identity table makes the report incomplete. Empty known track arrays can compare identical, while omitted arrays stay unknown. An ambiguous or unresolved opposite name table cannot establish removal. Missing key channels, invalid quaternion norms and arithmetic overflow remain unavailable. Metadata timing gaps do not invalidate otherwise usable local key values: beforeTimeSeconds/afterTimeSeconds are emitted independently only when their own stored rational terms are present. The animation-frame schema now exposes completeInput, tracksCoverage and identitiesComplete to make this coverage explicit.

The Animations tab retains independentFrames, afterFrame, positionTolerance, rotationToleranceDegrees and scaleTolerance alongside existing selections. Shared mode remains the default. In independent mode, each cursor has its own source index and chart clicks choose the before frame. The selected local comparison is separate from stored-key status and filters. Numeric metrics update when choices change; drawing uses prepared data and never loads files. Old workspace files retain shared-frame behavior. CLI comparison exit codes are 0 identical, 1 changed and 2 incomplete/error.

## Read-only local frame corpora and installed SDK verification - 0.132

The animation corpus validator now checks same-frame local equality and first/last-frame differences alongside raw source streams, preserving hashes and unknown coverage. The installed Core consumer demonstrates shared and independent frame comparisons. Distribution verification and SDK/CLI parity include the new reports. Configuration, real/encoded corpus, visual capture and secret-scan results are recorded in VALIDATION.md.

## Explicit source animation hierarchy poses - 0.133

The Core animation_pose API binds source frame tracks to a caller-supplied Skeleton or SkeletalMesh by exact case-sensitive names. It composes bounded affine matrices in verified raw hierarchy order, retaining frame, optional stored timing, track provenance and local-versus-ancestor availability. Missing or ambiguous tracks never fall back to reference transforms or BoneTreeIndex. Unbound tracks and incomplete inputs keep the report incomplete. This is derived source geometry, without interpolation, retargeting, root-motion extraction, virtual-bone evaluation or skinning. Twenty-seven focused checks and 34 reference-pose regressions pass; shared affine arithmetic preserves the existing reference-pose contract.

## Independent derived animation pose comparisons - 0.134

Prepared-pose and four-package comparison APIs retain independent animation/Skeleton owners and frame indices, and compare named affine joint origins with a finite nonnegative position tolerance. Hierarchy membership is tracked separately from source channel readiness, so a missing channel cannot invent an addition/removal. Parent changes remain significant at any position tolerance. Sixteen focused checks cover inherited displacement, inclusive boundaries, independent selection, incomplete hierarchies, bounds and cancellation. Joint origins do not claim to detect leaf orientation changes: the separate bone-local frame comparison remains available.

## Explicit Skeleton-bound animation pose CLI - 0.135

animation-pose ANIMATION SKELETON --frame N and animation-pose-diff BEFORE AFTER with required --before-skeleton, --after-skeleton and independent integer frame options expose the new Core results. Object choices and per-side project contexts remain explicit; all four inputs and their companion paths participate in output protection. Exit codes are 0 for complete/identical, 1 for changed comparisons and 2 for incomplete/invalid inputs. Forty independently encoded file checks and 78 CLI checks validate the contract. A Windows sandbox-only failure on short temporary path aliases was isolated: the same project/context cases pass outside the sandbox, with no resolver modification.

## Reusable bilingual prepared animation pose review - 0.136

The separate CyUI AnimationPoseView accepts host-prepared Core poses and owns at most 4,096 named joints. It presents per-side source owners/frame provenance, typed joint positions, parent identity, inherited availability and a synchronized affine viewport with picking, filtering and finite position tolerance. Selection, tolerance and camera round-trip through validated data-only state; invalid state is rejected before mutation. Changing tolerance preserves the camera and uses the same Core result for colors and details. English/French and five themes pass 294 component checks across 280 renderer-free frames; the native harness now checks 1,980 hidden WARP frames in 66 scenes. Hosts must supply explicitly prepared poses outside rendering; automatic external Skeleton loading is not added to the desktop Animations tab in this milestone.

## Installed hierarchy-pose SDK and explicit corpus validation - 0.137

The standalone animation_pose_consumer links only the installed Core target and builds its own source tracks and raw Skeleton without Git, CyUI or Unreal. It also accepts two or four local package paths. The distribution validator checks this tenth independent Core consumer. A new source-pose corpus validator compares derived origins with an independent quaternion cross-product ancestor-walk oracle, hashes all inputs and existing companions, checks self comparisons and first/last source frames, and writes only below build-native. Native capture tooling accepts explicit external animation/Skeleton inputs. Detailed verified results are recorded in VALIDATION.md.

## Explicit hierarchy pose API and commands

Include `<cyuediff/animation_pose.hpp>` and call `animation_pose(animation, skeleton, frame, animation_object, skeleton_object, options)`. Each selector may be empty only for a sole supported owner. The caller supplies both packages; this API does not search external objects or infer Skeleton compatibility. Exact case-sensitive names bind unique ready source tracks to verified raw bones. Affine ancestor composition produces joint origins in the selected source component space. Reference transforms, retarget tables, virtual bones, compressed animation and skinning are not substituted. This affine display does not emulate Unreal FTransform decomposition when nonuniform scale introduces shear. Missing source timing leaves seconds absent without invalidating usable integer keys.

`AnimationPoseOptions` bounds projections, joints and hierarchy depth; its top-level `control` supplies shared cancellation and progress to both projections and composition. `AnimationPose::to_json()` emits `cyuediff.animation-pose/1`, provenance, named joints, local track bindings, unbound tracks and explicit completeness. `display_pose` is a geometry carrier for viewports; use the enclosing `to_json()` for the correct source-space labels.

`compare_animation_poses` accepts two prepared results, or four packages with independent `AnimationPoseSelection` values. It emits `cyuediff.animation-pose-diff/1`; its finite nonnegative tolerance applies to derived joint-origin distances. Parent changes remain significant. Rotation/scale changes that do not move origins belong to the separate local-frame comparison. Incomplete source coverage cannot certify whole-pose equality.

```powershell
cyuediff animation-pose Walk.uasset Rig.uasset --frame 10 --out pose.json
cyuediff animation-pose-diff Before.uasset After.uasset --before-skeleton OldRig.uasset --after-skeleton NewRig.uasset --before-frame 10 --after-frame 12 --position-tolerance 0.01 --out poses.json
```

Single poses accept `--object` and `--skeleton-object`; pair commands accept independent `--before-object`, `--after-object`, `--before-skeleton-object` and `--after-skeleton-object`. Per-side project contexts resolve both the animation and the supplied Skeleton. Exit codes are 0 complete/identical, 1 changed comparisons, 2 incomplete/error. All package inputs and companion paths are protected against output overwrites.

The optional separate UI header `<cyuediff/animation_pose_view.hpp>` exposes `AnimationPoseView`. A host evaluates Core poses outside rendering, calls `set_poses`, then draws the bounded owned view. Selection, camera, filter and position tolerance have validated state roundtrips. EN/FR and five themes are supported. The native desktop Animations tab continues to inspect bone-local streams; an external Skeleton file picker/loading workflow remains future work.

## Desktop source pose request and workspace contexts - 0.138

Desktop document requests now carry explicit per-side Skeleton paths, animation/Skeleton owners and integer frame selections. Shared frames are the default; independent after frames remain explicit. Workspace format four stores these choices, accepts prior formats with disabled bindings by default, bounds all new frame/string fields and resolves loose Skeleton paths with their corresponding project contexts. Callback/cache objects and codecs remain outside saved data. External associations currently apply to loose local packages. Validation passes 119 workspace-file checks and 52 queue/workspace checks.

## Worker preparation of explicitly bound desktop animation poses - 0.139

The desktop worker opens supplied loose Skeleton paths through the existing verified package cache, resolves each side with its own project context, and prepares selected Core poses before publication even when texture/mesh previews are disabled. Requested failures retain raw package inspection and a per-side unavailable pose, never known absence. Skeleton sources and companions are protected; cancellation propagates. Reports retain separate source-pose readiness, provenance, diagnostics and derived comparison. Seventeen new model checks and forty existing desktop checks pass, including shared cache ownership, invalid names/frames/files, mounted independent projects, input hashes and actual queue publication.
