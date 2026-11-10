# Stored struct identity and serialization

Source development 0.192 preserves the custom struct GUID independently of the property member GUID. Legacy `StructProperty` tags carry the struct name and GUID; complete type names can also carry the qualified owner and a GUID parameter. The raw type tree remains available. Canonical `structGuid` uses 32 lowercase hexadecimal digits in Unreal GUID word order. An empty public `TaggedType::struct_guid` means unavailable; 32 zeros mean no custom GUID was supplied. A nonzero GUID alone does not establish a class or a serialization recipe.

## Reading and explicit context

The builtin source readers require a compatible owner when one is available and no nonzero custom GUID. A custom or foreign struct named like `IntPoint` no longer selects that native reader merely by name. Nonempty bounded reflected property streams remain readable; native formats still need a matching builtin or an explicitly registered decoder. An empty `None` stream is ambiguous with native bytes and needs an explicit tagged certificate. The failure remains scoped to the containing property, with bytes/fingerprint and recovery at the next property.

An optional source context schema can declare:

```json
{
  "structGuid": "0123456789abcdef0011223344556677",
  "serialization": "tagged",
  "properties": {}
}
```

Place this schema under its qualified owner in the existing `taggedTypes.schemas` catalogue and bind the containing property's struct type to that owner. Types may also carry `structGuid`. The stored GUID, the declared type GUID and a schema GUID must agree when present. `serialization` accepts `tagged` or `native`; omission makes no new wire-format assertion. This explicit certificate takes precedence over the older positive `native` trait hint. `native` requires a serializer binding; `tagged` permits an empty stored override map. Neither mode reconstructs omitted fields or defaults. A USMAP conversion does not automatically supply this certificate.

Legacy tags do not store the package owner, and ordinary native C++ structs commonly have zero custom GUIDs. Without explicit matching metadata, a legacy zero-GUID builtin name retains the existing builtin interpretation. Stored bytes alone cannot distinguish every such name collision. Supply the correct project/build context; this milestone does not claim universal serializer discovery.

Legacy struct-array inner tags now retain their actual type in `wireTypes`, indexed by the nested type path (for example `/*`). Their observed identity also participates in `resolvedType` and source comparison guards. This remains stored tag evidence rather than invented class data. GUID or effective owner changes can leave both decoded values readable while their correspondence is unverified.

## Native extension API

`AssetDecoder::struct_guid` optionally selects one exact canonical custom GUID. It is valid only for struct registrations. Existing empty selectors match any GUID, so a matching wildcard and GUID-specific registration still fail as ambiguous rather than depending on order. Use disjoint profiles or selectors. `DecoderRegistry::find` accepts the optional effective GUID. `DecoderContext::effective_type` borrows the effective struct type for the callback duration; it is null for native exports. Do not retain that pointer.

`TaggedTypes::serialization(owner, guid, summary)` validates the selected profile and declared GUID. Rebuild integrations against matching 0.192 headers and libraries: `TaggedType`, `AssetDecoder` and `DecoderContext` changed. Format names remain compatible with existing catalogues that omit the new optional fields.

Declarative fingerprints now use `cyuediff.binary-layout-definition/2`. Matched profile intervals and GUID selectors choose applicability; they do not change the immutable wire/output program and are excluded from its definition hash. The whole catalogue hash still records them. Kind, target, prefix and program interpretation remain significant. Version-one and version-two fingerprints intentionally differ; compare through a matching current SDK instead of treating historical hashes as equivalent.

## Evidence and limits

An original private Unreal 5.3.2 `UUserDefinedStruct` named `IntPoint` and native `FIntPoint(0,0)` produce the same eight value bytes through the actual Engine serializer. Their actual property tags and API GUIDs distinguish the cases. The old 0.191 reader falsely returned `[0,0]` for the user-defined stream. The current reader preserves its GUID and keeps it opaque without a certificate; with the matching tagged certificate it returns `{}`, the stored override map, while recording that omitted defaults remain unresolved. Actual native `(0,0)` and `(9,-4)` values remain readable.

The Core validation packages wrap those actual Engine property streams in controlled package headers; they are not claimed as complete Engine-produced packages. The optional read-only adapter is documented in [the reference tools](../tools/unreal_reference/README.md). Original fixtures cover older and complete type tags, member/struct GUID separation, explicit selection, foreign owners, recovery, source comparison and identity budgets. Further producers, class/archetype defaults and canonical logical-key correspondence remain separate acceptance work.

Source development 0.193 adds [source key/reader context and readable serialization metadata](SOURCE_KEY_CONTEXT.md). Reader observations are context, and key encoding is bounded separately from raw native data.
