# Pak and IoStore containers

Version 0.15 adds a standalone container layer to `CyUEDiff::Core`. It inventories members, reconstructs decoded bytes and compares their SHA-256 fingerprints without launching Unreal. `<cyuediff/container.hpp>` is installed with the SDK; no Git, UI, Unreal library or codec dependency is added to Core.

## Coverage

| Format | Implemented |
| --- | --- |
| Pak versions 10 and 11 | Unencrypted primary and full directory indexes, compact 32/64-bit entries, nonencoded entries, deleted records, uncompressed members and compressed block reconstruction. |
| IoStore TOC versions 3, 4 and 5 | 144-byte headers, chunk IDs, virtual ranges, compression block tables, perfect-hash table skipping, directory names when indexed, unnamed chunks, and sibling UCAS partitions. |
| Compression | Copy blocks work without a codec. Hosts provide a named-method callback for compressed blocks. The explicit bulk-codec adapter maps Oodle and LZ4 names to their correct numeric ABI identifiers. |
| Diffs | Pair members by their relative virtual path, or chunk ID when unnamed; report added, removed and modified decoded contents. Deleted Pak records are absent content. |

Container bytes are separate from UObject serialization. IoStore export-bundle chunks are cooked Zen data, not classic loose `.uasset` files. The separate [cooked Zen reader](ZEN.md) now decodes supported object tables and compares opaque object bodies. Unversioned property values and cooked native payloads remain undecoded, and the desktop asset browser does not yet mount containers. It does not implement encryption, path-hash-only Pak indexes, other TOC versions, virtualized resources or signature authentication. Unknown codecs fail explicitly; Zlib/Gzip are not built in.

The supplied CyTrace UE 5.3 build validates real Pak 11 and TOC 5 data. Other accepted versions are covered by synthetic fixtures, not a broad real-build certification.

## CLI

```powershell
cyuediff container Game-Windows.pak --out inventory.json
cyuediff container Game-Windows.utoc --out inventory.json
cyuediff container-read Game-Windows.pak Game/Config/DefaultGame.ini --out member.bin --codec my_codec.dll
cyuediff container-read global.utoc 000000000000000000000005 --out script-objects.bin
cyuediff container-hash Game-Windows.utoc --codec my_codec.dll --out hashes.json
cyuediff container-diff before/Game-Windows.utoc after/Game-Windows.utoc --codec my_codec.dll --out diff.json
```

Use a member's exact `path` or `id` from its inventory. `container-read` requires an explicit output filename and writes a JSON size/hash report to stdout. Member names are never interpreted as filesystem destinations. Output guards protect the Pak or TOC and all referenced UCAS partitions, including equivalent paths and hard links.

`container-hash` reads every live member and reports SHA-256 over its decoded bytes. For IoStore these are measured fingerprints, not validation against the stored BLAKE3 hashes. It also retains the inventory's integrity diagnostics. Compressed reads require an explicitly selected codec; inventory does not decompress payloads. `container-diff` reads all live content on both sides, even additions/removals, and returns 0 for identical, 1 for changes and 2 if any required read fails. A codec or read failure does not produce an equality result.

`ContainerEntry::stored_bytes` for an IoStore chunk includes the full physical blocks touched by that chunk. Shared or partially consumed blocks can therefore be counted more than once across entries. `size` is the exact reconstructed chunk/member byte count. `offset` is virtual for IoStore and the physical payload-header location for Pak.

## Embed

```cpp
#include <cyuediff/container.hpp>

cyuediff::ContainerOptions index_options;
index_options.max_entries = 100000;
auto input = cyuediff::Container::open("Game-Windows.utoc", index_options);

cyuediff::ContainerReadOptions read_options;
read_options.max_payload_bytes = 64ull * 1024 * 1024;
// Optional host-owned module; omitted for copy blocks.
read_options.decoder = cyuediff::container_block_decoder(
    cyuediff::load_block_decoder("my_codec.dll"));
auto bytes = input.read(0, read_options);
```

The named `ContainerBlockDecoder` callback must respect the output capacity and return the exact decoded size. Hosts may implement additional methods themselves. Neither compression method numbers in a TOC nor Pak indexes can be passed directly to the bulk-codec numeric ABI. No codec is discovered automatically.

A minimal installed-SDK consumer lives in `examples/container_consumer`. It inventories a container and hashes its first member, using an uncompressed input so that no codec is required.

## Bounds, cancellation and integrity

Indexing defaults to at most 128 MiB of serialized index bytes, one million entries, four million compression blocks, 64 KiB strings and directory depth 128. Pak's primary and full directory index byte counts share the index limit. Reading defaults to 256 MiB each for decoded output/stored Pak payload and 16 MiB per compressed/decoded block. These are input/buffer limits, not an aggregate heap reservation: metadata, byte vectors and decoding scratch space coexist. Hosts should lower them for their workload.

Structural validation rejects invalid sizes/offsets, missing partitions, invalid method references, duplicate IDs/paths, invalid UTF-16, directory cycles, unsafe relative names, unreachable directory entries and payload/index disagreements. Pak primary/full index SHA-1 values are checked during open; the payload header and stored member-byte SHA-1 are checked before returning a decoded Pak member. SHA-1 checks detect corruption and do not establish authenticity. The unused Pak path-hash index is not hashed. IoStore's stored BLAKE3 chunk hashes and RSA signatures are not verified; this limitation appears in inventory diagnostics.

Both open and read accept `OperationControl`. Cancellation propagates as `Error("cancelled")` during parsing, reads, hashing, codec boundaries and progress callbacks. Open does not retain callback closures. A codec call itself must return before cancellation can proceed.

Container descriptors are immutable and shareable; reads use local file handles and scratch vectors. Source files must remain stable for the operation. Size changes since indexing are rejected, but same-size concurrent edits are not a supported snapshot mechanism. Reopen after replacing a build. There is no container decode cache yet; shared blocks may be decompressed repeatedly for different chunks.

## Validation

Native fixtures cover both accepted Pak versions and all accepted TOC versions, compact/nonencoded entries, multi-block compression, unaligned/shared ranges across UCAS partitions, tombstones, Unicode names, limits, cancellation, truncation, cyclic directories, integrity mismatches and explicit signature limitations. CLI fixtures cover exit codes, full reads/diffs and output protection including hard links. Python is used for the CLI CTest suite when available; native Core tests have no Python runtime dependency.

Run the independent local corpus check with explicit inputs:

```powershell
python scripts/validate_containers.py --input D:/build/Game-Windows.pak --input D:/build/Game-Windows.utoc --input D:/build/global.utoc --codec D:/local/my_codec.dll
```

The script independently parses indexes and reconstructs every member through a Python byte reader, using the same explicitly supplied codec implementation for decompression. It compares all decoded SHA-256 values to Core, checks self-diffs and extracted samples, rejects writes onto original sources/truncated copies, and verifies original file hashes before/after. It is an independent container parser, not an independent codec implementation. Reports and sample bytes remain in an ignored workspace build directory; no proprietary input is distributed.
