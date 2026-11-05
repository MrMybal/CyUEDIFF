# Raw reference skeletons

The reader retains stored raw bone hierarchy and local reference transforms without launching Unreal. No engine-owned source or asset data is redistributed.

## Bounded native hierarchy and local reference pose - 0.83

Classic Skeleton exports with file versions UE4 522 / UE5 1013 expose a bounded raw reference-skeleton prefix: bone FNames, parent indices, optional editor export names, double-precision local transforms and the stored name/index map. Counts, parent order, duplicate identities, pose/map consistency, finite values, byte/text/work limits and cancellation are checked. Unsupported profiles and failures retain the entire native suffix opaque; successful reads keep all later retarget/SmartName/marker payloads opaque too. Native skeleton decoding can be disabled, with a separate cache profile. Full-package fixtures and every truncated native prefix are covered; the filtered prefix is additionally tested through the bounded archive adapter. No skinning, animation, virtual-bone rebuilding or engine defaults are evaluated.

## Named skeleton snapshots and structural diffs - 0.84

Skeleton snapshots key raw bones by their exact stored FName and use named parents, while optional sourceIndex fields retain inspection ordering. The comparison omits those indices, so sibling source reordering alone does not change the named projection; ordinary package diffs still retain serialized array changes. Local pose components, exported names, UObject properties and remaining native fingerprints stay visible. Missing native prefixes report incomplete, malformed host models fail, and object/bone/node/text/depth budgets and cancellation remain explicit. General package inspection adds skeletons and comparisons add skeletonChanges.

## Bounded affine reference-pose display data - 0.85

reference_pose exposes named joint positions by composing stored local translation/quaternion/scale matrices through the raw hierarchy. Signed scale and bounded unit-quaternion roundoff are handled explicitly; invalid rotations leave the affected joint and descendants unavailable. Iterative composition has joint/depth budgets and cancellation, and nonfinite derived values abort. The raw-reference-affine space is display data: it does not execute animation, skin vertices or emulate Unreal FTransform decomposition where nonuniform scale introduces shear. Stored local values remain unchanged and continue to drive the structural diff.

## CLI inspection, pose export and independent SDK consumer - 0.86

skeleton exports the stored named hierarchy; skeleton-diff compares two projections with exit 0 identical, 1 changed and 2 incomplete/error; skeleton-pose exports raw-reference-affine joint positions and accepts an explicit object path. Inspection remains successful when a projection is unavailable, with complete/diagnostics explicit. Shared project contexts and source/companion/hard-link output protections apply. An independent installed Core consumer demonstrates native package reads or an explicit host model, structural comparison and reference-pose composition without Unreal, CyUI or Git.

## Reader profile and scope

The current native prefix supports UE4 file version 522 with UE5 file versions 1004 or 1013. Cooked native data and other file profiles remain opaque; custom licensee packages require a dedicated backend. The native window starts with UObject's optional GUID marker, followed by raw bone infos, local transforms and the stored name/index map. Remaining native state stays fingerprinted and opaque. Three real engine-owned packages saved by UE 5.0 use profile 1004; modern profile 1013 has independently encoded fixture coverage. The reader does not rebuild virtual bones, evaluate animation, read skin weights or decode SkeletalMesh source/skin/LOD/render payloads beyond the reference prefix.

The GUID marker was added to the fixtures after checking actual engine packages; invalid markers and optional nonzero GUIDs have explicit regressions. Filtered editor export-name omission has bounded archive-prefix coverage, rather than a claimed real filtered-package corpus.

## Commands and SDK

    cyuediff skeleton Content/SK_Example_Skeleton.uasset
    cyuediff skeleton-diff before/Skeleton.uasset after/Skeleton.uasset
    cyuediff skeleton-pose Content/Skeleton.uasset --object SkeletonObject --out joints.json

Include cyuediff/skeleton.hpp for stored snapshots/diffs, or cyuediff/skeleton_pose.hpp for affine display positions. Inspection indices do not participate in named structural diffs. Native tail fingerprints do, so undecoded native changes remain detectable. An incomplete projection never reports equality. Comparisons also require fingerprints for nonempty opaque native ranges; disabling fingerprints cannot silently certify equality of those ranges. Pose joint readiness is separate from raw stored-projection completeness. Skeleton pose data is owned and can be consumed without a renderer.

