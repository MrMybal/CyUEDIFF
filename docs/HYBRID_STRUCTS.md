# Hybrid reflected/native structures

Post-0.182 source development adds a reusable Core reader for explicitly certified struct serializers that write reflected properties first, then a separately described native tail. This supports native/plugin formats whose complete serialization cannot be inferred from UHT or USMAP fields. Core does not launch an engine or execute plugin code.

## Tagged source structures — 0.185

A source catalogue can now certify a tagged property prefix followed by a native tail. Add `"prefix": "tagged-properties"` to a `kind: struct` decoder in a `cyuediff.binary-layouts/1` catalogue. Its target is the exact serialized struct identity and its archive/custom-version profile must match the producer. The standard CLI `--layouts` / `--after-layouts` and desktop **Native structure layouts** controls select independent catalogues. No Engine, plugin DLL or historical project code is loaded.

```json
{
  "format": "cyuediff.binary-layouts/1",
  "layouts": {"SettingsTail": {"type": "record", "fields": [
    {"name": "stamp", "value": {"type": "i64"}},
    {"name": "samples", "value": {"type": "array", "count": "i32", "element": {"type": "f32"}}}
  ]}},
  "decoders": [{
    "id": "my-plugin.settings/1", "kind": "struct", "target": "MyPluginSettings",
    "profile": {"ue4": [522, 522], "ue5": [1009, 1009]},
    "prefix": "tagged-properties", "layout": "SettingsTail"
  }]
}
```

The existing property parser reads through the exact `None` prefix terminator; the callback then sees only the remaining native window. Values retain `{ "decoder": "...", "prefix": "tagged-properties", "data": { "properties": { ... }, "native": ... } }`. Prefix entries keep their stored tag/type metadata. Direct values must consume their full payload; repeated values consume one prefix and one declared tail. An empty native tail is valid when its prefix made progress. No unreadable prefix or failed tail is published as a usable partial hybrid: the complete containing property keeps its opaque range/fingerprint, and subsequent bounded properties remain recoverable.

For trusted C++ integrations, set `AssetDecoder::prefix = DecoderPrefix::tagged_properties`. `DecoderContext::tagged_properties` borrows the completed prefix only for the callback duration, allowing a host to choose a tail format from a known stored field. It is null for ordinary whole-value/export callbacks. `charge` shares the enclosing value/text work budget; the context also supplies the remaining depth. Declarative parsing charges failed work and returned JSON validation remains bounded. The ordinary source parser's property/string limits and cooperative cancellation still apply. Set the matching explicit narrow-string encoding in `ReadOptions` when the producer needs one.

Root fields, nested structures, arrays and full-type map/set values use this path. Older map/set tags can omit a struct's identity. A native-tail description does not supply that missing type: those values stay opaque and need source metadata completion. A probe of an existing UE 5.3 physical-cloth source LOD still returns `hybrid-prefix`; its legacy struct-valued maps require this remaining work. The new mechanism does not resolve constructor/class defaults, infer arbitrary native serializers or complete every source asset format.

