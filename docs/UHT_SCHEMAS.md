# Matching-source native mappings

The optional host tools generate reusable cooked-property descriptions from the selected engine/project's UHT reflection model. CyUEDiff Core accepts the cached mappings through its existing standalone USMAP API and explicit Zen contexts. The header parser is needed during metadata preparation; asset inspection does not start it or Unreal Editor.

## Prepare metadata

The validated driver currently supports Windows UE 5.3 hosts, Python 3.11+ and a compatible .NET SDK. Supply the engine root, existing project UHT manifest, project file and XML configuration cache from a completed build. The manifest's original engine root must agree with the selection. Missing headers, unexpected runtimes, extra UHT plugins and unsafe output paths are rejected.

```powershell
python scripts/export_uht_metadata.py --engine-root C:/UE/UE_5.3 --manifest C:/Project/Intermediate/Build/Win64/ProjectEditor/Development/ProjectEditor.uhtmanifest --project C:/Project/Project.uproject --xml-cache C:/Local/XmlConfigCache.bin --output-dir build-native/uht-metadata
python scripts/build_uht_mappings.py --metadata build-native/uht-metadata/modules --output-dir build-native/uht-mappings
```

The original MIT exporter source is under `tools/uht_schema_exporter`. It builds against explicitly selected host UHT/Core assemblies with package sources disabled. A copied manifest redirects all outputs to the workspace build directory. An empty exporter module provides UHT's required plugin output owner; standard code generation is disabled. The copied configuration cache prevents rebuilding the host cache. Original headers, project/manifest/cache/build-version files and selected engine/tool inputs are fingerprinted before/after. The resulting `provenance.json` records the engine build and source hashes; generated private metadata stays local.

`cyuediff.uht/1` metadata preserves full derived nested-property type references, class/struct bases, declaration order, enum labels/values, static dimension expressions and relevant flags. The standard UHT JSON exporter drops some inner-property struct/enum references, so it is not accepted as this complete metadata format.

## Mapping contract

The converter emits uncompressed USMAP 4 with PPTH qualified identities, explicit 64-bit enum values, inherited schemas and supported nested types. It filters editor-only fields for cooked use. Transient and deprecated fields still occupy their ordinal slots: omission from a particular serialized instance does not remove their schema position. Numeric static-array dimensions expand those slots. The matching UHT LWC pseudo-property maps to the runtime double property, as constructed by the selected engine.

Unknown static dimensions, including expressions and ambiguous literals, exclude their declaring type and derived types unless a matching explicit dimension manifest supplies their exact counts. An unavailable field type retains its slot without a descriptor, so a present value fails explicitly instead of shifting later property names. Missing/ambiguous type references, unsupported nested shapes and excluded structures are recorded in `summary.json`. Cyclic/excessive inheritance is rejected. Metadata inputs have byte/node/depth budgets and must stay unchanged during generation.

The converter does not infer native serializers, instance values, inherited defaults or native/reflected struct bindings. A native script struct's declarations do not prove that its serialized bytes follow a reflected property stream. Existing explicit native layouts and declarative wire descriptions remain necessary where appropriate. The generated file contains native header descriptions; Blueprint-declared fields still require their separately stored declarations. Dynamic native modifications and arbitrary plugin C++ serializer logic remain outside this metadata model.

## Use in Core, CLI and desktop

For example, the context below explicitly binds Texture2D properties at offset zero for the selected UE 5.3 layout and supplies three checked native struct layouts. Replace the fingerprint with the generated file's actual SHA-256.

```json
{
  "format": "cyuediff.zen-schema/2",
  "profile": "ue5.3",
  "usmap": "native.usmap",
  "sha256": "REPLACE_WITH_64_LOWERCASE_HEXADECIMAL_DIGITS",
  "bindings": {
    "/Script/Engine.Texture2D": {"schema": "/Script/Engine.Texture2D", "offset": 0}
  },
  "reflectedStructs": [],
  "nativeStructs": {
    "/Script/CoreUObject.Guid": "guid",
    "/Script/CoreUObject.IntPoint": "int-point",
    "/Script/Engine.PerPlatformFloat": "per-platform-float-cooked"
  }
}
```

Select it with `zen-inspect --schema CONTEXT.json` or the desktop's schema context controls. Successful reads retain consumed bytes, qualified names and mapping/context fingerprints. `mappingVerified:false`, unresolved defaults and opaque native payload coverage remain explicit. A matching build-version string or metadata fingerprint alone is insufficient to certify every cooked schema.

The original fixture test converts metadata and reads an independently encoded cooked stream through the public CLI. It checks inheritance, static-array indices, editor filtering, arrays/enums/references and named before/after values. Unknown type slots and static dimensions fail conservatively without modifying inputs.

