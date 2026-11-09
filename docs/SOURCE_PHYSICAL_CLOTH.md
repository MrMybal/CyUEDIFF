# Tagged source physical cloth

Development 0.188 adds an explicit UE 5.3 Windows source path to the existing physical model. It reads stored `ClothingAssetCommon` instances inside loose or materialized Git packages without launching Unreal. It is an alpha reader for the verified 522/1009/licensee-0 archive profile, not a universal cloth serializer.

## Prepare a certified context

A source catalogue needs matching qualified type metadata and independently certified native serializers for `ClothLODDataCommon` and `ClothTetherData`. Type metadata alone cannot describe their native tails. Prepare the selected roots from the matching USMAP, extending a catalogue whose native layouts have been validated for the producer:

```sh
cyuediff source-types native.usmap 522 1009 /Script/ClothingSystemRuntimeCommon.ClothingAssetCommon /Script/ChaosCloth.ChaosClothConfig /Script/ChaosCloth.ChaosClothSharedSimConfig --layouts certified-native.json --out source-cloth.json
cyuediff physical-cloth Before.uasset --layouts source-cloth.json --physical-cloth-profile ue5.3-windows --max-properties 2000000
cyuediff physical-cloth-diff Before.uasset After.uasset --layouts source-cloth.json --after-layouts after-source-cloth.json --physical-cloth-profile ue5.3-windows --max-properties 2000000
```

The Chaos configuration roots matter: a short serialized struct name alone does not certify its qualified owner. The default source work limit remains 100,000; the selected larger real example requires an explicit higher limit. [Source read limits](SOURCE_READ_LIMITS.md) apply independently per side. The after profile inherits the before profile in CLI; desktop profile choices are independent and Disabled by default.

## Core and operations

`read_physical_cloth(const Package&, const PhysicalClothOptions&, export_index)` builds the same owned `PhysicalClothAsset` used for cooked packages. Select `PhysicalClothProfile::unreal_5_3_windows` explicitly. When several instance exports exist, select an export index; default/class archetype exports are excluded. Missing required fields, unsupported source coverage, missing metadata or a failed read guard produce an unavailable model. Malformed known values, forged map replacement, wrong qualified struct owners, invalid transitions and invalid profiles fail atomically. Cancellation and finite model/value/report limits remain in force.

The source reader borrows generic property/native trees during construction. It retains stored positions/normals/triangles, inverse masses, twelve bone slots, explicit map entries/removals, tether batches, modern 64-byte transition records and supported stored Chaos configuration fields. It provides physical vertex details, interpreted transition records, the certified Chaos reference-pose wrap operation and base mesh geometry for OBJ export. It does not execute simulation, evaluate constructors or reconstruct omitted class/archetype values.

The CLI also exposes `physical-cloth-vertex`, `physical-cloth-transition`, `physical-cloth-wrap` and `export-physical-cloth`. These share `--layouts`, `--physical-cloth-profile` and the source limits; use `--lod`, `--vertex` and `--transition up|down` for applicable queries. `--include-values 1` expands the inspection report. OBJ export requires `--out MESH.obj`. Explicit `--export-index` and comparison-only `--after-export-index` select individual exports. Output guards protect input files, companions and selected context files.

Source provenance records the stored package identity, original file fingerprints, exact decoder catalogue and type-context SHA-256, archive profile, selected native decoder IDs and any nondefault source policy. It does not imply a Zen package or script-object table. Source/cooked mixed comparisons retain known typed values while leaving raw package-byte comparison unavailable. Original stored package identities can survive byte copies to another project path; reference validation pairs such copies explicitly.

Source maps remain stored deltas. Their default/replacement inventories are unresolved even when all stored entries decode. A specialized comparison returns known field changes alongside incomplete coverage and CLI exit 2; an unchanged partial projection cannot prove complete equality. This does not change the separate generic source difference contract.

## Desktop and reusable UI

In source or local Git mode, select each side's **Physical cloth settings** profile and matching **Native structure layouts**, then Load. The worker prepares at most sixteen physical instance models per side, including when ordinary image previews are disabled; extra instances remain available through indexed Core/CLI inspection. The **Physical cloth** tab selects before/after objects independently, then offers the existing physical LODs, vertex/tether details, configuration and stored/reference-pose wire views. Its explicit **Compare selected physical objects** option pairs differing object paths; otherwise the reusable view keeps unknown correspondence visible.

`AssetView::set_physical_cloth_assets` accepts immutable host-prepared models, with no I/O in drawing. Object pairing, object identities and nested physical LOD/vertex/filter/camera choices round-trip through the existing workspace 14 view-state extension. Missing saved object identities cannot silently restore explicit pairing against another object. Existing source profile fields are reused, so no settings migration is required. English and French are provided.

## Independent validation

The installed Core-only `physical_cloth_consumer` example accepts:

```sh
physical_cloth_consumer --source Before.uasset source-cloth.json [After.uasset after-source-cloth.json] --max-properties 2000000
python validate_source_physical.py --manifest cases.json --consumer physical_cloth_consumer --output-dir new-proof
```

The manifest declares `layouts`, canonical `decoderSha256` and `typeSha256`, optional `maxProperties`, and explicit cases with `asset`, Unreal API `reference` and stored qualified `actualMesh` identities. The validator imports the independently established geometry/reference comparator. It checks source provenance, exact stored float32/integer values, all twelve bone slots, byte-exact native tethers/transitions, supported configuration fields, bounds and reference-pose wrap components. It preserves unavailable default fields and hashes its inputs before/after. It does not validate a running simulation or source render-to-physical correspondence.

See [physical model details](PHYSICAL_CLOTH.md), [source type metadata](TAGGED_TYPES.md) and [completion evidence](COMPLETION.md).
