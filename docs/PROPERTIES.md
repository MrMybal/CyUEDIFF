# Tagged properties and native values

The reader preserves Unreal property tags, decoded values, unknown byte ranges and coverage diagnostics. It does not instantiate a UClass or apply defaults from a parent or plugin. Unknown native layouts stay opaque and remain comparable by hash.

In addition to primitives, arrays, common vectors/colors and GUIDs, the reader supports native `Transform` (rotation, translation, scale), `DateTime` and `Timespan` ticks. Transform precision follows the supported LWC serialization boundary. Exact property windows prevent a decoder from consuming adjacent values.

`TextProperty` supports localized base histories (flags, namespace, key, source string), culture-invariant/empty histories, and string-table references (table ID and key). The invariant-string flag is read only for the corresponding editor custom version (32+). It never loads a string table, consults a translation service or evaluates a formatted text history. Other histories retain opaque bytes with a `text-history` diagnostic; following fields remain readable.

Maps and sets expose their **stored deltas** rather than fabricated final values. `removed` contains explicit removed keys/elements; `entries` contains stored additions or replacements. Keys use canonical serialized JSON identities so iteration-order changes are ignored. Duplicate identities are rejected as opaque, preserving the original bytes. Modern full property types support nested decoded native/tagged values. Legacy container tags without enough struct/enum type information require a schema; the reader does not guess a missing subtype. Entry counts share a bounded per-container budget.

Property extension operation/experimental flags are retained. Experimental overridable value streams remain opaque until an appropriate backend exists. Unversioned property streams still require a schema backend. Selecting a project or engine root does not substitute a serializer version or create such a schema automatically.

Synthetic checks cover UE5 serializer versions 1009 and 1012, reordered maps/sets, removal deltas, localized/invariant/table/empty texts, unknown text histories, transforms, ticks, duplicate keys and experimental overrides. In the supplied 150-package CyTrace UE 5.3 corpus, fully decoded tagged-property exports increased from 11,575 to 12,091; partial exports decreased from 818 to 302, and three exports remain opaque. These counts describe tagged-property coverage, not complete native mesh/Blueprint/cooked decoding.