## CyUI hierarchy review

The Skeleton tab displays raw bone identities and the union of stored parent connections, with typed before/after local transforms and metadata. Bone outlines follow changes to stored bone data, rather than animation or derived runtime state. Node search, changed-node navigation, canvas framing and selected bone/property previews use the shared graph inspector. Exact object and bone names survive source-index reordering and explicit workspace save/reopen; missing identities do not select unrelated data. The canvas caps at 4,096 bones; full stored data remains available through Properties/Core. Layout condenses depth beyond 32 columns while keeping visible parent links. English is the default and French is built in.

## CyUI hierarchy review and verified engine package compatibility - 0.87

The desktop Skeleton tab and embeddable SkeletonView own normalized snapshots, show the union of stored parent relationships and compare typed local transforms. Search/change navigation and object/bone/property/canvas state survive explicit workspace restart. The canvas is bounded independently of Core data and locale changes update prepared pin labels. Native parsing now also covers the source-verified LWC file profile 1004 used by three real engine packages, including the required optional UObject GUID prefix. Remaining native tails stay opaque; skinning and animation are not implemented.

## Native SkeletalMesh reference prefixes - 0.88

The bounded native reader accepts exact Engine.SkeletalMesh objects for the previously verified UE5 file profiles, with explicitly bounded Editor/Core/Rendering custom versions. It retains UObject GUID presence, strip bytes, double imported bounds, ordered material references and slot labels, optional imported labels, UV flags/densities, raw bone infos, local reference transforms and the checked name map. Materials and bones share an aggregate budget. Unsupported versions, invalid booleans/references/finite values and every truncated prefix fall back atomically to opaque bytes; cancellation propagates. Source models, skin weights, LOD/render data and animations remain opaque. Three local engine-owned UE5.0-saved meshes independently reached the checked prefix; assets and engine source are not redistributed. Native tests pass 485 checks.

## SkeletalMesh named hierarchy and metadata projections - 0.89

Skeleton snapshots, named structural diffs and affine poses now accept both Skeleton and SkeletalMesh exports. Stored bounds, strip bytes and material metadata are projected with explicit budgets; the material array remains ordered because undecoded mesh sections can reference numeric slots, including duplicate labels. Per-object referenceComplete describes the checked raw hierarchy independently of complete, which still accounts for unrelated UObject properties. A valid affine pose can therefore be available while a full structural comparison remains incomplete. Three engine-owned meshes expose 71 stored bones and 71 ready joints; their unrelated partial properties are retained and reported, never treated as complete. Projection tests pass 29 checks.

## Named affine reference-pose displacement comparison - 0.90

The renderer-independent compare_reference_poses API pairs exact bone names across independently selected objects and emits before/after readiness, positions, named-parent changes, displacement vectors and finite magnitudes. Added or removed joints have no invented displacement. A finite nonnegative position tolerance affects derived positions only; parent changes stay visible. Ancestor movement affects descendant positions even when their stored local transforms are unchanged. Incomplete/unavailable joints cannot establish equality, and prepared input identity, text, joint/union budgets, cancellation and numeric overflow are checked. This comparison covers affine joint positions, not rotations, skinning, runtime animation or unrelated native tails.

## CLI displacement reports and installed SDK validation - 0.91

skeleton-pose-diff compares two package reference poses with independent --before-object/--after-object selection and finite --position-tolerance. Exit codes are 0 for identical positions/parents within tolerance, 1 for changes and 2 for incomplete/error. The existing skeleton commands now accept SkeletalMesh assets. Tests cover native mesh inputs, bad tolerances/selections, incomplete data, source/companion output protection and JSON output. The installed independent Core consumer exercises pose comparison without Unreal/CyUI/Git. The local corpus validator records object classes, raw-reference readiness and reference-only self-comparisons separately from full stored-property completeness.

## Synchronized CyUI reference-pose review - 0.92

