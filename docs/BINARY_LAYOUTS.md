# Declarative binary layouts

`<cyuediff/binary_layout.hpp>` adds immutable, application-supplied descriptions of serialized little-endian bytes to `CyUEDiff::Core`. A layout describes a wire format; it does not derive C++ member offsets or discover a plugin serializer. No engine or plugin code is loaded.

```json
{
  "format": "cyuediff.binary-layouts/1",
  "layouts": {
    "Weather": {
      "type": "record",
      "fields": [
        {"name": "intensity", "value": {"type": "f32"}},
        {"name": "preset", "value": {"type": "name"}},
        {"name": "samples", "value": {
          "type": "array", "count": "i32", "element": {"type": "f32"}
        }}
      ]
    }
  },
  "decoders": [{
    "id": "mygame.weather/1",
    "kind": "struct",
    "target": "WeatherSettings",
    "profile": {"ue4": [522, 522], "ue5": [1012, 1013]},
    "layout": "Weather"
  }]
}
```

`BinaryLayouts::read(path)` parses bounded UTF-8 JSON and rejects duplicate keys. `parse_text` accepts JSON text; `parse` accepts an already parsed JSON document. The latter cannot detect duplicate keys already discarded by the caller. Unknown fields, unsupported types, invalid version ranges, missing references and cyclic layout references are rejected. The SHA-256 identifies compact canonical JSON, so whitespace does not change it. This is a description identity, not proof that the description matches a build.

A layout node has one of these forms:

| Type | Fields / value |
| --- | --- |
| `u8`, `i8`, `u16`, `i16`, `u32`, `i32`, `u64`, `i64` | Exact-width integer |
| `f32`, `f64` | Finite floating-point number |
| `bool8`, `bool32` | Unsigned 0/1 marker |
| `string` | FString, ASCII narrow or validated UTF-16 with its terminator |
| `name`, `object`, `guid` | FName, package object reference or GUID resolved by the host's `DecoderInput` |
| `record` | Ordered `fields`: each has a nonempty unique `name` and a `value` node |
| `array` | `count` and `element`; count is a uint32 constant or `"i32"` for a nonnegative signed 32-bit prefix |
| `bytes` | Same `count`; returns exact bytes as lowercase hexadecimal text |
| `optional` | `presence` is `bool8` or `bool32`; `value` is read only when present; returns `isSet` and an optional `value` |
| `ref` | `layout` selects another named layout in the same catalogue |

All fields are required unless documented otherwise. Empty records and zero-length arrays are valid. A scalar layout can be named and referenced. Descriptions do not implement seeks, arbitrary expressions, filesystem lookup, compression, native C++ execution or arbitrary serializer control flow.

`decoders` is optional. Each binding requires `id`, `kind`, exact `target`, `profile` and `layout`. Kind is `struct` or `native-export`. The profile requires inclusive `ue4` and `ue5` file-version intervals; optional `customVersions` maps lowercase 32-digit GUIDs to inclusive intervals. These are archive versions, not marketing engine versions. `decoder_registry()` creates the existing immutable `DecoderRegistry`; ambiguous matching profiles and malformed values retain the original opaque range. Array elements use the existing enclosing-window contract. A layout must consume an entire directly tagged structure or native export suffix.

```cpp
auto layouts = cyuediff::BinaryLayouts::read("layouts.json");
cyuediff::ReadOptions options;
options.decoders = layouts.decoder_registry();
auto package = cyuediff::Package::open("Example.uasset", options);
```

Definition defaults: 2 MiB JSON and decoded text, 100,000 JSON nodes, depth 48 and 64 KiB strings. Value defaults: 16 MiB consumed input, 100,000 work nodes, depth 48, 4 MiB decoded text and 64 KiB per string. Input byte counts and raw FString lengths are checked before reading their contents. Record names and byte-value hex text consume the text budget. These are bounded inputs/work/output components, not a bound on the whole process heap. The caller owns input stability and any memory used by its `DecoderInput` resolvers. Definitions and values accept cooperative cancellation. An optional `charge` callback integrates an enclosing budget and includes failed reads. Values are returned only on success; the supplied input cursor can advance on failure.

The native suite checks all truncated prefixes of a nested record, scalar widths, UTF-16 pairs, invalid floats/markers, cycles, counts and budgets, cancellation and recovery of a following tagged property for five archive profiles. These original fixtures validate the generic mechanism, not an unknown real plugin's wire format.

