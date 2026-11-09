# Source read limits

Source development 0.187 exposes deliberate finite read policies for loose Unreal packages and historical Git materializations. The defaults stay at 100,000 work units and 32 MiB per property/script/native window. A matching schema does not silently raise either value. These policies also bound extension result validation; work units are not a count of visible fields. Existing per-string, table, depth, decoder-layout and payload limits remain separate.

## CLI and Core

```sh
cyuediff inspect Source.uasset --layouts source-catalogue.json --max-properties 2000000
cyuediff diff Before.uasset After.uasset --max-properties 2000000 --after-max-properties 100000
```

`--max-properties` accepts integers from 1 to 4,000,000. `--max-property-bytes` accepts 1 to 268,435,456 bytes. Source decode commands accept both; comparison commands also accept `--after-max-properties` and `--after-max-property-bytes`. Omitted after fields inherit their corresponding effective before value. Single-source commands, source metadata preparation and cooked/container commands reject after or inapplicable limits. Every option is validated before asset decoding.

The reusable `SourceReadLimits` helper in `cyuediff/source_limits.hpp` validates and applies this finite policy to `ReadOptions`. Direct Core hosts can still choose their own `ReadOptions` policy. Non-default effective values appear as `sourceReadLimits` in package reports; CLI/desktop reports retain per-side `readLimits`. These are provenance, excluded from semantic snapshots. Equal decoded assets do not become changed solely because their budgets differ. The package cache already keys the numeric options and does not reuse a permissive result for a restrictive request.

The installed [binary layout consumer](../examples/binary_layout_consumer/) accepts the same optional flags after its `package` inputs. The source cloth reference validator accepts explicit `--max-properties` / `--max-property-bytes` and checks the returned policy provenance.

## Desktop and saved work

Open **Source read limits** for local files or Git revisions. **Before work** and **Before bytes** select the limits used on Load. **Use before limits after** is enabled by default; disable it to select an independent after policy. **Reset read limits** restores both defaults and inheritance. The corresponding French controls are **Limites de lecture source**, **Travail avant**, **Octets avant** and **Réutiliser les limites avant**. Increasing a budget can increase memory and time; these counters do not cap the process heap.

Workspace format 14 retains limits per document and during session restart/duplication. Versions 1–13 migrate with the original defaults and inheritance enabled. Invalid, missing, fractional, negative or excessive settings are rejected; loading a workspace does not discover a larger policy. Containers retain their separate explicit schema/payload budgets.

## Limit/count failures and comparisons

A source property/native limit or count guard can fail before a value becomes available. Inspection retains the bounded opaque result, exact input fingerprints where enabled, original diagnostic and `sourceReadLimitFailed: true`. This flag is distinct from missing input bytes; `completeInput` still describes input availability.

Comparing a package with such a failed guard returns `unverified`, no invented semantic removal/addition rows, and available whole-input binary facts. CLI comparison exits 2. The desktop worker reports the failed read rather than opening a misleading paired review. The embeddable `AssetView` also rejects that pair before changing its existing state; single-package metadata inspection remains possible. Correct the input or explicitly choose a sufficient matching policy before comparing again. Cancellation still propagates and never becomes an opaque/cached result.

## Available evidence and remaining scope

An independently built consumer against the previously installed 0.186 Core measured the actual Humanoid source at 100,000, 500,000 and 2,000,000 work units, with the same 32 MiB window. The first two reads retain the opaque cloth export; the last reads both LODs with no unavailable reflected fields in that object. Its observed process peak is approximately 133 MiB for that isolated single-package probe, including retained parsed data. This one observation is not a general performance guarantee or a heap cap, and excludes full JSON reporting, paired UI snapshots and rendering.

The new current CLI/desktop/SDK path is verified separately against actual Engine API references. Generic source values/tails remain separate from a dedicated physical geometry/render correspondence model and unresolved constructor defaults. Further producer profiles and report-memory optimization remain open. Private source assets, schemas, reports and captures are not distributed; the previously verified Windows archive remains unchanged until final delivery preparation.

## Large source presentation

The native/embeddable asset view bounds raw per-object JSON and whole-document text comparison to 4 MiB and 50,000 lines per display input. A bounded formatter checks output while writing; it never supplies truncated JSON as a real value. Large object preflight avoids cloning its entire property tree into expanded inspection rows. Such objects start in the complete scoped property browser; their expanded table is a bounded root summary. Raw/text views show an explicit EN/FR size diagnostic and point to scoped properties or CLI JSON export. The Core's decoded values and named differences remain intact.

An actual Humanoid first exposed the upstream CyUI text viewer's 32 MiB guard: its full source JSON exceeded that size, blocking an otherwise valid package pair. The host now honors its own smaller display limits and keeps scoped review available. This changes presentation, without raising the upstream guard or removing source data from the reusable reader.

The stronger failed-limit guard is retained in 0.189. Other unread source values now use [scoped comparison availability](SOURCE_AVAILABILITY.md), preserving independent known fields instead of disabling the entire semantic comparison.