The Skeleton tab now switches between the stored hierarchy graph and a bounded 3D affine-reference preview. Before/after panes share a camera and stack at narrow widths. Joint picking synchronizes exact bone selection and typed stored/derived details; displacement vectors and magnitudes are explicit. Preview colors/filter follow derived position or parent changes, while hierarchy outlines continue to follow stored local data. The preview caps at 4,096 joints and rejects display coordinates beyond its camera range without modifying Core values. Missing rotations/ancestors stay unavailable. Pose mode, camera and selected-property state persist through explicit workspace save/reopen. All drawing consumes prepared owned positions, with no package I/O or pose evaluation per frame. Animation, skinning and SkeletalMesh source/render geometry remain unimplemented.

The SkeletalMesh prefix additionally requires stored Editor custom version 8–40, Core version 3–4 and Rendering version 10–45. Missing or out-of-range versions remain opaque, including future layouts. The material array keeps numeric slot order even when slot labels are unique. These bounds are a supported serialization contract, rather than certification of every engine release in between. Real input verification currently covers the three UE5.0-saved meshes; independent fixtures cover the modern file profile.

    cyuediff skeleton Content/Character.uasset
    cyuediff skeleton-pose-diff before/Character.uasset after/Character.uasset --position-tolerance 0.001
    cyuediff skeleton-pose-diff before.uasset after.uasset --before-object RigA --after-object RigB

Reference displacement units are the stored coordinate units. A tolerance applies to the Euclidean distance; equality at the tolerance is accepted. Position equality does not establish equal rotations, scale, skinned geometry, animation or undecoded bytes. Use skeleton-diff or the general package diff for the stored structural data and remaining native fingerprints.

## Bounded native Skeleton retarget sources - 0.93

The optional Skeleton continuation reads the counted retarget-source map, distinct stored pose labels, bounded local transform arrays, supported hard/soft mesh references and the Skeleton GUID. Nonempty tables require explicit AnimPhys versions 0-17; the soft-reference transition and verified file profiles use their stored layouts. Filtered reference omission is preserved. Unsupported or damaged details leave the already checked raw reference hierarchy available and keep the complete continuation opaque; cancellation still propagates. Raw bones, material arrays and retarget arrays share work/byte/text budgets, and the new read option participates in cache identity. The native suite passes 618 checks. Real verification covers three empty engine-owned tables; nonempty tables have independent binary fixture coverage.

## Named retarget-source projections and structural differences - 0.94

Stored retarget tables now project exact source keys separately from display pose labels and source mesh references. Matching transform counts bind to exact stored raw bone names; source-map and sibling bone reorderings are normalized for named differences. Count mismatches remain ordered, explicitly unbound arrays, without virtual-bone reconstruction or invented correspondences. Skeleton GUIDs, coverage, bounded source/transform/text/node work and cancellation remain explicit. Structural equality still includes unbound values and opaque native fingerprints; this does not certify an available derived pose. Independent projection tests pass 16 checks.

## Explicit affine composition of stored retarget poses - 0.95

ReferencePoseOptions can select an exact retarget-source key; the empty selection retains the raw reference pose. Affine joint composition uses only the selected stored local transforms and the raw hierarchy. Missing keys fail explicitly; unread tables and unbound counts return an incomplete empty pose without substituting raw transforms. Invalid quaternion and ancestor propagation, depth/joint budgets and cancellation match the existing pose contract. Comparisons inherit the before source by default and can explicitly select another source or the raw pose after; reports retain both source identities. Independent retarget pose tests pass 14 checks, plus existing pose/comparison regressions.

## Retarget pose CLI selection and installed SDK contracts - 0.96

skeleton-pose accepts --retarget-source, while skeleton-pose-diff accepts independent --before-retarget-source and --after-retarget-source. An omitted after source inherits the before source; an explicitly empty after source selects raw transforms. JSON reports retain selection identities, and incomplete source correspondence keeps exit code 2 for comparisons. CLI regressions cover source keys versus display labels, sibling/source-map reordering, position tolerance, bad flag/selection cases and source/companion/hard-link output protection. The installed independent Core consumer demonstrates a named source and comparison against raw transforms. The retarget CLI suite passes 25 checks.

