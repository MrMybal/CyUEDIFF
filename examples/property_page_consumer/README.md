# Core-only paged property consumer

Build against an installed CyUEDiff SDK with CMake's `CMAKE_PREFIX_PATH`, then run:

```sh
property_page_consumer before-properties.json after-properties.json /Items 39936
```

The example reads bounded JSON snapshots supplied by the host and prints a summary plus at most 128 direct children. `JSON_POINTER` is an actual escaped JSON pointer; the empty argument selects the root. Stored typed values are unwrapped for enumeration, and child paths retain their exact `/value` segments. `FIRST` is the first child offset, clamped to the last page if stale.

`-` explicitly means a **known absent** snapshot, so differences are additions/removals. A host with an unknown/undecoded peer must inspect its known snapshot independently instead. A present JSON `null` remains a value. No Engine, Git, CyUI, codec, file write or source-asset loading is involved in this example. The reusable API also exposes serialization fields, comparison budgets and cancellation; see [PROPERTY_BROWSER.md](../../docs/PROPERTY_BROWSER.md).

With matching 0.189 headers/library, append an optional `COVERAGE.json` object with JSON-pointer arrays `beforeUnavailable`, `afterUnavailable`, `comparisonUnavailable`, `ignored` and `raw`. Readable fields remain inspectable; unavailable peers use `Unknown` instead of a removal. Rows report `beforeAvailable`, `afterAvailable`, `contextOnly` and the actual `sourcePath`. These masks are host-supplied evidence, not a guessed schema or data decoder. See [source comparison availability](../../docs/SOURCE_AVAILABILITY.md).

The optional coverage JSON can also set `serializationFields: true` to enumerate actual tag fields beside unread values. Explicit side masks retain precedence. Use the matching 0.193 SDK; see [source key and metadata context](../../docs/SOURCE_KEY_CONTEXT.md).

Development 0.194 retains cooked interpretation separately from stored values. Use matching headers/libraries and [the cooked correspondence contract](../../docs/COOKED_CONTEXT.md). The property page example accepts `cookedProperties: true` with raw/unavailable scopes from the cooked coverage API.
