# Stored rich curves

No Unreal process or redistributed engine files are needed.

## Bounded native rich-curve keys - 0.113

RichCurveKey values decode their packed three byte modes and six float32 components for verified UE4 522 / UE5 0, 1004 or 1013 loose tagged profiles. The reader retains raw interpolation, tangent and tangent-weight bytes without Unreal load-time repair. Exact property size, finite values, shared work bounds and cancellation are checked; malformed/unknown layouts roll back atomically. Fifteen independently encoded tests cover all profiles, invalid values, raw unknown modes and aggregate limits. The actual ContentIntroCurve asset now exposes three stored cubic keys and their tangents. No engine assets or source are redistributed.

## Named source lanes and stable key-time differences - 0.114

The curves.hpp Core API projects exact CurveFloat, CurveVector, CurveLinearColor and AnimDataModel float curves. Fixed vector/color channels preserve missing lanes, while named animation curves use exact stored CurveName values; unresolved legacy smart-name identities are not inferred. Keys retain every stored numeric/mode field, optional source indices and ordered identities based on exact finite time values. Insertions do not replace later keys. Duplicate/missing times or curve names retain indexed records and incomplete coverage; unsorted times remain visible. Explicit empty arrays differ from omitted defaults. Stored settings and additional rich-curve fields remain inspectable, with aggregate owner/lane/key/node/text/depth limits and cancellation.

## Supported source evaluation, public CLI and SDK consumer - 0.115

evaluate_curve returns exact stored key values or bounded double-precision constant, linear and unweighted cubic Hermite segments. Strictly increasing complete keys are required. Relevant weighted/unknown tangent segments, unknown interpolation, arithmetic overflow, missing input and times outside the stored range stay unavailable. No extrapolation, class defaults, automatic tangent repair or float-for-float Unreal runtime equivalence is claimed. Lookup is logarithmic after source projection and supports cancellation. The curves, curves-diff and curve-value CLI commands accept project contexts, explicit owner/lane selectors and protected outputs. Twenty-one evaluator/file checks and seventy-five CLI checks cover three independently encoded file profiles. The independent curve_consumer links only the installed Core SDK.

## Bilingual curve plots and retained source-key inspection - 0.116

The Curves desktop tab owns bounded curve projections and supports independent owner selection, source lanes, key-time search, changed-key navigation, stored tangent/weight details and synchronized evaluation coordinates. Source points remain visible while unsupported weighted/unknown segments have gaps in the sampled evaluation plot. Binding prepares 512 samples and at most 4,096 stored markers per side; drawing does not read packages or scan unbounded key arrays. Owner/lane/key/time/filter/inspector choices persist through workspace saves and process restarts. Exact key IDs now use portable 16-digit IEEE754 binary64 time bits, with signed zero sharing numeric identity. English/French, five themes, narrow layouts, unknown opposite tables, missing owners and empty curves have coverage through 291 renderer-free checks, 280 UI frames, four process/workspace checks, and 1,260 hidden WARP frames.

## Current public API and CLI

Include `<cyuediff/curves.hpp>` and link `CyUEDiff::Core`. `curve_snapshot` retains the supported owner/lane/key model, `compare_curves` compares selected owners, and `evaluate_curve` evaluates a selected lane at a finite input coordinate, and `compare_curve_values` compares two selected lanes on an explicit finite grid. This coordinate is the curve input; it does not imply seconds for every curve asset.

```powershell
cyuediff curves Before.uasset --out stored-curves.json
cyuediff curves-diff Before.uasset After.uasset --out key-diff.json
cyuediff curve-value Before.uasset --time 0.5 --out evaluated-value.json
cyuediff curve-value-diff Before.uasset After.uasset --time-start -1 --time-end 3 --samples 257 --value-tolerance 0.001 --out value-diff.json
```

Pair comparisons accept `--before-object` and `--after-object`. Sampled comparisons additionally accept independent `--before-lane` and `--after-lane` selectors. Evaluation accepts `--object` and `--lane`; selectors may be omitted only when exactly one choice exists. Fixed scalar/vector/color lane IDs are `value`, `x/y/z` or `r/g/b/a`. Animation float curves use exact modern `CurveName` values prefixed by `name:`; missing/duplicate identities remain indexed/incomplete. Legacy smart-name resolution, color-asset postprocessing, bone application and inherited/plugin curve classes remain outside this projection.

Schemas are `cyuediff.curves/1`, `cyuediff.curves-diff/1`, `cyuediff.curve-value/1`, `cyuediff.curve-values-diff/1` and `cyuediff.curve-components/1`. Unique times use `time:` followed by sixteen lowercase IEEE754 binary64 bit digits, with signed zero normalized for numeric identity. Original stored key order is retained separately; duplicate times are indexed and unavailable for evaluation. Added keys retain the identities of later existing keys. The UI displays numeric times instead of identity encodings.

