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

The converter emits uncompressed USMAP 4 with PPTH qualified identities, explicit 64-bit enum values, inherited schemas and supported nested types. It filters editor-only fields for cooked use. Transient and deprecated fields still occupy their ordinal slots: omission from a particular serialized instance does not remove their schema position. Numeric static-array dimensions expand those slots.

Unknown static dimensions, including expressions and ambiguous literals, exclude their declaring type and derived types. An unavailable field type retains its slot without a descriptor, so a present value fails explicitly instead of shifting later property names. Missing/ambiguous type references, unsupported nested shapes and excluded structures are recorded in `summary.json`. Cyclic/excessive inheritance is rejected. Metadata inputs have byte/node/depth budgets and must stay unchanged during generation.

The converter does not infer native serializers, instance values, inherited defaults or native/reflected struct bindings. A native script struct's declarations do not prove that its serialized bytes follow a reflected property stream. Existing explicit native layouts and declarative wire descriptions remain necessary where appropriate. The generated file contains native header descriptions; Blueprint-declared fields still require their separately stored declarations. Dynamic native modifications and arbitrary plugin C++ serializer logic remain outside this metadata model.

## Use in Core, CLI and desktop

For example, the context below explicitly binds Texture2D properties at offset zero for the selected UE 5.3 layout and supplies two checked native struct layouts. Replace the fingerprint with the generated file's actual SHA-256.

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
    "/Script/CoreUObject.IntPoint": "int-point"
  }
}
```

Select it with `zen-inspect --schema CONTEXT.json` or the desktop's schema context controls. Successful reads retain consumed bytes, qualified names and mapping/context fingerprints. `mappingVerified:false`, unresolved defaults and opaque native payload coverage remain explicit. A matching build-version string or metadata fingerprint alone is insufficient to certify every cooked schema.

The original fixture test converts metadata and reads an independently encoded cooked stream through the public CLI. It checks inheritance, static-array indices, editor filtering, arrays/enums/references and named before/after values. Unknown type slots and static dimensions fail conservatively without modifying inputs.

## Real evidence and current scope

On the authorized UE 5.3.2 build, the tool exports 401 engine/project modules plus its empty output module, reading 5,718 unchanged headers. The generated mapping has 10,445 schemas and 1,934 enums. Thirty-one types are excluded because static dimensions are unresolved; thirty-five field descriptors remain unavailable. These counts describe metadata coverage, not fully decoded asset coverage.

For 21 explicitly paired real Texture2D packages, 20 cooked property streams decode, yielding 68 present properties. All 37 comparable stored imported-size/lighting-GUID values match their loose sources exactly. The remaining texture requires an additional PerPlatformFloat serializer description and stays opaque. All checked original source/container/context/codec fingerprints remain unchanged. English/French native desktop captures show the same decoded values.

The reproducible local validator accepts an explicit JSON list of pairs with `source`, cooked `id`, optional package `name` and optional chunk `sha256`:

```powershell
python scripts/validate_uht_mappings.py --pairs pairs.json --context context.json --container Game-Windows.utoc --script-objects global.utoc --codec local-codec.dll --output-dir build-native/uht-validation
```

It compares selected named texture properties, records actual consumed bytes and preserves inputs. The default result fails when a selected stream is unavailable; `--allow-unavailable` records known limits while retaining `complete:false`. Mismatched comparable values always fail. These checks do not validate every native class, property or serializer. Further cooked values, Blueprint schema composition, newer host APIs and native visual payloads remain active work.
