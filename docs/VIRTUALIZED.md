# Local virtualized editor payloads

The reusable Core can read an editor texture or modern static MeshDescription source whose bulk metadata reports `storage: "virtualized"`. Select a **filesystem payload store explicitly**. The reader opens no editor, starts no engine process and contacts no remote service.

```cpp
#include <cyuediff/core.hpp>
#include <cyuediff/content_hash.hpp>
#include <cyuediff/virtualized.hpp>

const auto package = cyuediff::Package::open("Texture.uasset");
cyuediff::TextureDecodeOptions options;
options.virtualized_payload_root = "D:/PayloadStore";
const auto image = cyuediff::decode_texture(package.textures.at(0), 0, options);
```

`MeshDecodeOptions` and `BulkDecodeOptions` expose the same root. A host can call `decode_bulk_payload` directly to obtain verified raw bytes without a UI dependency. The installed `examples/virtualized_consumer` project builds against `CyUEDiff::Core` alone and demonstrates decoding and BLAKE3 fingerprints.

The backend uses the Unreal filesystem layout `aa/bb/cc/<remaining-34-hex-digits>.upayload`, derived from the lowercase, nonzero 40-digit FIoHash. `virtualized_payload_relative_path` constructs that relative path without I/O. `virtualized_payload_file` resolves an existing regular file inside the selected directory and rejects an escaping symbolic link or junction. Neither function searches an engine, project, user profile or registry for a store.

Each file must contain exactly one compressed buffer. The existing bounded decoder checks magic, header CRC32, identity, declared sizes and block coverage. Method 0 is stored raw; methods 3 and 4 use an explicitly supplied codec for compressed blocks. Raw-sized blocks need no codec. Every virtualized read recomputes the **full 256-bit raw BLAKE3** after decompression and checks both the buffer's 32-byte hash and the package's 20-byte content ID. Changing the hash tail while retaining the FIoHash is rejected. Missing stores, missing content, corrupted data, unsupported codecs, size limits and cancellation have distinct errors.

Local embedded buffers retain their existing SHA-256 change checks. Hosts can additionally set `verify_raw_hash = true` to require full raw BLAKE3 for those reads. Byte and file BLAKE3 helpers are available in `<cyuediff/content_hash.hpp>`; they check cancellation and report progress in 1 MiB chunks. The private portable implementation is pinned with upstream file hashes and checked against the official unkeyed test vectors. See [third-party notices](../THIRD_PARTY_NOTICES.md).

The payload root changes explicit byte decoding, not package discovery or the semantic snapshot. Package metadata continues to identify virtualized storage and its content ID even when no root is supplied. The package cache does not retain decoded payload bytes. Reads are bounded by `max_payload_bytes` (256 MiB by default); a caller must deliberately increase it for larger resources.

```powershell
cyuediff preview Texture.uasset --payload-root D:/PayloadStore --out texture.png
cyuediff image-diff Before.uasset After.uasset --payload-root D:/BeforeStore --after-payload-root D:/AfterStore
cyuediff geometry Mesh.uasset --payload-root D:/PayloadStore --codec Codec.dll
cyuediff mesh-diff Before.uasset After.uasset --payload-root D:/BeforeStore --after-payload-root D:/AfterStore --codec Codec.dll
```

`--after-payload-root` inherits `--payload-root` when omitted. Both selected directories are protected against output writes. The desktop exposes the same controls in English and French, retains them per document in workspace version 6 and supports migration from workspace versions 1–5. Desktop `--no-after-payload-root` disables inheritance for the second side; switching the comparison sides also switches their effective roots.

This backend covers local editor bulk resources. It does not implement Perforce, HTTP, DDC cache records, cooked platform textures, skeletal render/skin buffers, Nanite pages or arbitrary serializers. Oodle remains a separately supplied adapter. Reading a virtualized buffer does not add a native decoder for the raw format it contains.

`scripts/validate_virtualized.py` extracts existing buffers unchanged from read-only real editor assets, virtualizes the indexes of workspace copies, compares pixels and geometry, checks independent roots and damaged data, and optionally exercises desktop previews and workspace restart. These are controlled reader fixtures, not a claim of validation on an independently generated Unreal virtualized project. Original assets and extracted resources are excluded from Git and distribution.
