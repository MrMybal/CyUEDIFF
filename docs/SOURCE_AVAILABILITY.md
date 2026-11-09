# Source comparison availability

Source development 0.189 separates known stored changes from unread values and incompatible decoded representations. It applies to tagged `Package` comparisons, local Git source comparisons, and the reusable EN/FR source desktop. It does not infer constructor/archetype defaults or evaluate an Unreal asset at runtime.

## Known, absent and unavailable

A complete property-tag inventory can contain an unread individual value. Other readable fields remain comparable. A wholly opaque/metadata/missing export is not an empty property map. A failed native projection is not a native member deletion. UE property serialization flag `Skipped` means no value was serialized; its placeholder is not a known null/default. An actual stored null without that flag remains known.

When a matched value is unread on either side, comparison omits its invented value removal/replacement. Stored tag/type/GUID/flag changes remain visible. Byte count and SHA-256 of opaque regions remain evidence when available. Equal hashes and matching stored/effective tag identities can establish unchanged stored bytes/markers, while the underlying value stays unread. Changed/missing hashes cannot establish semantic equality.

A native decoder ID or representation change is decoding context. Both outputs can be readable while their member correspondence is unknown. Incompatible effective source types also make value correspondence unverified; equivalent public descriptor string/object forms are normalized. Registered native JSON remains explicit data: fields named `type`, `opaque`, `path` or `resolvedType` are not reinterpreted as Unreal tags or decoder metadata. Hybrid structs retain their separately readable reflected prefix. Unknown reflected map keys, including removed keys, make the logical entry/deletion correspondence unavailable. Array size changes retain readable index differences alongside scoped unknown values.

## Reports and CLI

`compare(Package, Package)` retains known rows in the existing arrays and adds conditional `unavailableScopes`. Source entries identify the escaped JSON-pointer `path`, `object`, `reason`, `beforeUnavailable` and `afterUnavailable`, with optional fingerprints or decoder IDs. Format/key correspondence failures can have both unread flags false: the values are readable, but correspondence is unverified. Projected entries instead identify `domain`, `path` and the dependent-projection reason.

`projectionComparisonComplete` means the available stored projection comparison has no unresolved difference scope and no failed input/read guard. It can be true for identical opaque bytes. It does **not** mean every field was decoded, all defaults were reconstructed, or two runtime assets are equivalent. `completeInput` continues to describe required input availability. Available whole-input fingerprint facts remain independent.

Any unresolved source/projection scope makes the general report `unverified` (or `incomplete` for missing required input). CLI `diff`/Git comparison exit 2 in that case, even when readable fields changed. Exit 1 means known changes within the existing complete comparison contract; exit 0 means identical within that contract. A failed source read limit/count guard retains the existing stronger rule: no semantic rows, explicit read failure and available binary facts.

Optional `sourceContexts` records independent before/after archive versions, saved Engine/custom versions, source type metadata and native catalogue fingerprint. These are provenance, separate from stored asset rows. Read policies keep their existing separate provenance. No installed Unreal or plugin binary is executed by comparison.

Dedicated `compare_materials`, source `compare_collisions` and `compare_skeletons` share the dependent-source guard, expose `unavailable_scopes`/`unavailableScopes` and remain `incomplete` where correspondence is unavailable. The cooked overload keeps its explicit mapped-data contract. Other focused operations keep their separately documented completeness contracts.

Raw `semantic_snapshot()` and single-file `textconv` remain observational representations. They cannot carry paired unknown-side semantics; use the structured comparison report to decide whether a change/equality is verified. Ordinary text differences between snapshots prepared with different readers are not proof of a stored value deletion.

## Reusable property views

Compile hosts against matching 0.189 headers and libraries: `DiffResult`, specialized comparison types, `PropertyRow`, `PropertyTableOptions`, `PropertyPageOptions`, `ChangeCounts` and reusable view layouts changed.

`ValueCoverage` accepts actual JSON-pointer scopes:

- `before_unavailable` / `after_unavailable`: unread data on the named side.
- `comparison_unavailable`: readable values whose correspondence is unverified.
- `ignored`: decoding provenance/context retained for inspection.
- `raw`: explicit native JSON namespaces where property-tag interpretation is disabled.

