# Explicit tagged source type metadata

Source development 0.186 adds immutable type information for tagged properties whose archive tags omit inner struct/enum identities. Core reads ordinary source packages without loading Unreal or plugin code. A matching USMAP supplies reflected field types; an explicit native layout still certifies custom binary serialization. This complements [hybrid source structs](HYBRID_STRUCTS.md).

## Prepare and select a context

```sh
cyuediff source-types matching.usmap 522 1009 /Script/MyPlugin.Asset \
  --layouts certified-native-layouts.json --out source-catalogue.json
cyuediff inspect source.uasset --layouts source-catalogue.json
cyuediff diff before.uasset after.uasset --layouts before-catalogue.json \
  --after-layouts after-catalogue.json
```

The two numbers are exact UE4/UE5 **file archive versions**, not engine marketing versions. Select one or more exact root classes/structs. Preparation includes their parents and reachable struct dependencies, original static array dimensions, enum identities and positive native-serializer traits. It records the supplied USMAP SHA-256. Unknown/ambiguous definitions fail. The optional base preserves its existing native recipes and string encoding; preparation refuses a base already containing `taggedTypes`. The output is a directly reusable `cyuediff.binary-layouts/1` catalogue, with no inspection-report wrapper. A compressed mapping requires the explicitly supplied `--codec` adapter. Preparation alone does not certify a mapping against a source producer.

The existing desktop **Native structure layouts** / **Formats des structures natives** controls select these catalogues independently before/after. They participate in worker/cache identity and workspace retention. The property browser shows stored entries and resolved type data through its existing serialization-fields switch. No additional engine discovery or preferences are implied.

## Public Core contract

`TaggedTypes::from_schemas(schemas, roots, profile, options)` converts explicit `UnversionedSchemas`; `TaggedTypes::parse(json, options)` accepts a standalone context. `DecoderRegistry` takes an optional `shared_ptr<const TaggedTypes>` after its registration vector. `BinaryLayouts::decoder_registry()` attaches the selected inline context automatically. Pass that registry in `ReadOptions::decoders`, using the catalogue's explicit string encoding when applicable. The installed [binary layout consumer](../examples/binary_layout_consumer/) exercises this path without Git, CyUI or Unreal.

```json
{
  "format": "cyuediff.tagged-types/1",
  "profile": {"ue4": [522, 522], "ue5": [1009, 1009]},
  "schemas": {
    "/Script/MyPlugin.Asset": {
      "super": "", "native": false,
      "properties": {
        "Weights": {"arraySize": 1, "type": {
          "name": "MapProperty", "parameters": [
            {"name": "UInt32Property"},
            {"name": "StructProperty", "parameters": [{"name": "PointWeights"}],
             "schema": "/Script/MyPlugin.PointWeights"}
          ]
        }}
      }
    },
    "/Script/MyPlugin.PointWeights": {
      "properties": {"Values": {"type": {
        "name": "ArrayProperty", "parameters": [{"name": "FloatProperty"}]
      }}}
    }
  }
}
```

Embed this object as optional `taggedTypes` in the ordinary catalogue. Schema `super`, `native`, property `arraySize` and context `usmapSha256` are optional; a property type and the profile are required. Profile `customVersions` accepts explicit inclusive intervals keyed by lowercase GUID. An omitted dimension is one. Native flags are conservative positive evidence, never a serializer recipe. Parent fields are inherited only through an existing unambiguous definition; cycles and shadowing reject the context. Qualified identities are deterministic; ambiguous short aliases fail.

`type` retains the original serialized tag. `resolvedType` appears only when explicit metadata supplies additional information. `sourceTypeContext` records its hash, profile match and `defaultsResolved: false` in package reports; unselected report shapes remain unchanged. Provenance does not become a semantic asset change. Known stored kinds, struct/enum names, full-type package qualifiers and static array indices must agree with the context. Byte-enum compatibility retains the source archive's FName serialization. Conflicting metadata never converts the payload to another primitive. Qualified foreign types cannot select an unrelated built-in serializer by basename; explicit certified plugin bindings still take precedence. A failed hybrid prefix leaves the complete containing property opaque and allows recovery at the next bounded property.

## Bounds and evidence

Standalone defaults are 100,000 metadata nodes, 2 MiB text and depth 48, with cancellation; catalogues also enforce their existing definition limits. Conversion follows only selected dependencies and bounds inherited-field work. Source property/native work keeps its existing limits. Hosts can explicitly choose larger `ReadOptions` budgets when warranted; selecting metadata does not raise limits. Trusted C++ native callbacks remain the host's responsibility.

Original tests cover six encoded archive profiles (including full type names), omitted legacy map/set struct identities, native prefix/tail composition, enum storage, ambiguous/qualified identities, inheritance, static dimensions, contradictions and budgets/cancellation. These profiles are encoded fixtures, not six independent Engine producers. CLI and EN/FR desktop checks exercise independent contexts and workspace restoration.

The selected actual UE 5.3.2 Cube before/after sources expose two LODs each. `scripts/validate_tagged_cloth.py` compares stored fields, twelve-slot bone records, struct-valued weight maps, config links and exact native tether/transition encodings with separately exported actual Engine API references through an installed Core consumer. Omitted `ReferenceBoneIndex` remains unresolved. Other export suffixes remain opaque. The larger Humanoid source reaches the default property budget and remains unavailable; this is not whole source cloth or physical/render correspondence support.

USMAPs describe reflected fields and may omit editor-only fields or lose distinctions; stored contradictions reject rather than guess. Contexts do not reconstruct constructor/archetype defaults, infer arbitrary serializers, establish engine/plugin ABI compatibility or evaluate runtime state. Matching producer verification remains necessary. Private assets, mappings, Engine source and references are excluded from distribution. This local milestone leaves the previously verified 0.182 Windows ZIP unchanged.


## Source read policies — 0.187

Explicit source work/window limits are available in Core, CLI and EN/FR desktop, with independent after policies and workspace format 14 retention. Versions 1–13 preserve their original default policy. A failed limit/count guard keeps inspection fingerprints/diagnostics and prevents invented semantic removals in a paired comparison. The selected real Humanoid source can use a deliberately larger work policy; dedicated source physical projection, defaults, broader acceptance and final delivery remain open. See [SOURCE_READ_LIMITS.md](SOURCE_READ_LIMITS.md).