The [installed binary layout consumer](../examples/binary_layout_consumer/) exercises the public Core path. `scripts/validate_tagged_hybrids.py` compares it with separately reloaded actual Engine references for original plugin-defined hybrid assets, including exact int64 values and the complete known field difference set. See [VALIDATION.md](VALIDATION.md#tagged-source-hybrids--0185-development).

## Mapped unversioned contract

Use `UnversionedReadOptions::struct_tails` or `ZenSchemaContext::struct_tails` alongside an immutable `BinaryLayouts` catalogue. A binding maps an exact schema identity (or an unambiguous short alias) to a native-tail layout. It is exclusive with reflected-only, built-in native and whole-value declarative bindings for the same canonical schema. A native trait in supplied USMAP metadata still forbids reflected-only inference; the hybrid binding explicitly certifies the custom prefix/tail contract.

The prefix uses its mapped unversioned header and preserves qualified typed field values. The native layout starts at the exact consumed prefix offset, shares the enclosing property window and stops at its declared end. Arrays can contain repeated hybrids without swallowing subsequent elements/properties. Nested hybrid prefixes and native arrays use the existing shared value/text/depth/cancellation budgets. Failed native reads discard the entire bound property result; partial prefixes are not published as complete values. Zero-masked whole hybrid values are refused because no native default model is supplied.

```cpp
options.binary_layouts = layouts;
options.struct_tails["/Script/MyPlugin.Settings"] = "SettingsTail";
```

The returned struct value separates `{ "properties": { ... }, "native": ... }`. Direct struct properties also identify `structTailLayout` and `binaryLayoutsSha256`; selected bindings and catalogue hashes affect the enclosing context fingerprint. Prefix property presence does not resolve omitted class/archetype defaults. Old contexts without tails retain their previous fingerprints.

## Manifest version four

`cyuediff.zen-schema/4` retains all required version-three fields and adds required object `structTails`. Keep `structLayouts` and `nativeStructs` as empty objects when unused. For example, a tail containing a signed count followed by fixed record values can be bound as follows (within a complete matching context):

```json
{
  "format": "cyuediff.zen-schema/4",
  "profile": "ue5.3",
  "usmap": "matching.usmap",
  "sha256": "EXACT_MAPPING_SHA256",
  "bindings": {},
  "nativeStructs": {},
  "structLayouts": {},
  "structTails": {"/Script/MyPlugin.Settings": "SettingsTail"},
  "binaryLayouts": {
    "format": "cyuediff.binary-layouts/1",
    "layouts": {"SettingsTail": {"type": "array", "count": "i32", "element": {"type": "f32"}}}
  }
}
```

Replace the illustrative mapping fingerprint and supply the actual class binding and serializer evidence. Older manifest versions reject `structTails`. Alias collisions, missing layouts and overlapping bindings fail before reading values. Version-four manifests do not certify an arbitrary engine/build or native serializer; the host supplies that provenance explicitly.

## Current physical cloth scope

The selected UE 5.3 Windows context can bind the reflected portion of `ClothLODDataCommon`, followed by its stored up/down transition arrays, and the reflected prefix of `ClothTetherData`, followed by its nested tether batches. Actual source metadata plus explicit source-verified twelve-slot `ClothVertBoneData` dimensions enable mapped physical positions, normals, triangle indices, bone data, inverse masses, weight maps and stored configuration values. The runtime accepts the prepared mapping; source metadata preparation remains an optional separate tool.

Tether fields expose stored anchor, particle and finite float32 length values. The current transition description preserves each exact 64-byte record as raw hexadecimal bytes; this does not interpret active/inactive coefficients or evaluate a transition. Stored property/native differences flow through the existing CLI and EN/FR typed property inspector. Physical mesh projection, simulation-bound correspondence checks, resolved missing configuration defaults and runtime evaluation remain separate work. Do not treat raw transition byte changes as a decoded coefficient difference.

Unreal's `GetPathName` uses a colon at the asset/subobject boundary while Core's Zen outer paths use dots. The independent reference validator normalizes this display delimiter while retaining the package and every object name; it does not pair unrelated objects by class. Numeric API JSON can lose zero signs; exact native tether/transition serializers are compared separately.

See [the Core-only installed example](../examples/hybrid_struct_consumer/) and `scripts/validate_physical_cloth.py`. The example reads explicit members and independent contexts without Git, CyUI, Unreal or an optional codec. The validator checks matching API references, exact native bytes and unchanged protected inputs. Private source/cooked assets, mappings, Engine code and reference outputs remain outside the distribution. The last verified 0.182 archive predates this source feature.


## Subsequent typed physical projection

Version 0.184 adds a separate [physical cloth Core/CLI model](PHYSICAL_CLOTH.md) on top of these certified properties/tails. It interprets the modern transition records, creates physical geometry, provides named stored differences/reference-pose wrap queries and validates explicit render-source bounds. The generic schema projection itself retains its raw transition bytes. The dedicated physical desktop is available for the selected cooked profile; class defaults remain unresolved.

## Verification and present limits

The original hybrid fixtures pass 102 Core checks, including native USMAP traits, repeated values, following-property boundaries, every truncated prefix, canonical binding conflicts, budgets, cancellation and whole opaque fallback. Public CLI and desktop checks cover both supported Zen header profiles, named tail differences, protected outputs, workspace/locale retention and an unavailable peer that must not imply removed properties. The original hybrid checkpoint passed 153 Core suites and 238 Windows desktop/Git/native suites; version metadata is additionally checked after the 0.183 minor-version change.

A fresh Core-only SDK consumer built from its installed example checks four actual UE 5.3.2 before/after package cases, eight physical LODs, 588,898 stored/API values, 12,788 physical vertices, 24,520 triangles, 26,748 tethers and 20 explicit Chaos configuration values. Native tether encodings and all 12,788 raw transition records match the actual Engine serializers exactly. Package/object identities remain the same across the independently cooked revisions; the cube produces 84 named changes, including tether lengths and damping. Twenty-two altered reference/projection cases are rejected. All 106 protected original/private source, container, mapping, reference and SDK inputs remain unchanged. The public read-only manifest exporter reproduces the after references independently.

A first original after-fixture copied an unused boundary transition while creating its second LOD. The preparation helper now clears inherited transition arrays before invoking the actual Engine transition builder. That initial attempt is retained privately; the final counts above use the corrected independent cook. No Core comparison check is weakened to accept a mismatching reference.

Four actual native captures verify EN/FR tether/configuration selection, typed differences and workspace state. The earlier expanded inspector reaches its 20,000-row ceiling on the large physical object. The subsequent [paged browser](PROPERTY_BROWSER.md) supplies nested scopes, direct-child pagination, index navigation and workspace restoration for these complete decoded values. The expanded table keeps its original safety limit. Those results describe the earlier generic schema projection. The subsequent selected typed physical model, render-source bounds, transition queries and desktop are documented in [PHYSICAL_CLOTH.md](PHYSICAL_CLOTH.md); missing defaults, broader source/producer coverage and runtime evaluation remain open. The final verified 0.182 ZIP remains unchanged and predates this source/API milestone.