## Independent CyUI retarget-source review and saved selections - 0.97

The Skeleton pose preview offers independent before/after stored retarget-source selectors and an explicit raw-reference choice. Owned joint positions are prepared once while binding, with aggregate Core bounds; draw calls perform no decoding or pose composition. Typed bone details use the selected source values. Missing/deleted source keys remain selected and unavailable after workspace restore, without displaying raw replacements; count mismatches remain unbound. Source choices, exact bone selection, shared camera and property state survive explicit workspace save/reopen, with English/French labels and all five themes.

## Retarget source contracts

The optional native continuation applies to exact Skeleton exports after their checked raw reference prefix. It stores the source-map key, PoseName, ordered local transforms, the editor source mesh reference when serialized, and the distinct Skeleton GUID. The AnimPhys transition at version 15 changes a stored object reference into a soft path. Supported nonempty tables require an explicit AnimPhys version in 0–17, together with the existing checked package profiles. Empty tables do not need a source-reference layout that they never use. Filtered source-reference omission has archive-prefix fixture coverage. Remaining SmartName, marker and other bytes stay opaque.

native_skeleton_retargeting can disable only this continuation; native_skeletons still controls the underlying hierarchy reader. The cache separates those profiles. Failure to decode the continuation keeps the checked raw hierarchy available, resets to its boundary and fingerprints the complete remaining suffix. A corrupt source table cannot supply a partial retarget model. This is separate from a structurally valid table whose transform count differs from the raw bone count: its values are kept as an explicitly unbound ordered array.

Only equal transform/raw-bone counts receive named correspondence. The reader does not rebuild virtual bones, repair stale retarget records, synthesize default transforms, load a referenced mesh or execute a retargeting algorithm. PoseName is a stored label, not the source-map identity. Native references are retained as data.

    cyuediff skeleton-pose Skeleton.uasset --retarget-source T_Pose
    cyuediff skeleton-pose-diff before.uasset after.uasset --before-retarget-source T_Pose
    cyuediff skeleton-pose-diff before.uasset after.uasset --before-retarget-source T_Pose --after-retarget-source A_Pose

ReferencePoseOptions.retarget_source is the exact source key; an empty value selects the stored raw pose. ReferencePoseDiffOptions.after_retarget_source is optional: absent inherits the before selection, while an explicit empty string selects raw after. JSON retains both selections. Missing keys fail explicitly, while unavailable tables and unbound counts yield incomplete empty poses. Neither case silently substitutes raw values. If one pose is unread, unmatched joints report unavailable rather than an invented removal. Known absent asset sides may still display additions/removals.

These are affine positions computed from the selected stored local transforms in the existing raw-reference-affine display space. They do not evaluate runtime retargeting, animation or skinning. Three local engine Skeleton assets currently verify empty source tables and their GUIDs; nonempty tables are independently encoded fixtures, rather than a claimed real nonempty retarget corpus.

## Stored native animation curve metadata - 0.98

Skeleton native decoding now continues after the retarget table/GUID through bounded SmartName containers for the explicit UE5 file profiles. Nonempty containers require Framework 15-37 and AnimPhys 10-17; older UID/GUID mappings and unknown custom versions remain opaque. Stored material/morph-target flags, ordered bone-name links (including unresolved/None names), and the byte maxLOD when present (AnimPhys >= 12) are retained without runtime defaults or curve evaluation. Empty container maps do not require unused entry versions. A separate metadata opt-out participates in the package-cache identity. Curve failure rolls back only the curve suffix, cancellation propagates, and all native stages share work/text/byte limits. Independently encoded fixtures cover both file profiles, absent LOD, reorder, malformed entries, every truncated byte and cancellation. Layout references: local UE 5.5 Skeleton.cpp, SmartName.cpp, AnimCurveMetaData.cpp and BoneReference.cpp; no engine source or assets are redistributed.

## Stored synchronization marker names - 0.99

