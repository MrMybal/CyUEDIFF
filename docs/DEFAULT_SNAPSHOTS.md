# Captured class defaults

The optional `tools/defaults_exporter` editor module captures an explicitly selected class default object (CDO) in its matching Unreal environment. `DefaultSnapshot` reads that JSON in the standalone Core. The consumer does not load Unreal, a plugin DLL, a project or another file referenced by the capture. This is an initial default-value reader, not completed asset-state reconstruction.

## What is captured

The original MIT exporter records qualified declaring owners, static-array indices, property flags, serialization eligibility, class inheritance, struct GUIDs and selected native/binary traits. It keeps the selected CDO's inherited field values separately from initialized struct defaults. A derived constructor can change an inherited field, and a class member can have different values from its struct initializer.

Each eligible field also has an actual `FProperty::SerializeItem` byte stream. The selected persistent memory archive uses unversioned properties, forced Unicode, a per-field name table, a per-field object reference table and **no comparison default**. Its policy records the exact engine version, UE4/UE5 archive versions, current custom versions and editor-only filter. The source/cooked asset's archive is a separate context; this memory archive is not presented as its wire layout.

Serialization uses a temporary initialized property copy. The tool does not save or search assets. Running constructors or a custom native serializer is work in the explicitly invoked trusted Engine environment; the exporter cannot impose a wall-clock limit on arbitrary plugin code. Byte, collection, recursion and reflection/text limits bound the captured document. A failed field stays unavailable; a failed overall capture is rejected.

## Standalone Core API

```cpp
#include <cyuediff/defaults.hpp>

cyuediff::DefaultSnapshotReadOptions options;
options.expected_engine_version = "5.3.2-29314046+++UE5+Release-5.3";
options.expected_ue4 = 522;
options.expected_ue5 = 1009;
options.values.text_layout = cyuediff::TextLayout::ue5_history;
// Select native layouts only with evidence for this captured archive.
options.values.native_structs["/Script/CoreUObject.IntPoint"] =
    cyuediff::NativeStructLayout::int_point;
auto defaults = cyuediff::DefaultSnapshot::read("class-default.json", options);
```

An exact producer version and filter are required. An optional lowercase `expected_sha256` verifies the capture file bytes. The parser rejects duplicate JSON keys, missing/ineligible/foreign fields, changed declarations, mismatched field/archive policies, malformed hex and partially consumed supported wires. JSON bytes/nodes/depth/text, total decoded wire bytes, aggregate value work/text and cancellation remain bounded. Aggregate host charges include failed unsupported attempts. The reader uses the capture's own name/object tables; a `values` resolver or name pointer is not used.

`fields()` exposes qualified, independently available decoded field values with their selected interpretation, wire hash and retained reference tables. `structures()` retains initialized struct field values. An unsupported native route remains unavailable; it does not prevent independent scalar fields from being decoded. Reflected routes are taken only from captured nonnative, nonbinary declarations. Exact qualified native or declarative routes remain caller supplied.

`UnversionedSchemas::from_runtime_capture` can also consume the qualified declarations without any default values. Its provenance is `cyuediff.runtime-schemas/1`, not an invented USMAP file. `decode_unversioned_value` reads one explicitly typed wire value, returning its actual consumption and selected interpretation. Unlike the snapshot reader, this composable lower-level function permits trailing bytes in the enclosing window.

The [installed SDK example](../examples/default_snapshot_consumer/README.md) exercises the public API without linking an Engine component. It exits 0 when every captured field wire decodes, 1 for unavailable fields, and 2 for a rejected capture or invalid argument. Decoding every field wire does not establish effective asset defaults.

## Binding and remaining work

Every snapshot explicitly reports `binding: "unbound"` and `defaultsApplied: false`. Reading a capture neither certifies that its native module matches another build nor binds a Blueprint CDO to the current dependency bytes. Ordinary instance snapshots are rejected. A user-provided fingerprint is a consistency check, not proof of the producer's authenticity.

Stored package/Zen properties stay unchanged. The current reader does not merge asset overrides, map/set deltas, omitted nested fields, archetype templates or instanced references. Those require compatible class/template/dependency bindings and the actual container/key equality contracts. In particular, matching an array index is not evidence for borrowing the old default element, and class-member defaults must not be replaced with generic struct initializers. No UI/CLI asset-context switch or automatic capture refresh is advertised by this milestone.

FName wire base/number tables are retained. Decoded display text alone does not certify case/number equivalence for arbitrary map/set keys. The private producer records a real case-preserving name display change across asset save/reload; native property equality confirms that its logical value survived. Broader producer/version acceptance remains required.

## Evidence

The selected UE 5.3.2 producer supplies two native class CDOs with source and editor-filtered captures. An independently configured installed Core consumer decodes 64 field instances across those four captures, including ten initialized-struct instances; 202 visited typed value nodes agree with separately reflected values. Evidence includes an exact integer above 2^53, derived-constructor inherited values, distinct class-member/type defaults, arrays, maps, sets, native math, invariant FText and object references. Four intentionally altered decoded reports fail that comparison. Six real ordinary instance captures are rejected as default contexts. Missing native bindings leave Point/Transform unavailable.

The original zero-override asset has no stored root properties while the matching captured CDO contains Count=41. This demonstrates why the missing-value problem matters; it is not a claim that the standalone Core already reconstructs that asset's full effective state.
