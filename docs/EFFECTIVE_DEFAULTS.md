# Prepared effective reflected properties

The standalone Core can resolve and compare selected source properties against an explicit prepared native class-default context. Stored package properties remain available separately. This is a working source/native workflow for the verified UE 5.3.2 Windows profile; it does not complete arbitrary Engine, Blueprint, cooked, template or runtime-object reconstruction.

## Prepare a matching context

Build the original optional [default exporter](../tools/defaults_exporter/README.md) in the trusted matching project/Engine. In an owned commandlet session, initialize the selected CDO and its reflected/type defaults with normal configuration IO first. Then freeze that commandlet's configuration file IO before the final capture, preventing shutdown user-settings writes from invalidating the recorded inputs. The commandlet-only helper returns false in an interactive editor. No asset save is involved.

```python
import json
import unreal

cdo = unreal.get_default_object(unreal.MyNativeAssetClass)
assert json.loads(unreal.CyUEDiffDefaultLibrary.capture(cdo, False))["completeCapture"]
assert unreal.CyUEDiffDefaultLibrary.begin_read_only_commandlet_capture()
capture = json.loads(unreal.CyUEDiffDefaultLibrary.capture(cdo, False))
assert capture["completeCapture"]
# Save this returned JSON to a new local capture file.
```

The exporter records actual field wires, initialized structs, declaring owners/GUIDs, native/post-serializer traits and an environment inventory. Present module, executable, build, project and configuration files receive SHA256 fingerprints **during native capture**. Optional configuration layers record their absence. Length-framed merged configuration section/key/value state has BLAKE3 digests; configuration values themselves are not returned. Hashing has its own aggregate byte/work bounds. Constructors/custom serializers run only in the explicitly invoked trusted preparation environment; arbitrary plugin code has no imposed wall-clock bound.

```sh
python scripts/prepare_default_context.py class-default.json --source Asset.uasset --reader path/to/cyuediff --out context.json
# Optionally retain inputs locally for a pinned context that survives later project rebuilds:
python scripts/prepare_default_context.py class-default.json --source Asset.uasset --reader path/to/cyuediff --copy-inputs context-inputs --out pinned-context.json
```

Preparation inspects only the source archive metadata to pin its profile. It never uses target-instance property values as reconstructed defaults. Input file hashes must still match their capture-time values. The native source profile assumes the documented standard tagged/intra-property-delta serialization contract; an arbitrary custom saver policy is not discoverable from its display JSON.

`local-inputs` contexts verify recorded local paths, file bytes and optional configuration absence when opened. `pinned-inputs` contexts verify copied file bytes against the capture-time fingerprints and preserve captured absence as part of the recorded state. Copies use content addresses and are reused only when their bytes match. Keep input stores local: they can contain project configuration and Engine/plugin binaries, and are not part of the MIT Core distribution. Changing a module cannot silently attach a newer file fingerprint to an older capture. A supplied context fingerprint is a consistency pin, not authentication of an arbitrary capture's producer.

## Standalone inspection and differences

```sh
cyuediff effective-inspect Asset.uasset context.json --out new-report.json
cyuediff effective-diff Before.uasset After.uasset --before-context before-context.json --after-context after-context.json --out new-diff.json
```

Inspection accepts `--object EXPORT` and `--expect-context SHA256`. Comparison accepts `--before-object`, `--after-object`, `--expect-before-context` and `--expect-after-context`. Contexts are independent; an identical selected context is verified once and reused within the command. All effective commands refuse existing output files, including input aliases. Exit codes: 0 complete inspection/identical effective properties, 1 changed effective properties, 2 rejected or unavailable comparison data.

A context pins the actual capture bytes, qualified native class/CDO, source versions/custom versions/editor filter, Windows byte order, archive policy and recorded input bytes. It reads and hashes data; it does not load Unreal, execute a module or search an Engine installation. Prepared state is immutable after opening. Reopen/reprepare to check a newer environment; an existing context does not monitor changing files in the background.

