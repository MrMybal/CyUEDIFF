# Typed value name identities

Unreal serializes an FName using a plain name-table entry and an internal number. A literal base `Item_1` with number 0 and base `Item` with number 2 both display as `Item_1`. They are different names in the tested Engine. A display string is insufficient for default/container reconstruction.

The optional Core observation journal retains those components alongside decoded values. It does not change existing property values, encoded container IDs, read-format inventories or asset files. It does not implement runtime name/key equality, effective-default merging or a new name-aware comparison algorithm.

## Public reads

```cpp
cyuediff::ReadOptions options;
options.value_identities.names = true;
auto package = cyuediff::Package::open("asset.uasset", options);
const auto& identities = package.value_identities;
```

`Package::value_identities` and its `to_json()` field `valueIdentities` address `semantic_snapshot()`. Each `names` key is an escaped JSON Pointer to its actual string value; each observation contains `base`, `number` and `source`. Source `wire` means the components were read from the property stream/name table. The schema reader also identifies `zero-mask` names explicitly.

`UnversionedReadOptions::value_identities` controls the same journal for both single typed values and unversioned property streams. Returned `UnversionedValue::value_identities` paths address `value`; `UnversionedProperties::value_identities` paths address `properties`. `ZenReadOptions::value_identities` forwards it into each decoded export's `propertyRead.valueIdentities`, with an enclosing package budget. `DefaultSnapshotReadOptions::values.value_identities` retains field-relative observations in each field's `valueIdentities` and aggregate statistics in snapshot provenance. The [default reader](DEFAULT_SNAPSHOTS.md) remains unbound and does not apply defaults to a package.

```sh
cyuediff inspect asset.uasset --name-identities --out inspection.json
cyuediff zen-inspect build.utoc CHUNK_ID --cooked-profile ue5.3 --script-objects global.utoc --schema mapping.json --name-identities --out inspection.json
```

The flag supports full `inspect`, `git-inspect` and `zen-inspect` output. Snapshot-only/metadata-only modes and other commands reject it. The [installed source example](../examples/value_identity_consumer/README.md) and default-snapshot example demonstrate the public options without Unreal linkage.

## Coverage and limits

The journal covers typed name values, reflected children, arrays, map/set keys/removals, supported schema soft-path components, field paths and delegates. It excludes object/import/export/tag-name tables, raw native/declarative callback output and names inside native text histories. `coverage: "typed-name-values"` and `status: "complete"` describe the selected journal, not complete asset decoding or certified runtime equality. Opaque properties retain their existing coverage.

Failed source property/export transactions discard observations that cannot address published values, without refunding spent work. Container keys receive their actual encoded JSON location after decoding. Conflicting locations do not publish an arbitrary identity. The journal validates pointer/text limits before expansion and avoids unnecessary buffers when disabled or when a key contains no names.

`max_names` and `max_text_bytes` default to 100,000 observations and 4 MiB per enclosing operation. Text accounting includes name bases, pointer paths and relocated key paths; failed preparation remains charged. Exhaustion reports an incomplete journal while preserving readable stored values and known observations. Cancellation still aborts the operation. Optional `charge` callbacks run during the read; cache reads with an active charging callback bypass retention. Cached packages distinguish observation selection and limits, and do not retain the callback. These are metadata/work bounds, not a whole-process heap limit.

## Evidence and remaining work

Two original UE 5.3.2 source assets reload with native property equality and prove that equal display names can be different FNames. Fourteen published source observations retain the native components, while raw property snapshots agree exactly with the frozen 0.195 reader. Scalar, array and mapped values change their components across the pair without changing their display strings. Native fixture preparation and the Core consumer are separate processes.

The fixture also contains two genuinely different map keys with the same display. Existing display-derived container IDs collide, so that property remains opaque; its discarded observations are not exposed as valid value paths. A component-aware key codec and identity-aware comparison/default resolution remain required. Unicode bytes are preserved without case conversion; no universal Unicode or custom native key equality contract is claimed. The desktop has no new component-specific inspector or preference in this milestone.
