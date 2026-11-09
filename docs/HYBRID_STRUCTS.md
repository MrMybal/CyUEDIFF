# Hybrid reflected/native structures

Post-0.182 source development adds a reusable Core reader for explicitly certified struct serializers that write reflected properties first, then a separately described native tail. This supports native/plugin formats whose complete serialization cannot be inferred from UHT or USMAP fields. Core does not launch an engine or execute plugin code.

## Explicit contract

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


## Verification and present limits

The original hybrid fixtures pass 102 Core checks, including native USMAP traits, repeated values, following-property boundaries, every truncated prefix, canonical binding conflicts, budgets, cancellation and whole opaque fallback. Public CLI and desktop checks cover both supported Zen header profiles, named tail differences, protected outputs, workspace/locale retention and an unavailable peer that must not imply removed properties. The complete regression passes 153 Core suites and 238 Windows desktop/Git/native suites; version metadata is additionally checked after the 0.183 minor-version change.

A fresh Core-only SDK consumer built from its installed example checks four actual UE 5.3.2 before/after package cases, eight physical LODs, 588,898 stored/API values, 12,788 physical vertices, 24,520 triangles, 26,748 tethers and 20 explicit Chaos configuration values. Native tether encodings and all 12,788 raw transition records match the actual Engine serializers exactly. Package/object identities remain the same across the independently cooked revisions; the cube produces 84 named changes, including tether lengths and damping. Twenty-two altered reference/projection cases are rejected. All 106 protected original/private source, container, mapping, reference and SDK inputs remain unchanged. The public read-only manifest exporter reproduces the after references independently.

A first original after-fixture copied an unused boundary transition while creating its second LOD. The preparation helper now clears inherited transition arrays before invoking the actual Engine transition builder. That initial attempt is retained privately; the final counts above use the corrected independent cook. No Core comparison check is weakened to accept a mismatching reference.

Four actual native captures verify EN/FR tether/configuration selection, typed differences and workspace state. The generic inspector still has its existing 20,000-row display limit: the large physical object reports `displayTruncated: true`. Its complete values remain available through the Core/SDK, but efficient navigation of large nested values is further UI work. Dedicated physical mesh projection, render/simulation correspondence bounds, semantic transition decoding, missing defaults and runtime evaluation are not declared complete. The final verified 0.182 ZIP remains unchanged and predates this source/API milestone.
