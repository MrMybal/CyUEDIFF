# Binary block deltas

Standalone byte comparisons and verified reconstruction, independent of Unreal/CyUI/Git.

## Bounded rolling block matching — 0.158

The Core indexes aligned base blocks and scans rolling target windows, preserving copy matches after inserted/deleted bytes. Candidate hashes are lookup hints; every accepted byte is compared against the original base. Operations coalesce adjacent literals and contiguous original-base copy ranges. Input/index/operation/literal/candidate-work limits, cancellation and progress are explicit. Exact byte identity is separate from Unreal property or visual differences. The algorithm is deterministic for the same inputs/options and does not promise a minimal patch or compression.

## Verified reconstruction and portable manifests — 0.159

BinaryDelta serializes as cyuediff.binary-delta/1: endpoint sizes/SHA-256, block size and ordered original-base copy ranges or lowercase hex literals. Strict parsing rejects unknown/duplicate fields, future formats, invalid numbers/hex/ranges, nested or oversized input and aggregate budgets. Reconstruction validates the entire plan and base identity before allocation, then verifies the target SHA-256 before returning any result. File APIs are bounded readers only. Candidate hash collision tests include a mathematically constructed rolling-hash collision and adversarial repeated candidates; copy acceptance remains byte-exact and work-bounded. The manifest contains new literal bytes, can exceed the target size, is not compressed or authenticated, and summary identities are claims until the base/target reconstruction is verified.

## Binary CLI and no-replace output publication — 0.160

binary-diff BASE TARGET emits a portable delta, or a payload-free --summary report; --out selects a new destination. binary-apply BASE DELTA --out NEW_FILE verifies base and reconstructed target identities before publication. New binary commands refuse existing outputs, source/companion aliases and missing output parents. A fully flushed exclusive sibling temporary file is published without replacement; ordinary I/O failures clean the reserved temporary file. Integer/budget options are strict. Exit codes are 0 for identical/successful application, 1 for changed bytes and 2 for failure. These commands do not parse an Unreal package, so they work with companions and still-opaque asset formats too.

## Local Git and LFS binary revision deltas — 0.161

CyUEDiff::Git can materialize one exact portable repository file and compare its decoded bytes without a package reader. Ordinary blobs and locally verified LFS content retain immutable commit/path/blob/LFS/SHA-256 provenance; no checkout, filters or fetch run. Revision ownership preserves materialized bytes. Missing sides remain explicit as added/removed/absent; present empty files stay distinct. Source names cannot collide with flat workspace scratch files. git-binary-diff emits the plain Core manifest for binary-apply, while --summary adds Git provenance. CLI writes protect repository metadata and configured LFS storage, reject existing destinations and reject both-missing selections without publishing a delta. Renames require explicit --after-path. Core and Git cancellation controls apply during materialization and reconstruction.

## Installed SDK examples and independent corpus validation — 0.162

The Core and Git SDK examples reconstruct generated or explicitly supplied files through serialized deltas and verify exact bytes. The distribution validator compiles both consumers independently against installed headers/libraries, verifies a generated shifted/deleted payload and reconstructs a historical Git README. The public validate_binary_deltas.py script independently interprets the manifest with Python slicing and SHA-256, then compares CLI reconstruction and optional installed SDK results for self comparisons, byte-edited copies and pairs of distinct original files. Original assets/companions are fingerprinted and outputs remain in new ignored build directories. Primitive manifest nodes are work-bounded as well as objects, and delete-to-empty plans avoid an unused block index. This byte layer does not expand semantic Unreal format coverage, stream multi-gigabyte files, provide a chunk server or replace Git/LFS storage.

## Compact raw-byte manifests — 0.163