`property_table` and `property_page` use these masks and bounded/cancellable comparison work. Expanded rows retain their previous logical `path` for saved navigation and add actual stored `source_path`; pages use actual pointers. `before_available` / `after_available` distinguish a readable value from its placeholder, independently from presence. `context_only` identifies context rows. `ChangeKind::unknown` and `ChangeCounts::unknown` stay separate from known change counts.

The installed `property_page_consumer` accepts optional JSON pointer arrays in `COVERAGE.json`. It reports the availability, context and stored-pointer fields. Coverage is supplied by the host's evidence; this is not a schema discovery API. Invalid scopes, excessive work/paths and cancellation fail explicitly. Comparison node/report budgets do not cap the process heap.

## Desktop

The source `AssetView` binds immutable paired availability before preparing property tables/pages. Known fields keep exact before/after values; unread values show **Unavailable** / **Indisponible**, with **Unknown** / **Indéterminé** change state. Context is labelled independently. Stored strings retain their own language. Copying an unread placeholder is disabled; copying a prepared path uses its actual stored JSON pointer. A new document clears stale coverage, while restored navigation rebinds the current packages.

Whole-document text comparison is unavailable for unresolved or differently decoded source pairs; the view directs review to scoped properties. Large valid inputs keep their existing separate size diagnostic. Blueprint and material components whose source dependencies are unknown retain single-revision inspection without misleading change outlines. Source skeleton/collision views suppress false removals for unavailable owners. Unrelated fully readable components remain comparable. Pending/failed source geometry preparation is distinct from an actually absent document, so an unprepared mesh peer cannot color all geometry, weights or morphs as deleted.

## Local validation

The complete local regression passes 165 Core suites and 259 Windows native/Git/desktop suites. Original availability tests exercise 45 source contracts, 18 value-coverage contracts and 39 reusable source-view checks across 20 English/French desktop/narrow frames. Existing navigation/state tests retain their actual changed-node and property-selection expectations; authored decoded fixtures now declare their complete property inventory explicitly. Metadata-only coverage remains authoritative even if a mutable host package contains stale properties.

An independently linked consumer from a fresh installed 0.189 Core validates the original Engine-reloaded plugin pair: 110 stored values, 24 hybrid structures, 18 integers beyond 2^53 and 21 named changes. With native codecs deliberately unbound on the after side, its independently referenced `AfterHybrid` value still changes from 987 to 988 while the unread structures stay unknown. The native EN/FR captures show this combination, and a workspace restart rebinds the same source coverage without user preferences. The large physical regression independently rechecks all 819,102 stored/API values through the current installed Core.

On actual Cube source assets, known stored changes survive one unresolved opaque region. Two changed configuration values match independent Unreal API references. Reading the exact same original bytes with native codecs unbound gives zero stored changes, two unknown scopes and `unverified`; current CLI and installed SDK reports agree. A separately linked property-page consumer retains three unknown rows without invented additions/removals.

The actual Humanoid pair produces 34,550 generic known rows plus one unknown scope. One observed CLI parse/comparison/JSON-output run takes about 3.47 seconds, peaks at 619,827,200 bytes of working set (about 591 MiB), and writes a 9,988,061-byte report. Other native regression jobs were running concurrently. This records an optimization baseline, not a speed guarantee or memory cap; aggregate/duplicated report memory still requires work.

## Deliberate limits

This is conservative availability handling, not a universal Unreal serializer. A dependent graph/material/body can lose visual comparison as a whole; its readable raw source fields remain available through scoped properties. It does not yet provide a complete per-pin/per-node uncertainty algebra inside every damaged projected component. Full source property defaults, arbitrary plugin native serializers, unknown struct-key canonicalization across distinct decoded representations, broader producer certification and aggregate/report memory remain separate work. Complete decoded map keys still follow the reader's explicit stored-key identity contract. Dedicated simulation/render/evaluation tools retain their own documented coverage.

See [source read policies](SOURCE_READ_LIMITS.md), [tagged type contexts](TAGGED_TYPES.md), [hybrid structures](HYBRID_STRUCTS.md) and [product completion evidence](COMPLETION.md).
