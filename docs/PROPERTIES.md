# Tagged properties and native values

The reader preserves Unreal property tags, decoded values, unknown byte ranges and coverage diagnostics. It does not instantiate a UClass or apply defaults from a parent or plugin. Unknown native layouts stay opaque and remain comparable by hash.

In addition to primitives, arrays, common vectors/colors and GUIDs, the reader supports `DateTime` and `Timespan` ticks. For modern LWC packages, the aliased `Transform` retains its nested tagged Rotation/Translation/Scale3D properties. Explicit immutable `Transform3f` and `Transform3d` use their 40/80-byte binary profiles; they are not inferred from the alias or an in-memory size. The pre-LWC alias retains its existing legacy profile. Exact property windows prevent a decoder from consuming adjacent values. Missing reflected channels stay missing.

`TextProperty` supports localized base histories (flags, namespace, key, source string), culture-invariant/empty histories, and string-table references (table ID and key). The invariant-string flag is read only for the corresponding editor custom version (32+). It never loads a string table, consults a translation service or evaluates a formatted text history. Other histories retain opaque bytes with a `text-history` diagnostic; following fields remain readable.

Maps and sets expose their **stored deltas** rather than fabricated final values. `removed` contains explicit removed keys/elements; `entries` contains stored additions or replacements. Keys use canonical serialized JSON identities so iteration-order changes are ignored. Duplicate identities are rejected as opaque, preserving the original bytes. Modern full property types support nested decoded native/tagged values. Legacy container tags without enough struct/enum type information require a schema; the reader does not guess a missing subtype. Entry counts share a bounded per-container budget.

Property extension operation/experimental flags are retained. Experimental overridable value streams remain opaque until an appropriate backend exists. Unversioned property streams still require a schema backend. Selecting a project or engine root does not substitute a serializer version or create such a schema automatically.

Synthetic checks cover UE5 serializer versions 1009 and 1012, reordered maps/sets, removal deltas, localized/invariant/table/empty texts, unknown text histories, transforms, ticks, duplicate keys and experimental overrides. In the supplied 150-package CyTrace UE 5.3 corpus, fully decoded tagged-property exports increased from 11,575 to 12,091; partial exports decreased from 818 to 302, and three exports remain opaque. These counts describe tagged-property coverage, not complete native mesh/Blueprint/cooked decoding.

## Field diagnostics and reflected transforms — post-0.181

Nested unversioned read errors preserve their specific code, absolute input offset and nearest qualified field names. Diagnostic text is capped at 4 KiB on UTF-8 boundaries, including messages from host resolvers; cancellation and operation-wide budgets propagate without being converted to an opaque result. Zen records the error offset when discarding an unsuccessful property projection. No partial properties become a valid decoded export.

A modern aliased Transform has ordinary reflected serialization in the selected matching engine; the existence of a C++ archive operator does not make it a native property serializer. Its tagged channels therefore remain typed properties in a loose snapshot and use an explicitly reflected nested header in a cooked mapping. The native `transform-f32`/`transform-f64` schema wire layouts remain available for a host that certifies an actual binary serializer; binding them to a modern aliased Transform merely by name is incorrect.
