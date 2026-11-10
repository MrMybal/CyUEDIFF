# Native decoder definition identity

Source development 0.191 retains explicit per-binding definition identities separately from stored asset JSON. A reused decoder ID no longer establishes correspondence when its recorded definition changes. This applies to registered source struct values, tagged-prefix native tails, native exports and the source/Git/EN/FR property-review paths that share source coverage.

## Declarative binary layouts

`BinaryLayouts::decoder_registry()` computes a canonical SHA-256 for each binding. It covers the ordered wire/output program, referenced definitions, binding kind/target, prefix mode and an explicit interpreter contract version. String nodes use their effective narrow encoding. Object nodes include the selected package-reference mode or the host resolver identity.

The fingerprint excludes unrelated catalogue entries, local layout names and read/cancellation budgets. Shared layout references are hashed in validated dependency order instead of expanding their output trees. Renaming a layout or replacing an inline node by an equivalent reference preserves the definition identity. Record member names/order, integer sign/width, referenced definitions and effective text interpretation remain significant. These hashes identify supplied descriptions; they do not prove a description matches an Unreal producer.

The whole-catalogue SHA-256 remains separate provenance. Changing an unrelated definition can change that catalogue context without making the current native values incompatible. Since 0.192, contract `cyuediff.binary-layout-definition/2` excludes matched profile intervals and GUID selectors: these constrain applicability without altering a matched wire/output program. Version-one historical fingerprints remain distinct.

## Host callbacks and reference resolvers

`AssetDecoder::definition_sha256` optionally supplies a canonical lowercase 64-digit SHA-256 for a host callback's implementation/schema contract. `DecoderRegistry::definitions(summary, control)` returns nonempty explicit identities for registrations matching that archive profile. `Package::decoder_definitions` retains these available registration identities; their presence is not proof every registration was executed.

Callbacks without an explicit hash retain the existing stable, versioned `AssetDecoder::id` contract. The Core cannot inspect arbitrary callback machine code or captured mutable state. Hosts must update IDs or explicit hashes when interpretation changes, provide deterministic callbacks and obey the existing threading/ownership rules. Supplying the same identity for different implementations defeats that host-provided assertion; no automatic plugin discovery or binary attestation is claimed.

A custom `BinaryLayoutReadOptions::reference` passed to `decoder_registry()` requires a matching nonempty, printable ASCII `reference_identity` of at most 256 characters. The host owns the same stability/versioning obligation for that resolver. An identity without a resolver, missing identity or malformed identity fails with `layout-reference-identity`. Direct `BinaryLayouts::decode()` retains its existing resolver interface; it does not create source-package comparison provenance.

## Comparison and presentation

The structured report's `sourceContexts` and package inspection's `decoderDefinitions` expose the ID-to-definition map. Semantic snapshots and callback `data` keep their existing stored-value shape. Fields named `definitionSha256` or `decoderDefinitions` inside native data remain ordinary data.

A matched source wrapper with different explicit definitions, or an explicit definition missing on one peer, gets a scoped comparison-unavailable result. Unknown rows include available `beforeDefinitionSha256` and `afterDefinitionSha256` evidence. The reasons are `native-struct-definition-incompatible` or `native-definition-incompatible`. Native definitions used in stored or removed map keys also guard logical key correspondence with `map-key-definition-incompatible`, preserving readable-key flags separately from incompatible-definition flags. Both values can be readable while their member correspondence is unknown. Equal output numbers or identical input bytes do not prove equivalent interpretation under different definitions.

For a hybrid struct, only the incompatible native tail is masked; independently readable reflected prefix fields and nested structures with matching definitions retain known changes. Native export incompatibility masks that export's native projection. Existing conservative dependent graph/material/mesh/skeleton/collision guards remain. Known siblings, binary facts and opaque evidence stay visible. The general status remains unverified/CLI exit 2 when unresolved scopes exist. These rules do not reconstruct missing defaults or certify every producer.

The reusable EN/FR property browser shows Unknown for incompatible correspondence while retaining both readable values. Whole-document text equality is disabled when source coverage or decoder/type context cannot support it. Existing selection/workspace behavior is retained.

## SDK and limits

Rebuild hosts against matching 0.192 headers/libraries: `Package`, `AssetDecoder` and `BinaryLayoutReadOptions` gained members. No catalogue format change or new desktop selection is required. The [installed source comparison example](../examples/source_diff_consumer/) carries independent catalogues through the reader and streams the report. The [callback consumer](../examples/decoder_consumer/) demonstrates host-declared schema hashes, fixture ownership checks and streamed output; its declaration is not callback binary attestation.

The implementation validates bounded supplied definitions and supports cancellation. This is interpretation provenance, not a process memory cap, a schema-discovery mechanism or a native serializer sandbox. Further producer ownership, class/archetype defaults, canonical logical map keys and additional producer validation remain separate work. See [source availability](SOURCE_AVAILABILITY.md) and [validation](VALIDATION.md).

See [stored struct identity](STRUCT_IDENTITY.md) for the 0.192 GUID and explicit serialization contract.
