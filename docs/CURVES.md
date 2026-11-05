# Stored rich curves

No Unreal process or redistributed engine files are needed.

## Bounded native rich-curve keys - 0.113

RichCurveKey values decode their packed three byte modes and six float32 components for verified UE4 522 / UE5 0, 1004 or 1013 loose tagged profiles. The reader retains raw interpolation, tangent and tangent-weight bytes without Unreal load-time repair. Exact property size, finite values, shared work bounds and cancellation are checked; malformed/unknown layouts roll back atomically. Fifteen independently encoded tests cover all profiles, invalid values, raw unknown modes and aggregate limits. The actual ContentIntroCurve asset now exposes three stored cubic keys and their tangents. No engine assets or source are redistributed.

## Named source lanes and stable key-time differences - 0.114

The curves.hpp Core API projects exact CurveFloat, CurveVector, CurveLinearColor and AnimDataModel float curves. Fixed vector/color channels preserve missing lanes, while named animation curves use exact stored CurveName values; unresolved legacy smart-name identities are not inferred. Keys retain every stored numeric/mode field, optional source indices and ordered identities based on exact finite time values. Insertions do not replace later keys. Duplicate/missing times or curve names retain indexed records and incomplete coverage; unsorted times remain visible. Explicit empty arrays differ from omitted defaults. Stored settings and additional rich-curve fields remain inspectable, with aggregate owner/lane/key/node/text/depth limits and cancellation.
