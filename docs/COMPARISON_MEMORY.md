# Comparison and report memory

Source development 0.190 compares source export properties, native fields and opaque metadata directly from the two input packages. It no longer clones both complete semantic snapshots before comparing them. Explicit projected models are prepared one pair at a time. Stored paths, known changes, unavailable scopes and source context keep the [availability contract](SOURCE_AVAILABILITY.md).

## Ownership and exact numbers

`compare(before, after)` borrows the inputs only during its synchronous call. Its returned `DiffResult` owns changed values; the packages may be destroyed after comparison. Whole-object additions/removals and changed large arrays still require owned result values. The result is not a lazy reference into the input packages.

## Arrays of different lengths

Arrays whose lengths differ no longer produce one `replace` holding both complete arrays. Equal leading and trailing elements are skipped. Remaining elements are paired at the same index, and the surplus is reported as `remove` rows (indices of the `before` array) or `add` rows (indices of the `after` array). Inserting one element at the front of a large array is therefore one `add` row.

Arrays that contain unavailable scopes or stored typed names keep index correspondence (pairs at each index, then trailing additions or removals), because those annotations identify values by index. Arrays of equal length are compared index by index, as before. The flat and paged property inspectors use the same alignment. Their array rows are numbered in the longer array's indices, so an inserted element is one added row and every row path is unique.

Mixed JSON numeric comparison checks the mathematical integer value instead of rounding an integer to a floating-point number. For example, integer `9007199254740993` differs from floating-point `9007199254740992.0`. Exactly representable equal signed/unsigned/integral floating-point values remain equal. Nested source comparisons and reusable property comparison share this behavior. This does not reconstruct missing defaults or expand reader coverage.

## Streaming existing results

```cpp
const auto result = cyuediff::compare(before, after);
result.write_json(output_stream); // same report facts as result.to_json()
```

`DiffResult::write_json(std::ostream&, int indent = 2, const Json& additional = Json::object(), const OperationControl& = {})` borrows existing result arrays while writing. It avoids an additional owned report tree and complete serialized string. `additional` must be an object; Core report field names are reserved, including optional fields not present in this particular result. Supported indentation is -1 for compact output or 0 through 16. Invalid options fail before output.

`GitComparison::write_json(std::ostream&, int indent = 2, const OperationControl& = {})` provides the corresponding Git report operation. Existing `to_json()` APIs remain explicit owned materialization operations. Use matching 0.190 headers and libraries. The installed [source diff consumer](../examples/source_diff_consumer/) demonstrates independent per-side catalogues and streamed output without Unreal, Git or CyUI.

The result, additional fields and destination must remain valid for the entire call. Do not mutate them concurrently. Cancellation is checked between report fields and generated items; serialization inside one already-owned JSON subtree is not interrupted at every node. A failed or cancelled public stream write may leave a partial document. The caller controls stream buffering and recovery.

## CLI and desktop files

The CLI streams generic source and Git differences with the same diagnostics, policies and context fields as before. Other JSON commands serialize their existing output without first building a complete string. CLI and desktop JSON report files use a buffered, exclusively created sibling temporary file. Successful generation flushes and closes it before same-directory replacement. A generation/write failure preserves the previous destination and removes the owned temporary file. Existing input/companion protection checks run before output. This is file-output handling, not a transaction over the input assets or a universal filesystem durability guarantee.

Desktop document/workspace reports generate nested entries from retained document models. Git documents share the comparison already owned by the Git result. An explicit `Document::report()` still materializes an owned report for internal callers requesting one. The compatible output continues to include both the active document and its workspace entry; streaming avoids keeping those duplicate output trees in memory.

## Verification and remaining limits

A sequential comparison of the same original large UE 5.3 physical-cloth source pair against the frozen 0.189 SDK retains exactly 34,550 known changes and one unavailable scope, with the same 9,988,061-byte JSON report. With matching optimized Release builds, the final CLI measurement reduces peak Windows working set from about 591 MiB to 270 MiB (2.97 to 2.25 seconds); the desktop including its full report changes from about 618 MiB to 352 MiB (6.84 to 6.09 seconds). Desktop document/workspace facts remain identical to the exact historical source build. This is one observed process workload, including source reading/comparison/reporting; it is not a general memory cap or timing guarantee. Final CLI/desktop regression measurements are recorded in [validation](VALIDATION.md).

The packages, full changed-value arrays, explicitly decoded physical/projection models and UI caches are still owned. A whole-object addition can retain a large value. Some cooked Zen report entries still materialize one complete package at a time. Source work/byte limits and cooperative cancellation do not bound the entire process heap. Streaming does not make arbitrary assets universally readable, resolve defaults or certify additional native producers. Final updated Windows distribution remains separate from this source milestone.

Source development 0.193 adds [source key/reader context and readable serialization metadata](SOURCE_KEY_CONTEXT.md). Reader observations are context, and key encoding is bounded separately from raw native data.

Development 0.194 adds [cooked interpretation and stored key correspondence](COOKED_CONTEXT.md). Inventories share reader budgets; views use a cooked-wrapper context policy rather than allocating one ignore scope per annotation. Default reconstruction and broader producer/projector acceptance remain open.

Development 0.195 adds [bounded default snapshots](DEFAULT_SNAPSHOTS.md). The complete JSON document has independent byte/node/depth/text limits before publication, duplicate keys are rejected, and field decoding shares aggregate value work/text limits across successful and unsupported attempts. Actual field wire bytes have a separate total limit; names and object references come from the captured tables. Initialized struct defaults remain separate from member CDO values. Parsing and interpretation still allocate owned JSON trees; these bounds are not a whole-process heap limit. Reading a snapshot does not merge or duplicate any asset property model.

Development0.196 adds opt-in [typed name observations](VALUE_IDENTITIES.md) outside existing values. Per-operation name/pointer budgets and discard-without-refund transactions prevent repeated failed work from evading bounds. Exhausted metadata does not make a readable property opaque. Disabled/nameless scopes avoid unnecessary journal buffers; source cache profiles distinguish limits and bypass retention for an active charge callback. No whole-process heap cap or runtime key equivalence is implied.