## Real evidence and current scope

On the authorized UE 5.3.2 build, the tool exports 401 engine/project modules plus its empty output module, reading 5,718 unchanged headers. The generated mapping has 10,445 schemas and 1,934 enums. Thirty-one types are excluded because static dimensions are unresolved; thirteen field descriptors remain unavailable after LWC pseudo-property resolution. These counts describe metadata coverage, not fully decoded asset coverage.

For 21 explicitly paired real Texture2D packages, all 21 cooked property streams now decode, yielding 72 present properties. All 39 comparable stored imported-size/lighting-GUID values match their loose sources exactly. The additional cooked PerPlatformFloat binding exposes the remaining stored platform value, independently checked against its raw marker/value bytes and the 39-byte property-stream boundary. All checked original source/container/context/codec fingerprints remain unchanged. English/French native desktop captures show the same decoded values.

The reproducible local validator accepts an explicit JSON list of pairs with `source`, cooked `id`, optional package `name` and optional chunk `sha256`:

```powershell
python scripts/validate_uht_mappings.py --pairs pairs.json --context context.json --container Game-Windows.utoc --script-objects global.utoc --codec local-codec.dll --output-dir build-native/uht-validation
```

It compares selected named texture properties, records actual consumed bytes and preserves inputs. The default result fails when a selected stream is unavailable; `--allow-unavailable` records known limits while retaining `complete:false`. Mismatched comparable values always fail. These checks do not validate every native class, property or serializer. Further cooked values, Blueprint schema composition, newer host APIs and native visual payloads remain active work.

## Explicit static dimensions — post-0.181 development

A source token such as `MAX_TEXCOORDS` is not an evaluated count in UHT metadata. Supply an explicit qualified field binding after checking the matching build's source or compiler facts:

```json
{
  "format": "cyuediff.uht-dimensions/1",
  "metadataSha256": "REPLACE_WITH_summary.json_metadataSha256",
  "fields": {
    "/Script/Engine.MeshUVChannelInfo.LocalUVDensities": {
      "expression": "MAX_TEXCOORDS",
      "size": 4
    }
  }
}
```

```powershell
python scripts/build_uht_mappings.py --metadata build-native/uht-metadata/modules --static-dimensions dimensions.json --output-dir build-native/uht-resolved
```

The manifest binds the complete export fingerprint, exact qualified field and exact expression. Counts are integers from 1 through 255. Unknown/unreachable bindings, changed expressions, invalid counts, contradictions with known literals, stale metadata and input/output hard-link overlap are refused before output replacement. The 2 MiB manifest limit complements the metadata budgets. Every input stays unchanged; the summary retains the manifest fingerprint and selected facts. This is explicit host evidence rather than C++ expression evaluation or automatic build certification. Keep the supporting header/compiler evidence with private local reports.

The metadata fingerprint is SHA-256 over the UTF-8 compact JSON object of relative POSIX metadata paths and their SHA-256s, sorted by key. It is independent of the export directory's absolute location. With no explicit dimensions, the converter keeps its conservative exclusions.

The validated UE 5.3 source defines the selected UV density array with four entries. That binding restores one schema and its dependent field descriptions. With LWC double descriptors, this selected map contains 10,446 schemas, 1,934 enums and 11 unavailable descriptors, while 30 types remain excluded. UHT's LWC pseudo-property is constructed as an FDoubleProperty in the matching engine; it is not a new inferred native struct serializer.

Do not assign raw native serializers by a struct's name or C++ stream operator alone. In particular, the selected BoxSphereBounds property uses a reflected stream containing explicitly bound native vector values and a double radius. Its C++ archive operator does not establish its UPROPERTY wire layout. Native/reflected bindings remain explicit and mappingVerified stays false.

`validate_mapped_values.py` compares all common supported property/type leaves with explicit loose source pairs. Pair records contain `name`, `id`, `source` and `class`, with an optional chunk `sha256`. Object/reference identities, per-platform resolution and unstored archetype defaults are excluded; array lengths and static array indices are retained, and normalized name collisions fail. A mismatch produces a private report and failure, while original package/companion/container/context/mapping/codec hashes are checked before/after.

```powershell
python scripts/validate_mapped_values.py --pairs pairs.json --context context.json --container Game-Windows.utoc --script-objects global.utoc --codec local-codec.dll --output-dir build-native/mapped-values
```

Real evidence covers 84 selected StaticMesh packages and 405 present properties. All 3,119 common projected type/value/count checks match their loose sources; original inputs remain unchanged. This establishes selected property evidence and native-body starting boundaries. The render geometry and Nanite bodies remain separate readers.