Root exports may use their implicit native CDO or the exact captured CDO template. Different classes, nested/template exports, unread root inventories, unsupported archive profiles and Blueprint captures are rejected. Unknown individual fields remain explicitly unavailable; they never become a fabricated null, zero, empty container or copied class default.

## Core API and result contract

```cpp
#include <cyuediff/defaults.hpp>

auto context = cyuediff::DefaultContext::read("context.json");
cyuediff::ReadOptions options;
options.value_identities.names = true;
auto package = cyuediff::Package::open("Asset.uasset", options);
auto effective = cyuediff::resolve_source_defaults(package, "Asset", context);
auto comparison = cyuediff::compare_effective_defaults(effective, other_effective);
```

See the independently configured [installed SDK consumer](../examples/effective_defaults_consumer/README.md). `DefaultSnapshot` itself stays immutable and unbound; a context supplies the separate explicit binding and prepared source profile. The input `Package` is never modified or duplicated into the result.

`EffectiveDefaults.fields` uses qualified declaring-field identities. Each field has its captured type, availability, completeness, origin and a value when readable. Reflected structs contain the same field envelopes; arrays contain element values. Names retain actual `base`/`number` components. Effective maps/sets expose `effective-map`/`effective-set` entries under their supported runtime key identity. Missing typed-name components remain unavailable; suffixes are never guessed from display strings.

Resolution preserves the actual load distinctions:

- Omitted roots inherit the selected CDO, including derived-constructor overrides of inherited fields.
- Partial reflected members retain their actual member defaults, which can differ from the struct initializer.
- Present arrays replace the previous contents. New reflected elements use initialized type data, never the old CDO element at the same index.
- Maps/sets apply removals and matching-key updates. A matched map retains its existing key, including display spelling. New values receive initialized storage; a matched map's existing storage is distinct from a comparison-default pointer. Nested containers serialized without comparison defaults replace their old contents. A delta with an unavailable base is not reconstructed as an empty map.
- Selected integer/bool keys use their typed identity. FName and FString keys use the verified ASCII case-insensitive profile. Non-ASCII string/name keys, native/custom struct equality and unsupported key types stay unavailable.
- The selected qualified IntPoint, Vector and Quat math routes must match their captured layouts. Reflected native post-serializers, user-defined struct loader behavior, instanced/subobject reference rebasing and unsupported native routes are not replaced with generic field merges.

Comparisons follow the retained types and known coverage. They ignore origin labels and context fingerprints as asset values. Hidden FName base/number differences are visible even when display strings match; verified ASCII name case equivalence is respected. Different Unicode name spellings without a proven equality contract remain unverified. Ordinary string values retain their display-sensitive value comparison. Incompatible fields/key contracts, missing declarations and unread added/removed values stay scoped as unavailable, while independent known changes remain visible.

Document bytes/nodes/depth/text, dependency counts/bytes, snapshot wires and aggregate resolution/comparison work/text have separate explicit bounds. Escaped paths and copies are charged before expansion. Cancellation is cooperative and never becomes an opaque fallback. These are operation bounds, not a whole-process heap or wall-clock guarantee.

## Evidence and remaining scope

The selected native producer supplies six unchanged source assets, fresh CDO/type captures and separate native reflected references checked with `FProperty::Identical_InContainer`. All 78 root property instances and 364 visited typed values match. The six effective comparisons reproduce 0/6/5/7/6/8 native-derived changes, including the same-display/case key case. Four deliberately incorrect reports fail the independent verifier. Stored source snapshots remain unchanged.

This scope is the prepared **serialized reflected state**. It does not simulate arbitrary constructor dependencies, post-load/runtime plugin behavior, localization evaluation, custom property conversions or unrecorded external state. Recorded file consistency is not universal closed-world dependency attestation. Blueprint/archetype/subobject contexts, cooked effective defaults, broader equality/producer profiles, automatic context refresh, practical EN/FR desktop selectors and final matching distribution remain open. The original full-product goal remains active.
