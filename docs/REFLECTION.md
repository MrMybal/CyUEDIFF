# Serialized field declarations

The development Core reads the declared `FProperty` definitions stored in supported `UStruct` export prefixes. This includes Blueprint-generated classes and their serialized functions, script structures, user-defined structures and selected generated class families. It does not start Unreal or load plugin binaries.

```cpp
#include <cyuediff/reflection.hpp>
auto package = cyuediff::Package::open("Blueprint.uasset");
auto declarations = cyuediff::reflection_definitions(package);
```

`inspect FILE` includes the same `reflection` projection. Each decoded object's `native` section also appears in semantic snapshots, general named diffs and the existing typed desktop property inspector. The installed [standalone consumer](../examples/reflection_consumer) links only `CyUEDiff::Core`.

## Contract

The projection schema is `cyuediff.reflection/1`. `definitions` is keyed by exact exported object path; its `declaration.fields` map is keyed by stored field name. `fieldOrder` independently preserves serialization order. Renumbering the package name table does not change these identities.

Each declaration retains its superclass and child object references, optional object GUID, editor metadata presence, field class/name, object/property flags, array dimension, serialized element size and replication metadata. Supported fields include integer/float/name/string/text types, object/class/soft/weak/lazy/interface references, struct/byte/enum declarations, bool layouts, field paths, delegate signatures and recursively declared arrays, sets, maps and optionals. Nested declarations retain their own names and metadata. Full 64-bit property flags are preserved.

These are declarations, not instance values or memory layouts. `elementSize` is stored evidence; it is not an inferred serialized payload length or offset. Inherited fields and archetype defaults are not reconstructed. Script bytecode and class/function-specific tails remain separately opaque and fingerprinted.

## Profiles and failures

The native prefix reader currently requires UE5 file version 1000 through 1013, Core custom version 4 (`FProperties`) and Framework custom version 29 through 37 (`RemoveUField_Next` onward). It recognizes explicit CoreUObject Class/ScriptStruct/Function, Engine BlueprintGeneratedClass/AnimBlueprintGeneratedClass/UserDefinedStruct and UMG WidgetBlueprintGeneratedClass identities. Other classes use existing readers or explicit host decoder layouts.

Editor prefixes include the serialized field metadata marker/map. Packages carrying `PKG_Cooked` omit those bytes and report metadata as unavailable. The cooked-prefix layout has original fixture coverage; real validation below covers editor assets. Unversioned property packages and Zen export bodies still require their own explicit boundary/schema context; this reader does not enable cooked native decoding by searching for fields.

An unknown profile/type, duplicate field name or metadata key, invalid reference/layout/boolean, truncated field or exhausted budget discards the entire declaration. The original native range remains opaque with a diagnostic. The reader observes export/byte/value/depth limits and cancellation. `ReadOptions::native_reflection=false` disables the reader and participates in cache identity. Explicit registered native decoders retain precedence.

`complete` means all recognized declarations in a complete package input were available. It does not certify all properties, inherited fields, bytecode, native payloads or the entire asset. Unavailable recognized objects are listed with their native coverage.

## Evidence

The source layout was checked against locally available UE 5.5 `UStruct::Serialize`, `UStruct::SerializeProperties`, `FField::Serialize` and the relevant property serializers. Epic's [UStruct API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/UObject/UStruct?application_version=5.5) describes the reflected struct type. Engine source is not included in this repository.

The original fixture suite checks all supported field classes, editor/cooked metadata branches, nested types, optional GUIDs, profile boundaries, declaration diffs, invalid layouts and every truncated prefix. The CLI suite checks inspection, named differences, split exports and source preservation.

`python scripts/validate_reflection.py --input HOST_CONTENT --cli PATH_TO_CLI --output-dir build-native/reflection-validation` compares decoded definitions and exact consumed boundaries with an independent Python wire reader. On the authorized local UE 5.3 editor corpus, 33 assets yielded 123 declarations: 29 Blueprint classes, one widget class and 93 functions, containing 704 top-level fields of 12 classes. All 246 value/boundary comparisons passed. A controlled flag edit in a private copy of one real Blueprint produced exactly one named `propertyFlags` difference; original hashes were unchanged. This is independent parser validation of the same stored assets, not an Unreal runtime or inherited-default comparison. Private metadata, original files and reports remain under ignored local build directories.

## Remaining coverage

Stored declarations do not contain arbitrary native C++ serializer logic. Native plugin structures, stripped compiled native-class definitions, inherited/default resolution, older field-object formats, newer custom versions, Zen native integration and reconstruction of a matching cooked schema remain separate work. No USMAP file or universal serializer is inferred from this projection.
