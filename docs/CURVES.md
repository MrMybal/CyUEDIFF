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

## Public API and CLI

Include `<cyuediff/curves.hpp>` and link `CyUEDiff::Core`. `curve_snapshot` retains the supported owner/lane/key model, `compare_curves` compares selected owners, and `evaluate_curve` evaluates a selected lane at a finite stored time coordinate. This coordinate is the curve input; it does not imply seconds for every curve asset.

```powershell
cyuediff curves Before.uasset --out stored-curves.json
cyuediff curves-diff Before.uasset After.uasset --out key-diff.json
cyuediff curve-value Before.uasset --time 0.5 --out evaluated-value.json
```

Pair comparisons accept `--before-object` and `--after-object`. Evaluation accepts `--object` and `--lane`; selectors may be omitted only when exactly one choice exists. Fixed scalar/vector/color lane IDs are `value`, `x/y/z` or `r/g/b/a`. Animation float curves use exact modern `CurveName` values prefixed by `name:`; missing/duplicate identities remain indexed/incomplete. Transform animation curves, legacy smart-name resolution, color-asset postprocessing and inherited/plugin curve classes remain outside this projection.

Schemas are `cyuediff.curves/1`, `cyuediff.curves-diff/1` and `cyuediff.curve-value/1`. Unique times use `time:` followed by sixteen lowercase IEEE754 binary64 bit digits, with signed zero normalized for numeric identity. Original stored key order is retained separately; duplicate times are indexed and unavailable for evaluation. Added keys retain the identities of later existing keys. The UI displays numeric times instead of identity encodings.

Source comparisons cover stored keys and explicitly present settings; absent class defaults stay absent. Evaluation uses stored values and slopes in double precision, with logarithmic segment lookup. It does not run Unreal load-time repairs or promise identical floating-point rounding to an engine build. Exact keys are usable even when their adjacent weighted segment is unsupported. No outside-range extrapolation or inferred empty-curve default is applied. Unavailable evaluations return CLI code 2; comparisons use 0 identical, 1 changed, 2 incomplete/error. The stored-key inspector and evaluated-time value are distinct UI selections.

The independent `examples/curve_consumer` constructs a host-owned curve without arguments, or reads one/two explicit assets. No engine source or assets are included in the distribution.

## Read-only curve corpora and installed SDK verification - 0.117

The curve corpus validator checks source/companion hashes, per-owner self-comparison coverage and selected stored/in-segment sample coordinates. The ninth independent installed Core consumer validates source keys, a changed value and supported evaluation without Unreal, Git or CyUI. The distribution validator and prepared CI include this consumer. Native and Core-only configuration results, real and synthetic coverage, visual captures and secret scans are recorded in VALIDATION.md.

## Bounded weighted cubic evaluation - 0.118

Stored active tangent weights now form two-dimensional cubic Bezier handles. A bounded solver isolates time-monotone branches at derivative roots, bisects each branch with cancellation, verifies the normalized time residual and selects the largest valid parameter when time handles create multiple roots. Independent parametric tests verify inverse evaluation, overshooting time handles, zero-length weights, negative/unknown active weights and cancellation. Inactive weights retain their existing Hermite semantics. Non-finite geometry, unresolved roots or overflow remain unavailable; no engine load-time repair or float-runtime identity is claimed. Existing encoded weighted fixtures and public CLI expectations now confirm usable weighted segments.

## Explicit stored extrapolation and empty-curve defaults - 0.119

Evaluation now accepts explicitly stored constant, linear, cycle, cycle-with-offset and oscillating pre/post modes, as raw bytes or exact qualified/unqualified Unreal enum names. Linear extrapolation uses adjacent stored key values. Cyclic endpoints retain the first/last-key boundary convention, track cycle count and preserve unknown evaluation gaps; unresolved arithmetic or counts above INT32_MAX stay unavailable. An explicit finite DefaultValue is usable only with a known empty key table; the MAX_flt incoming-default sentinel remains unknown. Omitted modes/defaults and RCCE_None are not repaired or inferred. Fifty-seven focused checks cover signed cycles, endpoints, offset, parity and missing settings, and encoded package tests now cover all five modes and defaults in three file profiles.
