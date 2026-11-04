# Cooked Zen object tables

Version 0.16 adds `<cyuediff/zen.hpp>` to the standalone Core SDK. It reads Zen export-bundle chunks from IoStore, resolves names and script classes, inventories objects and locates changed opaque bytes by object. It does not launch or link Unreal.

## Coverage and profiles

The supported header is the 52-byte Zen layout with export dependencies (Zen version 3), 72-byte export entries and 32-byte bulk-resource entries. Select a layout explicitly:

| Profile | Bulk map layout | Validation |
| --- | --- | --- |
| `ue5.3` / `ZenProfile::unreal_5_3` | Size-prefixed, unaligned bulk map | Real CyTrace UE 5.3 cooked build plus fixtures. Versioned fixtures require UE4 522 / UE5 1009, licensee 0. |
| `ue5.5` / `ZenProfile::unreal_5_5` | Recorded alignment padding, size-prefixed bulk map, aligned following tables | Synthetic fixtures and the locally available format definitions. Recorded versions are restricted to UE4 522 / UE5 1012–1013, licensee 0; no real UE 5.5 cooked corpus has been certified. |

`ZenProfile::unspecified` is rejected. Unversioned cooked headers do not identify their engine version, and their layout is not guessed from offsets or the currently installed engine. Other Zen layouts, licensee variants or recorded versions fail explicitly. These profiles do not certify every native asset format in those engines.

Implemented metadata includes batched ANSI/UTF-16 names, numeric FName suffixes, optional recorded/custom versions, imports/public export hashes, local/script/package references, exports, create/serialize commands, dependency groups, imported package names when present and bulk-resource descriptors. Invalid ranges, indices, command ownership, outer cycles, Unicode and overlapping exports are rejected. Export overlap is checked before hashing so corrupt tables cannot multiply payload hashing work.

Every export body remains opaque, even if `PKG_UnversionedProperties` is absent. `propertiesDecoded:false` is explicit in package/catalogue/diff reports. Unversioned property values need a matching schema such as a supported `.usmap`; neither schema loading nor cooked native texture/mesh/Blueprint payload decoding is implemented here. The classic loose-package property reader is separate.

## CLI

```powershell
cyuediff zen-catalog Game-Windows.utoc --cooked-profile ue5.3 --script-objects global.utoc --codec local_codec.dll --out cooked.json
cyuediff zen-inspect Game-Windows.utoc Game/Content/BP_Player.uasset --cooked-profile ue5.3 --script-objects global.utoc --codec local_codec.dll --out package.json
cyuediff zen-diff before/Game-Windows.utoc CHUNK_ID after/Game-Windows.utoc CHUNK_ID --cooked-profile ue5.3 --script-objects before/global.utoc --after-script-objects after/global.utoc --codec local_codec.dll --out diff.json
```

Select a package by its exact inventory path or chunk ID. `zen-inspect`/`zen-diff` require an IoStore chunk of type 1. `--script-objects` selects a container with exactly one global script-object chunk (type 5); omission retains unresolved script IDs and diagnostics. Supply the table belonging to the selected build. The table's SHA-256 is recorded as context provenance, not an authenticity guarantee. External package imports use a package name and public export hash when available; otherwise their package index remains explicit. This does not load imported packages automatically.

`zen-diff` accepts distinct `--after-cooked-profile` and `--after-script-objects`; omitted after options reuse the before context. A changed class resolved through the context is reported even when package bytes are unchanged. `contextChanged` also identifies a different profile/global table; an unrelated global-table change alone does not force an object-content change.

`zen-inspect --snapshot` emits the normalized object projection. `zen-catalog --snapshot` includes that projection in every package entry. The normal catalogue reports package names, export/class counts, property-versioning flags and fingerprints. `--max-packages N` limits examined packages, including failed ones; `--max-exports N` limits accepted export totals. Limits or individual read failures retain partial results with `complete:false` and exit code 2. Cancellation propagates as an error.

