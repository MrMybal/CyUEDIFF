# Core-only cooked animation consumer

Configure this installed example with `find_package(CyUEDiff CONFIG REQUIRED)` and `CMAKE_PREFIX_PATH` pointing to the installed SDK. It links only `CyUEDiff::Core` and needs no Unreal, Git or UI runtime.

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH=ABSOLUTE_SDK_PATH
cmake --build build --config Release
cooked_animation_consumer INPUT.utoc ANIMATION_ID GLOBAL.utoc CONTEXT.json SETTINGS_ID SKELETON_ID OUTPUT.json
```

All three package IDs come from the same input container in this minimal example. The Core API and CLI also accept separate explicit dependency sources. The context must bind exact Engine AnimSequence, AnimBoneCompressionSettings and Skeleton property schemas. Global script objects, package/public-export identity, registered codec selection and Skeleton GUID are verified before names or compressed samples are published. The explicit profile is UE 5.3 Windows and this example explicitly supplies Linear interpolation; a conflicting stored mode is refused.

The example checks every focused `sample_track` against the complete named frame, checks the lightweight catalogue cardinality and compares bounded endpoint key previews to exact `stored_key` queries. Output includes verified query counts. The result contains the native metadata plus numeric/named compressed bone-local samples at every integer and half frame. Output is limited to 10,000 frame-domain keys and 200,000 track samples. It does not borrow reference poses for unstored tracks, decode compressed curves or apply additive bases, retargeting, hierarchy or skinning. Choose a new private output path; the example refuses input containers/partitions/global tables/context/mapping as output.

`validate_cooked_animation.py` accepts an explicit `cyuediff.cooked-animation-validation/1` manifest. Each `cases` item supplies absolute `container`, `global`, `context`, `reference`, `skeletonReference` paths and `entry`, `boneSettingsEntry`, `skeletonEntry` IDs. References are independently exported by the optional original Unreal adapter: direct compressed-codec samples and a real Skeleton API export. Optional `protectedSources` entries contain absolute `path` and expected `sha256` for originals and companions.

```text
python validate_cooked_animation.py --consumer ABSOLUTE_EXE --manifest ABSOLUTE_MANIFEST --output NEW_PRIVATE_DIRECTORY
```

See [the complete contract](../../docs/COOKED_ANIMATIONS.md) in a source checkout, or the installed manual. Real assets, mappings and private reference outputs are not distributed with the SDK.
