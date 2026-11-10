# Stored package metadata

The standalone Core reads the native `UMetaData` suffix in explicitly selected editor source packages. It requires no Engine runtime, project module or exporter to inspect saved data. This reader is separate from UStruct/FProperty declaration metadata and from effective property defaults.

## Supported profile

The export class must be exactly `/Script/CoreUObject.MetaData`. The selected archive profiles are UE4 version522, UE5 version1009 (5.3) or1013 (5.5), licensee version0 and Editor custom version3–40. Editor-filtered and unversioned packages are not selected. The native field order was checked against the matching 5.3/5.5 serializers; independent Engine-produced/API comparison currently covers UE5.3.2 Windows. Wider producers and custom serializers still require evidence.

After the tagged property terminator the reader consumes the optional UObject GUID, the weak-object metadata maps using package references, and the root FName/FString map. The entire native suffix must be consumed. Unknown profiles and damaged data retain diagnostics and opaque byte fingerprints; no partial metadata tree is published.

## Public values and differences

`ReadOptions::native_metadata` defaults to true. Successful exports expose `native_coverage = "package-metadata"`, exact native start/end offsets and `native_data`:

```json
{
  "kind": "package-metadata",
  "scope": "stored",
  "objectValues": {
    "\"Asset.Child\"": {
      "object": "Asset.Child",
      "values": {"[\"Category\",0]": {"name": {"base": "Category", "number": 0}, "value": "Tools"}}
    }
  },
  "rootValues": {
    "[\"PackageLocalizationNamespace\",0]": {
      "name": {"base": "PackageLocalizationNamespace", "number": 0},
      "value": "stored namespace"
    }
  }
}
```

Optional `objectGuid` is the four stored GUID words in 32 lowercase hexadecimal digits. Object keys are compact JSON encodings of resolved package reference paths, or `null` for a stored null weak reference. Name keys are compact JSON `[base,internalNumber]` encodings. A literal `Marker_1` with internal0 remains distinct from `Marker` with internal2, although both display as `Marker_1`. JSON Pointer escaping still applies to report paths containing `/` or `~`. Storage order and name-table indices do not determine semantic map order.

Use ordinary `inspect`, `diff`, `git-inspect` and `git-diff`; metadata differences appear under `/objects/<export>/native/rootValues/.../value` or `objectValues/.../values/.../value`. The SDK exposes the same data through `Package`. The existing EN/FR Properties inspector browses these native values and differences. Disabling the reader retains the suffix as opaque. Explicit host native-export decoders have precedence, including when a selected host decoder fails. Cache profiles include the metadata selection.

## Text, limits and interpretation

UTF16 strings are validated and converted to UTF8. An unspecified narrow encoding accepts ASCII metadata strings only. Non-ASCII narrow data needs the host's explicit ASCII/Latin1/Windows-1252/UTF8 selection; no code page is guessed. Name-table strings follow the package reader's selected encoding. Stored case, Unicode and text values are preserved.

Native windows and aggregate dynamic key/reference/value text use `max_property_bytes`; map traversal shares `max_properties`, individual strings obey `max_string_bytes`, and reference outer chains are bounded to128. Cooperative cancellation propagates. Invalid counts, references, names, GUIDs, duplicates, strings, truncation and trailing bytes fail transactionally. These are decoding budgets, not an exact process-heap accounting guarantee.

The output describes **stored maps**. Unreal may redirect metadata keys while loading; runtime key equality, redirects and post-load edits are not simulated. Reference paths use the existing package reference contract; this reader does not introduce a universal component-aware object identity model. A successfully decoded metadata export does not certify unrelated native exports, inherited defaults, cooked content or arbitrary plugin serialization.

## Independent validation helper

The optional MIT Engine module provides `CapturePackageMetadata(Object)` for an explicitly selected, already loaded UMetaData object. It reads the public native maps and existing GUID without creating metadata, loading additional objects or saving assets. Its `cyuediff.engine-package-metadata/1` output identifies loaded API maps **after Engine redirects**. Bounded capture reports `completeCapture`; incomplete captures omit both maps. The Core does not depend on this module.

Five actual UE5.3.2 packages compare exactly with independently captured API maps for the selected non-redirected fixture keys: root/object/subobject metadata, same-display FName keys, escaped paths, Unicode names/values and an optional UObject GUID. Original generated wire tests separately exercise damaged input, profile gates, budgets, cancellation, public source/split reads, cache profiles and host-decoder precedence. Validation results and broader outstanding work are recorded in [VALIDATION.md](VALIDATION.md) and [COMPLETION.md](COMPLETION.md).