Diff exit codes are 0 for identical, 1 for changed or binary-only, and 2 for errors. Output guards protect both selected containers, all UCAS partitions and both script-object containers, including hard-link aliases. No member name becomes an extraction path.

## Object comparison

Objects are paired by their resolved outer/name path. Local reference indices are normalized to those paths; script references are resolved against the supplied table. Reordering an export table without changing the normalized metadata or object bytes therefore yields `binary-only`. Public export hashes are preserved as exact hexadecimal strings.

A diff separates readable header fields from opaque export bodies. A body edit appears under an object's `/opaque/sha256` path; it does not claim that a particular property, Blueprint instruction or texture pixel changed. Unclaimed payload gaps and trailing bytes are also fingerprinted. Equality covers the package chunk, not external bulk chunks, imported assets, shaders or a complete playable asset. `externalBulkCompared:false` remains explicit. The SHA-256 field values are measured fingerprints; the container reader's existing BLAKE3/signature limitations still apply.

## Core API

```cpp
#include <cyuediff/zen.hpp>

cyuediff::ZenReadOptions options;
options.profile = cyuediff::ZenProfile::unreal_5_3;
cyuediff::ContainerReadOptions bulk;
bulk.decoder = cyuediff::container_block_decoder(
    cyuediff::load_block_decoder("local_codec.dll")); // Omit for copy blocks.

auto container = cyuediff::Container::open("Game-Windows.utoc");
auto package = cyuediff::read_zen_package(container, package_entry_index, options, bulk);
auto report = package.to_json();
```

Hosts load a global table with `ScriptObjects::parse(global_container.read(script_entry_index, bulk), options)` and assign a shared immutable copy to `options.script_objects`. `ZenPackage::parse(bytes, options)` also accepts already decoded bytes directly. `compare_zen_packages` and `catalog_zen_packages` operate independently of UI and Git. The installed-SDK example `examples/zen_consumer` uses copy blocks and an explicit profile.

Default parse limits are 256 MiB of input, 64 MiB of header bytes, one million entries per table, 64 KiB serialized names/resolved paths, depth 128 and a 64 MiB text budget. The text budget includes resolved references; it is not an aggregate heap reservation. Payload range hashes read directly from the input vector without making an export-sized copy. Metadata/JSON containers and snapshots coexist in memory.

`ZenCatalogOptions` adds limits for examined packages (100,000), accepted exports (one million) and an estimated report budget (128 MiB). String estimates conservatively allow JSON escaping; a final compact limit diagnostic is retained beyond the record budget. Detailed snapshots are opt-in. The catalogue has no persistent cache yet.

`ZenReadOptions::control` applies during parsing, hashing and table traversal. `read_zen_package` combines it with `ContainerReadOptions::control` across reading and parsing; each configured progress callback receives events. Shared script tables and parsed package results retain no callbacks or codec buffers. Container files must remain stable during a read as described in [CONTAINERS.md](CONTAINERS.md).

## Validation

Native fixtures cover profiles 5.3/5.5, optional version/custom metadata, numbered/mapped names, UTF-16 surrogate pairs, imports, local/script contexts, dependencies, command ordering, reordered exports, opaque/gap edits, corruption, limits and cancellation. CLI tests cover distinct before/after tables, catalogue limits, Unicode paths and protected output/hard links.

The supplied CyTrace corpus contains 3,507 unversioned packages and 7,502 exports. A separate Python parser independently reconstructs their names, classes, object references, import/bulk metadata and opaque body hashes using the same explicitly supplied codec for decompression. Its global table resolves 31,616 script objects. A one-byte edit on a copy of a real Blueprint's opaque body yields exactly one changed object fingerprint; self-comparison is identical. Original container hashes are unchanged. This validates object-table/opaque-body coverage, not decoded unversioned values.

```powershell
python scripts/validate_zen.py --container D:/build/Game-Windows.utoc --global-container D:/build/global.utoc --codec D:/local/local_codec.dll
```

The validator currently targets the supplied unversioned UE 5.3 corpus. Generated reports and modified copies remain under an ignored workspace build directory. Proprietary packages and engine files are not distributed.
