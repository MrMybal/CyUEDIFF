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