The optional compact format stores the same checked plan with raw literal bytes and fixed-width little-endian integers. Its exact 112-byte header is: eight signature bytes `CYUEDLT\\0`, uint32 version (1), uint32 flags (0), uint32 block size, uint32 reserved (0), uint64 base size, uint64 target size, uint64 operation count, then two raw 32-byte SHA-256 identities. Each copy is byte tag 0 followed by uint64 original-base offset and uint64 length; each literal is byte tag 1 followed by uint64 length and those raw bytes. Empty operations, trailing bytes, future versions/flags, nonzero reserved fields, truncated data and exhausted limits fail explicitly. Endpoint hashes are verified during application; compact parsing alone does not authenticate content. There is no compression or additional envelope checksum.

`BinaryDelta::to_binary` / `from_binary` use the compact contract. `read_binary_delta` detects the complete signature independently of extension and retains strict JSON support. Manifest limits are charged against the selected representation, so compact data does not incur JSON hex accounting. All manifests remain bounded in-memory values.

## Bounded delta composition — 0.164

`compose_binary_deltas(first, second)` turns A→B and B→C into A→C without loading any endpoint file. The intermediate size and SHA-256 claims must match exactly. Copies through B are split across the first delta's ranges and become original-A copies or sliced literals; adjacent literals and contiguous A copies coalesce. Reordered/repeated B ranges remain supported. The original base identity and final target identity are retained, using the first delta's block-size metadata. Intermediate bytes are not materialized or verified: apply the result to A to verify actual endpoint integrity. No intermediate authentication is implied.

Input/output operation limits and aggregate literal limits apply, including repeated literal slices. A separate default `max_composition_fragments` of 1,000,000 bounds expansion work even if output operations coalesce. A prefix index provides logarithmic range lookup; cancellation/progress remain cooperative. Empty endpoints and delete/reinsert chains retain their ordinary byte semantics.

## File-backed streaming reconstruction — 0.165

`apply_binary_delta_file(base, delta, sink)` validates the complete plan, streams and verifies the whole base SHA-256 before the first sink call, then seeks original-base copy ranges and supplies ordered target chunks of at most 1 MiB. The target SHA-256 is checked before successful return. Reconstruction uses a fixed 1-MiB working buffer plus the already-resident delta; it does not buffer either endpoint. Delta generation and manifest parsing remain buffered, and input/literal/operation limits still apply. The existing vector-returning API is unchanged.

Sink bytes are **provisional until successful return**. The host owns output reservation, cleanup, durability and publication; it must discard partial output after wrong target hashes, cancellation, source changes or I/O/sink exceptions. Sink exceptions propagate, and byte pointers are valid only during each synchronous callback. Do not mutate the base or delta from callbacks. Observed base size changes are rejected before and after reconstruction; this is not a filesystem snapshot or concurrent-edit service. The Core does not create output files.

## Compact/composed CLI workflows â€” 0.166

Local and historical `binary-diff` commands accept `--format json|binary` (JSON remains the default). Binary output requires an explicit new `--out` path and excludes `--summary`, keeping standard output textual. `binary-compose FIRST SECOND` accepts either input representation and emits the composed plan, with optional `--summary`, `--format` and `--max-composition-fragments`. Composition returns 0 on success, even for a changed target. `binary-info DELTA` emits checked metadata and explicitly reports `identitiesVerified: false`, since it has no endpoint bytes. These commands share no-replace output guards and manifest limits.

`binary-apply` detects either wire format and now streams through an exclusive sibling temporary file. It publishes only after complete base/target verification and successful flush/close; errors discard provisional output. Existing files and aliases remain protected. This reduces reconstruction memory while generation and manifest/literal storage remain buffered.

```powershell
cyuediff binary-diff A.uasset B.uasset --format binary --out A-B.delta
cyuediff binary-diff B.uasset C.uasset --format binary --out B-C.delta
cyuediff binary-compose A-B.delta B-C.delta --format binary --out A-C.delta
cyuediff binary-info A-C.delta
cyuediff binary-apply A.uasset A-C.delta --out Rebuilt-C.uasset
```