Source comparisons cover stored keys and explicitly present settings; absent class defaults stay absent. Evaluation uses stored values and slopes in double precision, with logarithmic segment lookup. It does not run Unreal load-time repairs or promise identical floating-point rounding to an engine build. Exact keys are usable even when their adjacent segment is unsupported. Constant, linear, unweighted Hermite and valid weighted cubic segments are supported. Outside-range constant/linear/cycle/cycle-with-offset/oscillating evaluation requires an explicit stored pre/post mode; omitted settings and RCCE_None remain unavailable. A finite explicitly stored default applies only to a known empty key table; the MAX_flt incoming-default sentinel remains unavailable. Unavailable evaluations return CLI code 2; comparisons use 0 identical, 1 changed, 2 incomplete/error. The stored-key inspector and evaluated-time value are distinct UI selections. The optional display range and comparison tolerance persist in workspace files. Changed-key status describes serialized fields, independently of sampled-value tolerance.

A sampled report contains every coordinate, before/after evaluation, signed finite delta and changed flag, plus compared/changed/unavailable counts and a maximum finite absolute delta. A difference is changed when its absolute delta exceeds the nonnegative tolerance. Any unavailable value or overflowing delta makes the report incomplete. Zero changed samples establishes agreement only at the chosen coordinates, never continuous equality. The default sample count is 257; the default configured maximum and CLI limit are 4,096. A one-sample grid requires equal start/end coordinates. The SDK can configure its own resource ceiling.

The independent `examples/curve_consumer` constructs a host-owned curve without arguments, or reads one/two explicit assets. No engine source or assets are included in the distribution.

## Read-only curve corpora and installed SDK verification - 0.117

The curve corpus validator checks source/companion hashes, per-owner self-comparison coverage and selected stored/in-segment sample coordinates. The ninth independent installed Core consumer validates source keys, a changed value and supported evaluation without Unreal, Git or CyUI. The distribution validator and prepared CI include this consumer. Native and Core-only configuration results, real and synthetic coverage, visual captures and secret scans are recorded in VALIDATION.md.

## Bounded weighted cubic evaluation - 0.118

Stored active tangent weights now form two-dimensional cubic Bezier handles. A bounded solver isolates time-monotone branches at derivative roots, bisects each branch with cancellation, verifies the normalized time residual and selects the largest valid parameter when time handles create multiple roots. Independent parametric tests verify inverse evaluation, overshooting time handles, zero-length weights, negative/unknown active weights and cancellation. Inactive weights retain their existing Hermite semantics. Non-finite geometry, unresolved roots or overflow remain unavailable; no engine load-time repair or float-runtime identity is claimed. Existing encoded weighted fixtures and public CLI expectations now confirm usable weighted segments.

## Explicit stored extrapolation and empty-curve defaults - 0.119

Evaluation now accepts explicitly stored constant, linear, cycle, cycle-with-offset and oscillating pre/post modes, as raw bytes or exact qualified/unqualified Unreal enum names. Linear extrapolation uses adjacent stored key values. Cyclic endpoints retain the first/last-key boundary convention, track cycle count and preserve unknown evaluation gaps; unresolved arithmetic or counts above INT32_MAX stay unavailable. An explicit finite DefaultValue is usable only with a known empty key table; the MAX_flt incoming-default sentinel remains unknown. Omitted modes/defaults and RCCE_None are not repaired or inferred. Fifty-seven focused checks cover signed cycles, endpoints, offset, parity and missing settings, and encoded package tests now cover all five modes and defaults in three file profiles.

## Explicit sampled value comparisons and tolerances - 0.120

compare_curve_values and curve-value-diff compare independently selected owner/lane pairs over a finite explicit sample grid. Reports retain sampled coordinates, before/after evaluations, signed deltas, absolute tolerance, changed/unknown counts and the maximum finite delta. Any unavailable sample or overflow makes the report incomplete while preserving usable differences. Grids are capped by a configurable 4,096-sample default, validate ranges/counts and support cancellation. Identical means identical within tolerance at those coordinates only. The SDK consumer demonstrates the same API. Eleven focused Core cases and 108 cumulative public CLI cases cover identities, tolerance, unknown domains, selectors, resource limits and protected outputs.

## Saved evaluation ranges and sampled desktop comparison - 0.121

The Curves tab now retains an explicit optional display range and absolute comparison tolerance. Cached before/after plots share one bounded sampled comparison report, with changed/unavailable counts and maximum finite delta. Stored-key changes remain independently inspectable even when sampled values fall within tolerance. Out-of-domain values stay unavailable when stored extrapolation modes are omitted; supported explicitly stored modes extend the preview. Range endpoints and tolerances validate atomically during workspace restoration. English/French, five themes and narrow layouts cover these states without package reads during drawing.

## Expanded evaluation corpora and installed SDK verification - 0.122

Read-only corpus validation now probes explicit empty curves, both sides of the stored domain and per-lane sampled self-comparisons. It preserves unavailable coordinates rather than treating missing extrapolation modes as defaults. Independently encoded fixtures cover every supported mode, weighted/invalid weights and sentinel defaults in all three file profiles. The extracted SDK consumer exercises sampled comparison as well as source keys and individual values; report parity with the installed CLI and final configuration/capture results are recorded in VALIDATION.md.

