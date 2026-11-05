# Stored animation source tracks

No Unreal process or redistributed engine files are needed.

## Bounded raw animation key arrays - 0.108

Loose tagged RawAnimSequenceTrack values now decode the UE5 Release 39–51 bulk serializer for verified UE4 522 / UE5 1004 or 1013 profiles. Three element-size/count-prefixed float arrays retain position, quaternion XYZW and scale keys. Scalar work shares the property reader budget; wrong strides, counts, non-finite components, truncation and unknown profiles roll back the whole property to opaque bytes. Older custom versions retain tagged member decoding. Tests cover both file profiles, custom-version boundaries, atomic rollback, aggregate limits and cancellation. Engine source was consulted locally; no engine code or assets are included.

## Named animation source projection and comparison - 0.109

The independent animation.hpp API projects exact Engine.AnimDataModel objects into bone-name-keyed position, quaternion XYZW and scale streams, with explicit model selectors for pair comparisons. Storage order and selected object path differences do not hide per-bone changes. Legacy reflected and recent bulk tracks share the same model; Quat4f reflected values now decode as four float components. Duplicate or missing names retain indexed records and incomplete coverage. Omitted frame-rate components, opaque channels and missing input remain unknown. Limits cover model/track/key counts, copied nodes, text and depth, with cancellation. Two engine tutorial assets now decode all 68 legacy source tracks, including rotations; no runtime animation evaluation or defaults are implied.

## Stored source frames, CLI and SDK example - 0.110

animation_frame reads a selected integer source-key frame in bone-local space. Each channel must store one constant key or exactly NumberOfKeys entries. Empty, missing or mismatched channels and non-unit quaternions stay unavailable. No interpolation, hierarchy composition, root motion, retargeting, skinning or default transform is evaluated. Out-of-range frames fail; seconds are emitted only when both rational frame-rate terms are stored. New animation, animation-diff and animation-frame CLI commands share project resolution and protected output handling. Pair commands accept independent model object selectors and retain exit codes 0 identical, 1 changed, 2 incomplete/error. The separate examples/animation_consumer links only the installed Core SDK. Tests include two actual binary fixture profiles, 18 frame/file checks and 46 public CLI checks. The two real animations yield 68 ready local tracks at sampled frames while omitted frame-rate denominators remain explicit; their full metadata comparisons correctly report incomplete.