After checked curve containers, Skeleton decoding now reads marker strip flags and ordered FName arrays for explicit Anim versions 2-15. Versions 0-1 record not-stored coverage; missing/future versions stay opaque. Editor-stripped names remain absent, unknown global strip bits are rejected, and class flags are retained. Repeated names and source order survive in native data. A malformed/truncated marker suffix preserves all checked curves, retarget sources and raw bones. Independent fixtures cover version boundaries, strip flags, invalid FNames, aggregate budgets, every truncated byte and cancellation. The three locally available real Engine Skeleton assets reach this continuation without modifying their input files.

## Named stored animation metadata comparison and CLI - 0.100

The bounded skeleton projection now includes exact container/curve identities and marker-name occurrence counts; raw native marker order remains available in the generic serialized snapshot. A separate skeleton_animation_snapshot/compare_skeleton_animation API and skeleton-animation/skeleton-animation-diff CLI provide a strictly scoped comparison independent of unrelated UObject property or native-tail coverage. Stored curve flags, ordered bone links and present LOD values are validated for host models too. Stripped/unread tables never establish equality; explicit pre-marker versions retain not-stored coverage. Dedicated container, curve, link, marker, JSON-node and text limits share each projection. CLI exit codes are 0 identical, 1 changed, 2 incomplete/error, with existing input/companion output guards and separate project contexts. The scoped result does not certify animation samples, runtime values, unrelated properties or remaining native bytes.

## Animation metadata desktop inspection - 0.101

The Skeleton tab now offers a separate animation-metadata mode with an exact container/curve and marker union, search, changed-only filtering, confirmed-change navigation and typed per-entry comparison. Marker occurrence counts and curve flags/links/LOD use the same named Core projection. Missing entries opposite an unread/stripped table remain unknown and are inspected as stored single-side values instead of confirmed additions/removals. The display union is capped at 4,096 entries; source data remains available through the Core/CLI. Explicit workspace state retains mode, selected exact entry, search, changed filter and property details; invalid nested state or conflicting view modes is rejected before mutation. UI strings are available in English and French across the five CyUI themes, without translating stored asset names. No file access occurs during drawing.

## Installed SDK and corpus verification for stored animation metadata - 0.102

The independent Core consumer now demonstrates package-based curve/marker projection, a changed curve LOD with reordered markers, and incomplete stripped-marker comparison. Distribution validation checks those contracts after building against the extracted installed SDK. The read-only corpus validator counts curve containers/entries/links, marker multiplicities, scoped metadata completeness, expected self-comparison status and fully consumed Skeleton native bodies, while preserving source and companion hashes. The local engine corpus contains three verified empty curve/marker tables and six checked raw references; nonempty metadata and malformed/stripped cases are covered by independently encoded package fixtures. Animation samples, runtime curve values, timelines and native SkeletalMesh skin/LOD decoding remain outside this scope.

### Stored animation metadata commands and SDK

```shell
cyuediff skeleton-animation Content/Skeleton.uasset --out metadata.json
cyuediff skeleton-animation-diff before/Skeleton.uasset after/Skeleton.uasset
```

`skeleton_animation_snapshot(package, options)` and `compare_skeleton_animation(before, after, options)` use `SkeletonOptions` and return the `cyuediff.skeleton-animation/1` and `cyuediff.skeleton-animation-diff/1` contracts. Containers and curves retain exact case-sensitive names; JSON-pointer tokens escape `~` and `/`. Curve bone links retain stored order and unresolved names. An absent `maxLOD` means no value was serialized in that profile; no runtime default is inserted. Sync marker names map to occurrence counts rather than inferred timing or order. Stripped, disabled, unsupported or corrupt tables cannot establish scoped equality. Known pre-marker versions have explicit `not-stored` coverage. A scoped identical result certifies only these stored metadata fields, independently of raw pose changes, unrelated properties and native suffixes. The generic serialized snapshot retains native array order and remaining byte fingerprints.

The independent installed `examples/skeleton_consumer` demonstrates package-based projections and a host model with a changed curve LOD, reordered markers and an incomplete stripped-table comparison. It links only `CyUEDiff::Core`.
