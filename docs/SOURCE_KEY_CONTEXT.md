# Source keys, reader context and metadata inspection

Source development 0.193 keeps stored map/set key text independent of reader annotations while retaining actual key data and explicit format uncertainty. This applies to the ordinary source reader and the Core/Git/CLI/desktop paths built on its packages. It is not a replacement for Unreal's runtime hash/equality implementation or a claim to reconstruct class defaults.

## Stored source key text

The reader's existing `stored-map-delta` and `stored-set-delta` shapes remain. Their `entries` and `removed` object keys encode canonical JSON of the stored decoded key. In known tagged-property wrappers, `resolvedType` and `readFormats` are interpretation context and do not enter this encoded key text. The original decoded `key` and set/removed values retain those annotations for inspection. Actual stored type/GUID/tag fields and values remain significant.

Native callback `data` stays raw: members named `type`, `value`, `resolvedType` or `readFormats` remain ordinary data. A certified tagged prefix is the exception only for its actual reflected `properties`; its native tail remains raw. Original tests retain two native keys differing only in a raw member named `resolvedType`.

Key JSON is written incrementally with the shared cancellation control. Its encoded text is charged before the corresponding allocation against an aggregate per-export `max_property_bytes` key-text allowance. This complements source work/window/type-text guards; it does not establish a process-memory cap. A failed key-text guard leaves the containing property opaque, records the read-limit failure and preserves bounded recovery at the following field.

## Actual selected readers

Successfully decoded struct-containing properties record `readFormats`, keyed by relative type paths: `""` for a root struct, `/*` for array elements and `/key` or `/value` for map/set children. Values identify the selected Core route: `builtin`, `tagged` or `extension`. Empty containers retain an empty observation map; they do not claim an element reader ran. Failed whole-property reads publish no attempted reader observations.

Known default builtin owner interpretation is completed consistently before decoding, including empty arrays. This remains the existing selected builtin interpretation for legacy unqualified zero-GUID names, not proof that a missing owner was serialized. Explicit foreign owners, custom GUIDs and tagged certificates keep their existing guards. Some builtin names use reflected serialization in particular profiles (modern `Transform`, older raw tracks and material inputs); their observations record the route actually used.

`readFormats` is context, not stored asset data. Different observed routes at a shared type path produce `source-reader-format-incompatible`; different extension IDs/definitions retain their existing wrapper/tail guards. New element paths in an otherwise compatible empty container do not establish an incompatible earlier reader. Mixed old/new host-produced packages with missing peer reader evidence remain conservative. Reparse both sides with matching current Core libraries.

## Map/set correspondence

Readable keys still need compatible interpretation. Common key format locations include effective reflected types, selected reader routes and extension identities/prefixes. A changed callback ID must not create logical key additions/removals just because the host omitted an explicit definition hash. Unknown keys, incompatible explicit definitions or incompatible formats suppress that container's membership comparison and retain an unavailable scope. Maps use `map-key-identity-unavailable`, `map-key-definition-incompatible` or `map-key-format-incompatible`; sets use the corresponding `set-key-...` reasons. Known independent fields remain comparable.

A metadata-only catalogue change on a controlled same-byte map/set previously produced two false known changes in 0.192. The current reader retains equal stored key identities and no invented membership changes. A genuine mapped value change remains a single known value change. These controlled headers/streams are original fixtures, not independently produced Unreal packages; actual Engine/corpus checks are recorded separately.

## Property inspection

Serialization-field inspection retains readable type/member/struct GUID metadata even beside an opaque or skipped value. The value and its descendants remain Unknown; an explicit unavailable export/parent mask still overrides stale metadata. A truly absent peer remains Added/Removed. Raw native objects with tag-like member names keep ordinary data semantics.

Default flattened property tables keep reader provenance in explicit serialization inspection. The paginated inspector can open the exact GUID and retained metadata on either side without fabricating an absent value. The installed `property_page_consumer` accepts optional `serializationFields: true` in its existing coverage JSON; its path masks keep their prior meaning. No new desktop setting or workspace schema is required.

The scope remains stored source deltas. Cooked mapping key identity, producer-specific custom hash/equality, all case/number equivalences, reconstructed constructor/archetype defaults and broader projector-owned correspondence require their own evidence. These changes do not establish arbitrary plugin serializer discovery or overall product completion.
