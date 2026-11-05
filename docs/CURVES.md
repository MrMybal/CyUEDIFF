# Stored rich curves

No Unreal process or redistributed engine files are needed.

## Bounded native rich-curve keys - 0.113

RichCurveKey values decode their packed three byte modes and six float32 components for verified UE4 522 / UE5 0, 1004 or 1013 loose tagged profiles. The reader retains raw interpolation, tangent and tangent-weight bytes without Unreal load-time repair. Exact property size, finite values, shared work bounds and cancellation are checked; malformed/unknown layouts roll back atomically. Fifteen independently encoded tests cover all profiles, invalid values, raw unknown modes and aggregate limits. The actual ContentIntroCurve asset now exposes three stored cubic keys and their tangents. No engine assets or source are redistributed.