Schema-backed cooked reads can also use the same descriptions through version-three Zen contexts. See [schema bindings](SCHEMAS.md#declarative-native-layouts--0174). `BinaryLayoutReadOptions::reference` optionally returns a bounded ordinary JSON identity for a signed package index; without this override `DecoderInput::reference()` supplies the existing string identity. A zero index with the JSON resolver returns null without invoking it. Resolver-returned values share work/depth/text checks. Engine-specific implicit native defaults remain unspecified.

## CLI and local Git selection — 0.175

```powershell
cyuediff layouts layouts.json
cyuediff inspect Before.uasset --layouts layouts.json
cyuediff diff Before.uasset After.uasset --layouts old-layouts.json --after-layouts new-layouts.json
cyuediff git-diff D:/repo HEAD~1 HEAD Content/Asset.uasset --layouts old-layouts.json --after-layouts new-layouts.json
```

`inspect`, `diff`, `git-inspect` and `git-diff` accept `--layouts`. `diff` and `git-diff` accept `--after-layouts`; omission inherits the before catalogue. An explicit empty catalogue selects no custom decoders. The `layouts` command validates a catalogue and reports its definition, canonical SHA-256 and decoder count without opening an asset. Selected catalogue files are protected from output overwrites, including hard links. Unsupported command/option combinations and metadata-only inspection with explicit layouts are rejected.

Reports with explicit selections include `layoutContexts.beforeSha256`, `afterSha256` and `contextChanged`. This provenance is separate from binary asset changes. Existing commands without these selections keep their report shapes. Descriptions do not load a DLL, execute arbitrary callbacks supplied by JSON or launch Unreal. Cooked commands continue to use `--schema` and version-three inline descriptions.

The Git adapter additionally exposes overloads of both `compare_git` forms with separate before/after `ReadOptions`. Existing single-options overloads retain their behavior. Local LFS materialization and input ownership stay in the Git adapter; descriptions are local host configuration and do not come from or execute historical project code.

## Desktop descriptions — 0.176

The **Native structure layouts** section accepts a before JSON catalogue and an independent after catalogue. **Use before layouts after** is enabled by default. Disabling it with an empty after path selects the built-in reader for that side. These controls and the section state have English/French labels. Load/compare runs on the existing worker; drawing does not open descriptions or execute serializers.

Desktop startup accepts `--layouts FILE`, `--after-layouts FILE` and `--no-after-layouts`. A before/after side swap carries the effective description with its file. Reports preserve catalogue identities separately from asset changes. Both loose and historical local Git inputs use independent registries. Explicit external Skeleton bindings use the corresponding side's reader. Description inputs are protected from report/workspace overwrites.

Workspace format five stores description paths and inheritance per document. It still reads formats one through four with the original default reader; relative description paths resolve beside the workspace. Codec executables remain outside persisted settings. Cooked documents use the existing schema controls with version-three inline descriptions, including the typed property inspector and named comparisons. Standalone description paths on a cooked request are rejected instead of silently ignored.

## Explicit narrow string encodings — 0.177

Descriptions can declare optional top-level `narrowStringEncoding`: `ascii`, `latin1`, `windows-1252` or `utf8`. A `string` node can additionally select an `encoding`, overriding the catalogue for that field. UTF-16 FString payloads retain their existing decoding. No system locale, statistical detection or replacement character is used. Windows-1252's five undefined byte values and malformed UTF-8 fail explicitly. Conversion preflights the exact UTF-8 output length against the caller's budget.

The CLI and desktop apply the selected catalogue encoding to tagged narrow FStrings, stored FText strings and supported counted ANSI properties, as well as declarative fields. A catalogue containing an encoding and an empty `layouts` object can select an encoding without overriding native serializers. Per-side selections and workspace restoration carry these settings through the existing immutable catalogue identity.

Version-three Zen contexts accept optional `narrowStringEncoding`; absent an explicit context value, they inherit the inline catalogue's selection. It applies to supported mapped narrow FString/ANSI values. The selected encoding affects the cooked context fingerprint. Existing contexts and unselected classic reads retain their previous behavior. C++ clients can set `ReadOptions::narrow_string_encoding`, `UnversionedReadOptions::narrow_string_encoding` and the corresponding Zen context field, or use the standalone `<cyuediff/string_encoding.hpp>` converter. The package cache distinguishes encoding profiles.

Selecting a codepage is a host declaration about serialized bytes, not a claim that stock Unreal writes non-ASCII ANSICHAR in that codepage. The scope excludes container filename conversion and automatic build encoding discovery. Tests verify every Latin-1 byte, strict ASCII/UTF-8, Windows punctuation/undefined codes, decoded-size limits, shared schema budgets, cache separation and tagged/cooked values. Python's standard `cp1252`/`latin1` codecs independently check public CLI/desktop values.


## Tagged prefixes — 0.185

Optional decoder field `prefix: "tagged-properties"` certifies a source struct's reflected prefix followed by its declared native layout. It applies only to `kind: struct`; omitted prefixes retain whole-value behavior. Unsupported prefix values and prefix-bearing native export registrations are rejected. The prefix and native result are separate, named stored values, sharing enclosing work/depth/text checks. Trusted host callbacks can consult the borrowed `DecoderContext::tagged_properties` to select a conditional tail. The existing independent CLI/Git/desktop catalogue controls require no new settings. See [HYBRID_STRUCTS.md](HYBRID_STRUCTS.md#tagged-source-structures--0185) for the wire contract, actual producer evidence and missing legacy map-type/default limitations.
