# Cooked interpretation and stored key correspondence

Development 0.194 separates mapped property values from the context used to interpret their bytes. This applies to `decode_unversioned`, Zen inspection/comparison, the command line and the desktop property views. It does not discover arbitrary native serializers or reconstruct omitted defaults.

## Retained interpretation

`UnversionedProperties::interpretation` and a decoded Zen export's `propertyRead.interpretation` contain `cyuediff.cooked-interpretation/1`: the qualified root `schema` and a `schemas` inventory. Each selected reflected/hybrid schema records qualified field names, local slots, type descriptors and its qualified superclass. Containers retain child type descriptors, including native bindings inside arrays, maps, sets and optionals. Recursive schema references share inventory entries rather than expanding indefinitely. Native whole-value layouts do not pretend that their unused reflected members were serialized.

The inventory is separate from `properties`. The existing `type`, `typeName`, `schemaIndex`, `nativeLayout`, `binaryLayout`, `structTailLayout` and whole-catalogue fingerprints remain inspectable; their provenance is not counted as a stored value edit. `serializedZero` remains an observed storage fact. Inventory preparation shares the read's work, text, depth and cancellation budgets. Consequently `values_read`, `text_bytes` and host charge callbacks include this preparation; they are not counts of logical properties or a process-memory cap.

## Native program identities

`BinaryLayouts::definitions(options)` returns a definition per catalogue-local layout name. Its Merkle SHA-256 covers the selected program/interpreter contract, ordered record member names, node types, counts, optional markers, effective narrow encodings and referenced program definitions. Renaming a local layout, rewriting an equivalent `ref`, or adding unrelated catalogue entries does not change its identity. Changing actual member names or wire interpretation does.

A custom reference callback needs a stable printable ASCII `reference_identity` of at most 256 bytes to certify correspondence. Direct decoding with an unnamed callback remains supported. Its reference-dependent definitions have an empty identity, including through `ref`/record/array/optional nodes; unrelated scalar programs retain their identities. The source registry continues to require an explicit callback identity and retains its existing binding-hash contract. `UnversionedReadOptions` now has the same optional identity; Zen supplies the built-in package-index resolver contract. Equal identities describe the supplied interpretation, not evidence that a particular producer used it. `mappingVerified:false` remains truthful.

## Comparisons

`compare_zen_packages` compares object tables and complete opaque export hashes as before. Matched, decoded property regions use their retained inventories:

- Equivalent selected programs compare values despite different layout names or whole-catalogue hashes.
- Incompatible field types/routes/programs produce `unavailableScopes`, rather than invented value edits.
- Hybrid reflected prefixes remain independently comparable when their native tails differ.
- Reassigned field slots, including inherited offsets, remain `cooked-schema-slot-incompatible`; identical bytes cannot become named value edits just because a mapping moved a field.
- Schema field names absent from the peer inventory are unverified; they are not silently treated as removed/added stored overrides. Omitted fields whose definitions do correspond remain actual stored override additions/removals.
- A failed, unbound or legacy decoded peer without retained interpretation cannot turn readable properties into removals. Both decoded values remain available for inspection where present.
- Empty containers do not attest that an element reader executed. An incompatible key type guards actual entries/removals while preserving unrelated properties and the stored replacement marker.

An unavailable property correspondence yields `status:incomplete`, even when there are known table/body/property changes. CLI diff exits 1 for that result. `contextChanged` remains independent of binary changes, known changes and uncertainty. Resolved class/import table behavior keeps its existing contract. External bulk and full native export bodies are not declared semantically complete by a property diff.

## Stored keys and views

Map/set `entries` and `removed` IDs use bounded incremental canonical JSON of the stored decoded key. At actual reflected field wrappers, only the decoded `value` enters identity. Schema slots, zero markers, type annotations, catalogue labels and fingerprints do not become key members. Qualified field names remain significant. A hybrid's reflected prefix follows this rule; builtin/declarative native output stays raw, including members named `type`, `value`, `schemaIndex` or `binaryLayoutsSha256`. Compatible key definitions are checked separately before membership is compared. This is correspondence of stored deltas, not Unreal runtime `GetTypeHash`, all FName/string equality rules, reconstructed map contents or custom plugin key equality.

`cooked_property_coverage(beforeRead, afterRead)` converts retained `propertyRead` records into `ValueCoverage` for flat and paged inspection. A null pointer is an absent object; a present unbound/opaque record is unread. Native values and structured primitive/reference results use raw JSON semantics; reflected wrappers retain the existing display/navigation paths. Actual wrapper annotations remain context, and native data with annotation-like names remains data. The desktop uses this coverage in both property inspectors and retains the same masks when navigating a page. Existing EN/FR Unknown/Context labels and workspace schema apply.

## Evidence and limits

An independently linked frozen 0.193 SDK reproduces same-byte false diffs: one row for an unrelated catalogue addition, two for an equivalent local layout rename and five for a changed native program's member name. Current controlled tests retain zero known changes for these cases; the changed program instead has one unavailable native-tail scope. A separate frozen-SDK counterexample produces four false known rows when two same-typed schema fields exchange slots on identical bytes; current comparison instead retains two unavailable field scopes. A genuine native scalar edit retains its named value change and complete body-hash change. Other cases cover hybrid arrays, nested reflected/native map and set keys, removals, mapped values, field renames, failed peers, explicit reference contracts, budget accounting and flat/paged UI coverage. These original controlled streams are not Unreal-produced packages.

Regression inspection of twelve actual UE 5.3 cooked corpus packages preserves every previously returned stored property and the same decoded/failed export counts (38 decoded exports, zero property failures). This is evidence for those selected packages; it does not certify every mapping field, native program or producer. The wider default/archetype, projector-owned cross-format correspondence and producer acceptance work remains open.
