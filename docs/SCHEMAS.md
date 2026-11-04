# Unversioned property schemas

Version 0.17 adds `<cyuediff/schema.hpp>` to `CyUEDiff::Core`. Hosts can load explicit USMAP mappings and decode bounded regions of unversioned property data without Unreal. Zen readers use these mappings only when an exact class binding supplies the property start offset.

## Mapping input

`UnversionedSchemas::parse` accepts bytes; `read` accepts a local file. The immutable result exposes schema definitions, qualified module identities, inheritance, expanded static-array slots, enum metadata, recorded versions and the SHA-256 of the original mapping file. No engine, plugin or filesystem schema discovery takes place.

Supported USMAP base layouts are versions 0–4: optional package/custom versions, long names, large enum tables and explicit enum values. Names are non-NUL ASCII; enum identifiers and signed explicit values are preserved. Duplicate qualified identities, ambiguous references, missing bases, inheritance cycles, overlapping slots, invalid descriptors and size mismatches fail explicitly.

Version 0.18 reads the CEXT layout version 0 and four extensions:

| Extension | Preserved data |
| --- | --- |
| PPTH v0 | Schema and enum module paths, qualified as `/Script/Module.Type` or `/Game/Package.Type` |
| MODL | Legacy module path table and 8/16-bit schema module indices, inside CEXT or behind the legacy signature |
| EATR v0 | Enum flags, schema kind and flags, raw serialized property-flag sequence |
| ENVP v0 | Replacement enum label/value pairs, including signed 64-bit values |

Known extension payloads must consume exactly their declared window and have matching table counts. Unknown extension IDs/versions/layouts, repeated IDs, combined PPTH/MODL identity sources and trailing bytes fail explicitly. The implementation does not discard failed or unknown extensions and continue with weaker identity information.

Qualified schema and enum names resolve exactly. A short name is accepted only when unique across the mapping. Homonyms with distinct module paths are retained as separate definitions; an unqualified ambiguous base/struct/enum reference is rejected. Matching a type to the referring module would be a guess, so the reader does not do that. A mapping whose base references are ambiguous cannot be loaded; other ambiguous value-type references fail when decoded.

`schema(name)` and `enumeration(name)` return immutable definitions, with basename, module and qualified `path`. `super` is normalized to its resolved identity. JSON enum keys use qualified identities when available, and schema/enum metadata retain their original basenames. EATR property flags are exposed as an uninterpreted sequence, with lossless hexadecimal strings in JSON; no association to sparse property descriptors is inferred. EATR metadata never enables a native/reflected serializer automatically.

Uncompressed mappings need no codec. Compressed methods are delegated to a host-provided `SchemaReadOptions::decoder` by name (`Oodle`, `Brotli`, `Zstd`), with an exact output-size check and bounded allocation. The existing numeric codec bridge supplies Oodle/LZ4 only; the CLI's `--codec` can therefore supply Oodle, while Brotli/Zstd need a suitable C++ callback. No codec or proprietary mapping is bundled. Callback tests validate dispatch/bounds, not these compression implementations.