## Commands and SDK

```powershell
cyuediff binary-diff Before.uasset After.uasset --out Asset.delta.json
cyuediff binary-diff Before.ubulk After.ubulk --summary
cyuediff binary-apply Before.uasset Asset.delta.json --out Rebuilt.uasset
cyuediff git-binary-diff D:/Game before-commit after-commit Content/Texture.ubulk --out Texture.delta.json
cyuediff git-binary-diff D:/Game before-commit after-commit Content/Texture.ubulk --summary
```

Use a new output path. Existing outputs are preserved; source/companion aliases and Git/LFS storage are protected. `--summary` emits statistics, rather than a reconstructable manifest. A Git manifest has the same plain Core format as a local one; Git provenance is available in `--summary` and the typed Git API. Added Git files require an explicitly empty base when applying their delta; removed files reconstruct an empty target. Both missing Git selections produce an error.

```cpp
#include <cyuediff/binary_delta.hpp>
auto delta = cyuediff::make_binary_delta_files(before, after);
auto wire = delta.to_json().dump();
auto restored = cyuediff::apply_binary_delta(
    cyuediff::read_binary_file(before, 256ull * 1024 * 1024),
    cyuediff::BinaryDelta::parse(wire));
```

`CyUEDiff::Core` owns only bounded readers, matching, manifests and reconstruction buffers. Hosts select their persistence and transport. `CyUEDiff::Git` additionally exposes `materialize_file`, `GitRevision::file_path` and `compare_git_binary_files` in `cyuediff/git_binary_delta.hpp`; keep the returned revisions alive while using their materialized files. The exact-file API does not load companion files implicitly. The `examples/binary_delta_consumer` and `examples/git_binary_delta_consumer` projects compile against the installed SDK.

## Default limits and interpretation

| Limit | Default |
| --- | --- |
| Input and reconstructed target | 256 MiB each |
| Literal bytes | 64 MiB aggregate |
| Manifest bytes | 160 MiB |
| Indexed full base blocks | 1,000,000 |
| Delta operations | 100,000 |
| Candidate bytes compared | 512 MiB |
| Block size | 64 KiB; allowed 16 bytes–1 MiB |

The CLI accepts `--block-size` for comparisons and `--max-input-bytes`, `--max-literal-bytes` and `--max-manifest-bytes` for explicit budgets. Other limits are available in `BinaryDeltaOptions`. Invalid limits and exhausted budgets fail explicitly. Inputs, literals, JSON and reconstruction are buffered in memory within the selected limits. File-size changes during reads are rejected, but this is not a filesystem snapshot service.

All copy offsets address the original base. Target windows can be unaligned; base blocks are aligned, and successful matches extend forward until different bytes. Files smaller than one block may require all literal bytes when changed. Copy/literal ordering is deterministic for fixed inputs/options; no optimal-size guarantee is made. SHA-256 establishes endpoint integrity when the base and reconstructed target are checked. Hash identities in a manifest or summary alone do not authenticate their author. A binary delta says which bytes can be reused; existing Unreal semantic and visual projections explain interpreted asset changes separately.

To validate explicit local inputs with a Python interpreter independent of Core:

```powershell
python scripts/validate_binary_deltas.py --input Before.uasset --input After.uasset --output-dir build-native/binary-validation
```

The output directory must be new and under an ignored workspace build directory. Optional `--consumer PATH` additionally checks the separately compiled Core SDK consumer. Byte-edited validation copies use `.bin`; they are reconstruction tests rather than Unreal asset editing.

The Windows validation pass additionally exposed a pre-existing smoke-runner issue: a scripted reload/duplicate shared the initial load's 30-second phase. The runner now resets that phase for each explicit action, and the historical test sequences log per-case times with separate aggregate budgets. Initial-load deadline failures and reruns remain explicit in [VALIDATION.md](VALIDATION.md).