## Explicit animation transform curve projection - 0.123

CurveOptions now selects float or transform animation curve scope. The default float scope is unchanged. Exact stored TransformCurves records expose nine named scalar lanes: translation, Euler rotation and scale XYZ. Modern unique names define stable groups; ambiguous or absent names remain indexed and incomplete. Missing vector components, arrays and keys remain unknown, while explicit empty arrays remain known. Stored group and vector metadata is retained with shared projection budgets and cancellation. This does not apply transforms to bones, infer additive defaults or evaluate transient VectorCurves.

## Independent vector, color and transform component values - 0.124

evaluate_curve_components evaluates raw scalar/vector/color channels or one explicitly selected animation transform group at a finite coordinate. Every component retains its own evaluation result and unavailable value; omitted scale never becomes an identity scale. Stored Euler XYZ means roll/pitch/yaw in degrees, without quaternion conversion, blending, additive application or bone evaluation. Invalid time/owner/group selectors, ambiguous identities, partial input and cancellation have explicit coverage.

## Typed file fixtures and public transform component commands - 0.125

The curve commands now accept an explicit --curve-family float|transform selector for AnimDataModel scope. curve-components reports one raw vector/color group or one selected animation group at --time, retaining per-channel gaps. Independent tagged binary fixtures cover UE4 522 / UE5 0, 1004 and 1013, actual indexed FloatCurves members, stable named differences/reordering, ambiguous names, missing components, empty/omitted tables, weights, extrapolation and explicit/sentinel defaults. CLI selectors, flag ownership, exit codes and protected input/companion outputs are checked.

## Bilingual transform family and component review - 0.126

The Curves tab now switches cached float and transform animation families and offers translated component labels. Independent owner, exact group/component lane, key, input coordinate, range, tolerance and inspector choices persist in workspace files. Changing family does not substitute missing opposite data. Invalid family state is rejected atomically. All projections are owned and prepared at binding; drawing does not load packages. English/French labels and all five themes include changed, missing, ambiguous, empty/default and narrow transform views.

## Component corpora and installed transform SDK validation - 0.127

Read-only curve corpus checks accept a selected animation family and include grouped component evaluations alongside individual coordinates and sampled self-comparisons. The Core-only consumer constructs its own transform group or reads explicitly selected groups from files, preserving partial component results. Distribution verification exercises both scalar and transform APIs after extraction. Matching SDK/CLI reports, source hashes, configuration/capture results and remaining real-corpus limitations are recorded in VALIDATION.md.

## Current transform and component contract

CurveOptions::animation_family defaults to AnimationCurveFamily::Float. Explicit Transform scope reads only the stored CurveData.TransformCurves array for exact AnimDataModel owners. CurveFloat/CurveVector/CurveLinearColor assets keep their existing lanes in either scope. The two animation scopes do not imply each other's completeness. A missing array stays unavailable; an explicit empty array is known. Unreal's transient VectorCurves table is not projected.

Unique modern CurveName values form group IDs prefixed with name:. Transform lane IDs append /translation.x, /translation.y, /translation.z, /rotation.x, /rotation.y, /rotation.z, /scale.x, /scale.y or /scale.z. Names containing slash/tilde remain literal in IDs and are escaped when used in JSON Pointer change paths. Indexed records retain ambiguous or missing names with incomplete coverage. curveGroups retains group/vector metadata separately; source order is context for unique names rather than a named change. Fixed vector members require their actual stored array indices 0, 1 and 2.

```powershell
cyuediff curves Animation.uasset --curve-family transform
cyuediff curves-diff Before.uasset After.uasset --curve-family transform
cyuediff curve-value Animation.uasset --curve-family transform --lane "name:Root/translation.x" --time 0.5
cyuediff curve-components Animation.uasset --curve-family transform --curve-id "name:Root" --time 0.5
cyuediff curve-value-diff Before.uasset After.uasset --curve-family transform --before-lane "name:Root/rotation.z" --after-lane "name:Root/rotation.z" --time-start 0 --time-end 1
```

evaluate_curve_components reports every selected raw component with its individual readiness, method, value and diagnostics. The values map retains null for unavailable components; usable components remain visible when the group is incomplete. An omitted group selector is allowed only for a sole animation group. Static scalar/vector/color assets need no group selector. Transform Euler X/Y/Z correspond to roll/pitch/yaw in degrees. There is no quaternion conversion, additive blending, scale-one fallback, bone binding or runtime transform application. Explicit stored defaults/extrapolation and supported weighted evaluation apply independently per lane.

The Curves tab retains animationFamily (float or transform), owner/lane/key, input coordinate, range, tolerance and inspector choices. Family changes use owned cached projections and translated channel labels, with unknown opposite fields kept explicit. The Core-only curve_consumer example accepts [--transform CURVE_ID] [BEFORE [AFTER]], or constructs host-owned scalar/transform examples with no input files.
