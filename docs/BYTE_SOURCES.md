# Byte sources

The Core reads packages through `ByteSource` objects, not through the filesystem. A host application can therefore supply the before and after revisions from any storage or version-control system (Git, Perforce, Subversion, Plastic, a database, a network service...) without writing temporary files. `Package::open(path)` remains available. It is a shortcut for local files.

## Contract

```cpp
class ByteSource {
  public:
    virtual std::uint64_t size() const = 0;
    virtual void read(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) const = 0;
    virtual std::string name() const;                  // diagnostics label, never opened
    virtual std::unique_ptr<Session> session() const;  // optional, see below
};
using ByteSourcePtr = std::shared_ptr<const ByteSource>;
```

- `size()` is constant for the lifetime of the source.
- `read()` fills exactly `bytes` bytes at `offset`, where `offset + bytes <= size()`, or throws. A non-`cyuediff::Error` exception becomes `Error("io")`.
- `read()` may be called concurrently from several threads. Background workers can decode previews of the same package at the same time.
- `session()` lets one decoder hold a resource, such as a file handle or a connection, while it reads sequentially. A session is used by one thread at a time. The default session forwards to `read()`.

The reader requests data through a 64 KiB window, so primitive reads (integers, names, tags) do not reach the source one by one. Large ranges are read directly in chunks of at most 1 MiB, with cancellation and progress checked between chunks.

## Ready-made sources

| Function | Use |
|---|---|
| `file_source(path)` | Local file. Its size is captured on creation. Each session reopens the file and reports `Error("source-changed")` if the size differs. No handle stays open between sessions. |
| `memory_source(std::vector<std::uint8_t>, name)` | Owned bytes, moved in. |
| `memory_source(owner, data, size, name)` | Zero-copy view of host memory. `owner` keeps it alive. |
| `callback_source(size, callback, name)` | Host function, for example a VCS blob reader or HTTP range requests. It must follow the `read()` contract. |

Hosts may also derive their own class from `ByteSource`.

## Package and companions

```cpp
cyuediff::PackageSources before;
before.name = "//depot/Game/Content/BP_Player.uasset";   // reported as Package::source
before.package = cyuediff::memory_source(std::move(asset_bytes));
before.companions[".uexp"] = cyuediff::memory_source(std::move(uexp_bytes));
auto old_package = cyuediff::Package::open(before, options);
```

Companions are keyed by extension: `.uexp`, `.ubulk`, `.uptnl`, `.m.ubulk`, `.o.ubulk`, `.upayload`. A missing key means the companion is absent in that revision. With explicit sources, the Core never looks for sibling files on disk: a missing `.uexp` produces an incomplete read with its usual diagnostics. `PackageSources::files(path)` builds the same structure from the files present beside a local package.

`Package::open(sources)` rejects a null package source, unknown extensions and null companions with `Error("byte-source")`.

## Lifetime

`Package::inputs` keeps the sources. Texture and mesh payloads (`BulkPayload::source`) keep the source that contains them. A package read from memory can therefore decode previews after the host has released its own copy. A package read from files still rereads those files when it decodes a payload. A file rewritten with another size is reported as `source-changed`; a rewrite with the same size is caught by the payload fingerprint when one was recorded.

`BulkPayload::file` and `PackageSources::name` are only labels for reports. Hand-built `Package` objects without `inputs` keep the previous behaviour and resolve payloads from files beside `Package::source`.

## Virtualized payloads

`BulkDecodeOptions::virtualized_payload_source` lets the host resolve a virtualized payload from its own store, such as a DDC or a VCS virtualization backend. The resolver receives the 40-digit FIoHash and returns a source, or null when the payload is absent (`Error("virtualized-missing")`). It takes precedence over `virtualized_payload_root`. As with the filesystem store, the identity format is validated and the full raw BLAKE3 hash is always verified.

## Scope

This layer covers loose packages, their companions and their bulk payloads. Pak/IoStore containers, schema/layout files, project descriptors and Git materialization keep their existing file-based APIs.
