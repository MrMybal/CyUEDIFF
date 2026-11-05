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
