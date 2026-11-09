# Core-only paged property consumer

Build against an installed CyUEDiff SDK with CMake's `CMAKE_PREFIX_PATH`, then run:

```sh
property_page_consumer before-properties.json after-properties.json /Items 39936
```

The example reads bounded JSON snapshots supplied by the host and prints a summary plus at most 128 direct children. `JSON_POINTER` is an actual escaped JSON pointer; the empty argument selects the root. Stored typed values are unwrapped for enumeration, and child paths retain their exact `/value` segments. `FIRST` is the first child offset, clamped to the last page if stale.

`-` explicitly means a **known absent** snapshot, so differences are additions/removals. A host with an unknown/undecoded peer must inspect its known snapshot independently instead. A present JSON `null` remains a value. No Engine, Git, CyUI, codec, file write or source-asset loading is involved in this example. The reusable API also exposes serialization fields, comparison budgets and cancellation; see [PROPERTY_BROWSER.md](../../docs/PROPERTY_BROWSER.md).