The base layout was cross-checked against the primary [UAssetAPI USMAP reader](https://github.com/atenfyr/UAssetAPI/blob/master/UAssetAPI/Unversioned/Usmap.cs). The Core parser is an independent C++ implementation and has no UAssetAPI runtime dependency.

## Decoded values

| Input | Behavior |
| --- | --- |
| Header fragments | Skip/count/last fields and mixed zero-mask fragments; 8/16/32-bit-word mask storage |
| Scalar values | Signed/unsigned integers at their declared widths, finite float/double, byte and boolean |
| Enum | Declared integer storage and enum type identity; no inferred display label |
| Name / string | Package FName index and numeric suffix, ASCII narrow FString and validated UTF-16 FString |
| Object / interface | Signed 32-bit package index through an explicit host resolver; interface addresses are not reconstructed |
| Text | Modern localized, empty/invariant and unresolved string-table histories; explicit standalone layout |
| Delegate / multicast | Persistent object/function bindings; original order, duplicates and null targets |
| Weak object | Persistent package object reference; no runtime pointer validity |
| Lazy object | Four serialized GUID words, retained as an unresolved stable identifier |
| Soft object | Explicit UE5 top-level-name layout: package FName, asset FName and subobject FString; no referenced asset is opened |
| Array / static array | Bounded dynamic element counts; expanded static slots retain array indices |
| Map / set | Stored removals and additions/replacements keyed by canonical JSON identity; explicit whole-container replacement state |
| Reflected struct | Nested unversioned header only when explicitly listed in `reflected_structs` |
| Explicit native struct | Host-bound math/color/GUID wire layouts; component values and layout provenance |
| Unsupported/unbound native type | Whole bound property read fails; Zen retains the complete opaque export and a reason |

Supported text histories and persistent delegate/weak values are described below. Legacy asset references, field paths and newer string values still require additional readers. Native structs such as vectors/transforms are not inferred from their schema name. A zero-masked unsupported scalar or unbound native struct also fails explicitly. Empty supported arrays/maps/sets need no bytes from their absent element types.

Only present properties are returned. A skipped property can inherit an archetype/default value; it is not synthesized as zero. Present zero-masked values carry `serializedZero:true`, and absent properties do not appear. Keys include the declaring schema identity (qualified when available) and property name, plus `[index]` for static arrays, so inherited property names stay distinct.

`decode_unversioned(bytes, offset, size, schemas, schema_name, options)` returns properties, consumed bytes and work/text counters. It throws on unsupported or invalid input, with no partial result. The input window can include trailing native data: `consumed_bytes` describes only the property stream. Callers retain the original bytes and decide what follows.

Default standalone limits are 100,000 value/fragment work units, depth 48 and 4 MiB decoded text. Header traversal, nested values and zero-masked values consume work. Optional `charge` callbacks let hosts enforce a shared enclosing-operation budget, including failed reads. Mapping input/expansion defaults to 64 MiB, one million combined slots/descriptors/enum entries and depth 48. Mapping names, resolved identities and expanded static-array type/name copies also consume a separate 64 MiB text budget (`max_text_bytes`); expanded type copies consume work units. Limits bound input/work, not total process heap use. Cancellation applies during file reading, parsing, hashing and property traversal; a decompression callback must implement its own interruption if needed during the call.

## Cooked references

Version 0.20 adds `InterfaceProperty`, `LazyObjectProperty` and the modern `SoftObjectProperty` layout. Interface values retain their resolved/null object reference; no native interface address is inferred. Lazy references retain four GUID words as `kind:lazy-object-guid` and a 32-digit hexadecimal `guid`; they are not resolved to an object. Soft paths retain `packageName`, `assetName` and `subPath` separately with `kind:soft-object-path`.

Standalone readers must select `SoftPathLayout::ue5_top_level_names` for nonzero soft references. Zen's supported UE 5.3/5.5 profiles select that layout explicitly. Older combined-name/index-based soft paths, `AssetObjectProperty` and raw weak-pointer index/serial layouts remain unsupported. Persistent weak package references are described below. Zero references consume no payload and retain their explicit zero state. References never trigger package lookup, asset loading, path rewriting or archetype reconstruction. Modern layouts are tested with synthetic mappings and bounded package bytes; matching-build real schema coverage is still pending.

## Map and set values

Version 0.19 reads the ordinary unversioned Map/Set layout: a signed 32-bit removal count, removed keys/elements, an entry count and stored pairs/elements. Element types come from the explicit USMAP descriptor. Supported nested arrays, collections and explicitly bound reflected structs use the same bounded recursive reader.

```json
{
  "kind": "stored-map-delta",
  "replace": false,
  "removed": {"\"OldKey\"": "OldKey"},
  "entries": {"\"Count\"": {"key": "Count", "value": 42}}
}
```

Set deltas use `kind:stored-set-delta`, and each `entries` value is the element itself. These are stored operations; the reader does not apply them to unknown inherited/default contents. Removal and re-addition of one identity are retained in separate buckets. A duplicate identity within either bucket fails with `schema-identity`, discarding the entire bound property read in Zen and retaining its full opaque body hash.

A zero-masked map/set consumes no count or element bytes and reports `replace:true` with empty buckets: the explicitly serialized zero clears the container. An ordinary empty nonzero delta reports `replace:false`; it must not be confused with that clear operation. The UE 5.5 map layout also accepts a `-1` removed-key count to replace the whole map. Standalone hosts opt into this using `UnversionedReadOptions::allow_map_replacement`; it defaults to false. Zen enables it for the explicit `ue5.5` profile only. The marker is not certified for `ue5.3` and falls back to opaque there. Other negative counts and a negative set removal count are rejected.

Canonical identities use compact, sorted-object JSON with exact string escaping and unchanged decoded values. This normalizes entry iteration order; it does not reproduce Unreal's runtime equality/hash rules for every type, normalize name case, coalesce floating-point signed zeros or reconstruct native struct defaults. Nested reflected keys preserve their serialized-property projection. Object keys use the host's resolved reference identity. No runtime container evaluation is performed.

Identity construction charges its output text before allocating it and consumes shared work/depth budgets while traversing the key. Removal/entry counts, element decoding, nested containers, duplicate detection and identity construction are cancellable. Canonical key strings are included in the decoded-text budget. A failed read still consumes enclosing work/text budget.

Unversioned collection values appear in the existing normalized property snapshots. A value edit produces a path under `/properties/.../value/entries/KEY/value`; JSON-pointer tokens are escaped. Reordering entries leaves this property projection unchanged, while the complete `/opaque/sha256` fingerprint still changes. Such a package remains `status:changed` because binary evidence is retained; it is not reported as identical.

## Explicit Zen context

The CLI reads a JSON context through `--schema CONTEXT.json`. `--after-schema` selects a different context for `zen-diff`; omission reuses the before context. Context paths use UTF-8, and a relative USMAP path resolves beside the context file.

```json
{
  "format": "cyuediff.zen-schema/1",
  "profile": "ue5.3",
  "usmap": "build.usmap",
  "sha256": "REPLACE_WITH_THE_USMAP_SHA256",
  "bindings": {
    "/Script/Example.ExampleAsset": { "schema": "/Script/Example.ExampleAsset", "offset": 0 }
  },
  "reflectedStructs": []
}
```

The example offset is illustrative: the host must determine the property start from the actual class serializer belonging to that build. The reader never searches an export for something resembling a header. Native prefixes and trailing bytes remain covered by the complete export fingerprint. Qualified class paths and explicit schema names avoid ambiguous basename matching. Unresolved script IDs do not match these bindings; provide the build's global script table when needed.

```powershell
cyuediff usmap build.usmap --out mapping-info.json
cyuediff zen-inspect Game-Windows.utoc CHUNK_ID --cooked-profile ue5.3 --script-objects global.utoc --schema context.json --out package.json
cyuediff zen-diff before/Game.utoc CHUNK_ID after/Game.utoc CHUNK_ID --cooked-profile ue5.3 --script-objects before/global.utoc --after-script-objects after/global.utoc --schema before/context.json --after-schema after/context.json --out diff.json
```

Use the `usmap` command's `sha256` field in the context. This fingerprint detects selecting a different mapping file; it does **not** prove the mapping matches the cooked build. Even a wrong schema can parse plausible values, so successful reads explicitly report `mappingVerified:false` and `defaultsResolved:false`. Matching profiles and recorded engine versions are checked, but custom versions/build provenance remain the host's responsibility. No default/archetype reconstruction is performed.

Unknown fields, duplicate keys, invalid offsets, unsupported profiles and fingerprint mismatches are rejected. Context JSON is limited to 2 MiB and depth 48. Output guards protect both context files and USMAP files, including hard links, as well as container/codec inputs.

A bound export records `propertyRead.status`, schema, relative offset, consumed bytes and typed properties. Invalid data or unsupported types yield `status:opaque` and an error without partial properties. Cancellation and budget exhaustion fail the operation instead of becoming opaque fallbacks. Zen shares its text budget and a one-million-unit default property-work budget across exports, including failed reads.

`propertiesDecoded` means at least one export property region was read, including a valid empty header. `propertyExportsDecoded` and `propertyExportsFailed` show exact counts. Catalogue `complete` describes package traversal, not complete semantic coverage. Normalized snapshots include successfully decoded properties; property diffs identify their names/values and also retain `/opaque/sha256` changes. Schema-context fingerprints are recorded per side and participate in `contextChanged`. Full native payload decoding and external bulk comparison remain unsupported.

## Core integration

```cpp
#include <cyuediff/zen.hpp>

auto context = std::make_shared<cyuediff::ZenSchemaContext>();
context->profile = cyuediff::ZenProfile::unreal_5_3;
context->schemas = std::make_shared<cyuediff::UnversionedSchemas>(
    cyuediff::UnversionedSchemas::read("build.usmap"));
context->bindings["/Script/Example.ExampleAsset"] = {"/Script/Example.ExampleAsset", 0};
// Only list structs whose serializer is known to use reflected properties.
context->reflected_structs.insert("/Script/Example.ExampleRecord");
cyuediff::ZenReadOptions options;
options.profile = context->profile;
options.schema_context = context;
// Supply script_objects and a ContainerReadOptions codec as required by the build.
// Publish the context as immutable; do not mutate it while readers use it.
```

The installed-SDK `examples/schema_consumer` reads a minimal `Consumer` mapping and a raw property buffer without Git/UI/Unreal. Schema/native-property coverage is validated using original synthetic fixtures, including independently generated SDK-consumer bytes with CEXT/PPTH/EATR/ENVP. The supplied CyTrace corpus has no matching USMAP; its 3,507 packages and 7,502 exports continue to validate object tables and opaque fallback, **not real mapped values**. Matching-build mapping and property-offset validation remain required before certifying any cooked class.

## Reusable context manifests — 0.29

`<cyuediff/schema_context.hpp>` exposes `ZenSchemaManifest::read(path, options)` and `manifest.load(schemaOptions)` in the Core. The first operation reads and validates only the JSON manifest, records its SHA-256, resolves the USMAP path relative to the manifest and exposes both absolute input paths. It does not open a USMAP or load a codec. This lets hosts protect inputs and show provenance before decoding.

`load` reads the explicit mapping, verifies its expected fingerprint and resolves every schema/reflected-struct identity. It returns an immutable `ZenSchemaContext` usable by `ZenReadOptions`. Fingerprints certify selected bytes, not a matching cooked build. Contexts remain explicit per side. The CLI uses this shared implementation.

Manifest defaults are 2 MiB serialized/text bytes, 64 KiB per string, depth 48 and 100,000 combined bindings/reflected-struct entries. Invalid JSON/UTF-8, duplicate/unknown fields, missing fields, embedded NUL strings, invalid profiles, non-unsigned offsets and malformed SHA-256 values fail explicitly. Reading and mapping loading accept cooperative cancellation; mapping decoder and bounds are supplied through `SchemaReadOptions`. `to_json()` describes manifest-only provenance and never claims verified build compatibility.

```cpp
#include <cyuediff/schema_context.hpp>
auto manifest = cyuediff::ZenSchemaManifest::read("build/schema.json");
// Protect manifest.source_path() and manifest.mapping_path() against output writes.
cyuediff::SchemaReadOptions mappings;
auto context = manifest.load(mappings);
cyuediff::ZenReadOptions options;
options.profile = manifest.profile();
options.schema_context = context;
```


## Explicit native struct values - 0.44

Standalone readers can bind a schema identity to `NativeStructLayout` through `UnversionedReadOptions::native_structs`. The supported wire layouts are `vector2-f32/f64`, `vector3-f32/f64`, `vector4-f32/f64`, `quat-f32/f64`, `rotator-f32/f64`, `transform-f32/f64`, `guid`, `linear-color-f32`, `color-bgra8`, `int-point` and `int-vector` (17 tokens; each precision alternative is a separate token). `parse_native_struct_layout` and `native_struct_layout_name` convert between tokens and enum values.

Bindings resolve exact qualified identities or unique short mapping names. Duplicate aliases, unknown layouts and a native/reflected conflict fail before reading the property stream. Nothing is inferred from names such as `Vector` or from EATR metadata. Float/double selection describes serialized components, not the size of an in-memory C++ type. Unreal's float/double aliases changed with [Large World Coordinates](https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-project-conversion-guidelines-in-unreal-engine-5); the host must select the layout actually written by its build.

Vectors/quaternions/rotators return component arrays (XY[ZW], quaternion XYZW, rotator pitch/yaw/roll). A transform returns `rotation`, `translation` and `scale` arrays, in that serialized order, without SIMD padding. Linear colors retain RGBA float values; `color-bgra8` retains wire BGRA bytes. Integer points/vectors retain signed 32-bit components. GUIDs retain four serialized words as 32 lowercase hexadecimal digits. Property metadata records `nativeLayout` and resolved `typeName`.

No quaternion normalization, transform evaluation, color conversion or default reconstruction occurs. A zero-masked transform returns zero components, including zero scale and quaternion, rather than identity. Non-finite components and truncated windows fail the complete property read. Nested arrays/maps/sets use the same decoder, shared work/text/depth bounds and cancellation. Source ordering was checked against the local UE 5.5 Core math serializers; these original synthetic fixtures do not certify a matching real cooked build. Zen/context integration is available through the version-two context below.

```cpp
cyuediff::UnversionedReadOptions options;
options.native_structs["/Script/CoreUObject.Vector"] = cyuediff::NativeStructLayout::vector3_f64;
options.native_structs["/Script/CoreUObject.Transform"] = cyuediff::NativeStructLayout::transform_f64;
auto decoded = cyuediff::decode_unversioned(bytes, offset, size, schemas, "MyAsset", options);
```


## Native Zen schema contexts - 0.45

Manifest format `cyuediff.zen-schema/2` adds a required `nativeStructs` object mapping schema identities to the explicit layout tokens above. Format one remains supported and rejects this new field. Existing contexts without native bindings retain their context fingerprint. Native layout choices participate in fingerprints and `contextChanged`; changing precision cannot silently produce an identical semantic comparison.

```json
{
  "format": "cyuediff.zen-schema/2",
  "profile": "ue5.3",
  "usmap": "build.usmap",
  "sha256": "REPLACE_WITH_THE_USMAP_SHA256",
  "bindings": {
    "/Script/Example.ExampleAsset": {"schema": "/Script/Example.ExampleAsset", "offset": 0}
  },
  "reflectedStructs": [],
  "nativeStructs": {
    "/Script/CoreUObject.Vector": "vector3-f64",
    "/Script/CoreUObject.Transform": "transform-f64"
  }
}
```

Every native binding must exist in the supplied mapping. Reading the manifest validates token/type/entry limits without opening a USMAP; loading it canonicalizes identities and rejects duplicate aliases or reflected/native conflicts before publishing the immutable context. Native entries share the existing combined entry budget with class/reflected bindings. Provenance includes declared bindings and the input format. A mapping fingerprint still certifies only selected bytes, not cooked-build compatibility.

C++ hosts set `ZenSchemaContext::native_structs` directly. Both supported Zen profiles forward these bindings to the bounded property reader. A decoded native property is compared at component paths such as `/value/translation/1`. The entire export hash remains part of the comparison. Missing layouts, malformed values and truncated native components retain the whole opaque export with a reason; no partial property values are published. Cancellation and shared budget exhaustion still fail the enclosing operation.


## CLI, desktop and installed SDK - 0.46

The existing `--schema` / `--after-schema` options accept version-two contexts in both the CLI and desktop. No additional reader mode or automatic native type detection is required. The cooked property inspector presents decoded native components through the existing typed table, change filtering and selected before/after preview. The schema tab exposes each declared native layout and its manifest/context fingerprints. Unsupported or truncated values still show opaque/unavailable properties; general export native payloads remain opaque.

The installed `examples/schema_consumer` forwards native/reflected bindings from an optional context to `UnversionedReadOptions`. Distribution validation independently encodes a mapping, version-two context and double-precision transform and runs the extracted SDK consumer, including an explicitly zero-masked transform. Source projects and local engine headers are never dependencies of this consumer.


## Cooked text histories - 0.47

`TextProperty` reads the modern flags/history layout when standalone hosts select `TextLayout::ue5_history`. It retains localized namespace/key/source (history 0), the modern empty/culture-invariant source marker (history -1), and unresolved string-table FName/key (history 11). Values use the same `flags`, signed `history`, `kind`, `source`, `namespace`, `key` and `table` fields as the classic tagged reader. ASCII narrow and validated UTF-16 strings share existing decoded-text/work/depth bounds and cancellation. Package names require nonnegative indices and suffix numbers.

Both supported Zen profiles select this modern layout. If an explicit cooked EditorObjectVersion is older than the invariant marker layout, text decoding remains opaque. No source lookup, namespace/key rewriting, string-table loading, culture selection or formatted text evaluation runs. Text namespace/key serialization was checked against the local UE 5.5 Text.cpp, TextHistory.cpp and TextKey.cpp serializers; [Epic's serialization entry point](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FText/SerializeText) documents the relevant engine API.

A zero-masked text is empty, with no payload bytes. Missing standalone layout, unsupported histories, invalid boolean markers, malformed UTF-16 and truncation fail the complete property read. Zen publishes no partial text properties and keeps its full export fingerprint. Supported source edits produce diffs at `/value/source`; namespace/key and table identity changes remain distinct. Mapping/build compatibility still requires the host's matching schema.


## Persistent delegates and weak references - 0.48

Standalone hosts select `ScriptReferenceLayout::package_indices` for `DelegateProperty`, `MulticastDelegateProperty` and `WeakObjectProperty`. Both supported Zen profiles select it for persistent package reads. This layout uses signed package object indices, not process weak-pointer index/serial memory. The reference resolver and package FName table remain explicit inputs.

A single delegate is `{kind:"script-delegate", object:REFERENCE, function:"StoredFunction"}`. A multicast value is `{kind:"multicast-script-delegate", bindings:[...]}` with ordered single-delegate entries. Duplicates and null/unresolved targets remain exactly as read; no runtime invocation-list compaction, validity test or execution occurs. Weak values retain `{kind:"weak-object-reference", object:REFERENCE}` without claiming live-object validity. Zero-masked values consume no payload and have null/None/empty bindings as appropriate.

Layouts were cross-checked against local UE 5.5 ScriptDelegates.h, PropertyDelegate.cpp, PropertyMulticastDelegate.cpp, PropertyWeakObjectPtr.cpp and ArchiveUObject.cpp. In this package archive, weak references are serialized through object references. The reader does not implement memory dumps, network serializers or string-proxy archives. A changed delegate function produces a precise `/value/bindings/INDEX/function` diff; order changes remain observable.

Counts, elements, nested values and host-resolved JSON identities share work/depth/text budgets and cancellation. Resolver object keys also consume text budget. Malformed UTF-8, non-finite values, unsupported resolver values and excessive reference structures fail explicitly. Missing layouts/maps/resolvers, invalid FNames/indices and truncated bindings discard the entire bound read in Zen; the opaque body hash remains. Matching-build real schema validation is still required.

## Cooked values across CLI, desktop and SDK - 0.49

The existing `--schema` / `--after-schema` contexts enable supported text and persistent reference values for both Zen profiles. No new manifest version is needed: the selected profile supplies the wire layout, while class/schema/offset bindings and package name/object tables remain explicit. The inspector exposes source, namespace/key/table, delegate bindings/functions and weak target paths through the existing typed table, search, selected preview and change filter. Workspace save/reopen retains the selected property path; unsupported or damaged values retain the opaque/unavailable state.

The installed schema consumer selects modern text and persistent package-reference layouts when supplied its explicit context. It has no package name table or object resolver, so its standalone example can read localized/invariant text, empty multicast lists and null weak targets; nonempty named/object values require host inputs. The CLI/desktop tests instead use bounded Zen name/object resolution. No text is localized and no delegate or target is executed or loaded.

## Unversioned optional values - 0.50

Standalone readers opt into `OptionalLayout::bool32_value`: a uint32 binary structured-archive presence marker (0 or 1), followed by the untagged USMAP inner type only when set. An unset value is `{kind:"optional",isSet:false}` with no `value`; a set value additionally contains `value`, including an explicit zero or null reference. A skipped property remains absent. A zero mask means unset and consumes no payload. An unset optional needs no reader for an absent unsupported inner value, just as an empty supported container does not read absent elements.

The checked UE 5.5 Zen profile enables this layout. UE 5.3 keeps nonzero optional properties opaque until that layout is independently verified for the profile. Nested optionals, collections, references and explicitly bound structs use the same recursive limits and cancellation. Invalid markers, unsupported present inner values or truncation discard the complete bound property read. Presence changes appear at `/value/isSet`; inner changes appear under `/value/value`.

The binary shape was checked against local UE 5.5 PropertyOptional.cpp and BinaryArchiveFormatter.h and the primary [optional property serializer](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/UObject/FOptionalProperty/SerializeItem?application_version=5.5). No in-memory intrusive-optional representation or tagged optional conversion is inferred.
