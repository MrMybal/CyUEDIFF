# Cooked Texture2D native pixels

Post-0.181 development adds a reusable, explicit UE 5.3 reader for editor-stripped cooked `Texture2D` BulkData. It runs without Unreal Editor or engine libraries. A matching schema context must successfully decode the selected object's property region; the native reader begins at that exact consumed boundary. It never scans a package for plausible image headers.

## Core API

```cpp
#include <cyuediff/cooked_texture.hpp>
// zen.profile, script_objects and schema_context are selected explicitly by the host.
auto reader = cyuediff::CookedTextureReader::open(container, package_entry, zen, container_reads);
auto texture = reader.read(); // Sole non-template Texture2D, or pass an export index.
auto rgba = cyuediff::decode_cooked_mip(texture.platforms.at(0), 0);
auto samples = cyuediff::decode_cooked_mip_hdr(texture.platforms.at(0), 0);
```

The move-only worker session retains one verified package parse and a bounded cache of complete external chunks. Further exports and mip reads reuse that snapshot. Open a new session on reload; use separate sessions on separate workers. The convenience `read_zen_texture2d` function opens one session for one object.

The complete native window is validated before returning a texture: UObject GUID, both strip-flag records, cooked/mip-provider markers, package FNames, relative format skip offsets, derived-data placeholders, platform dimensions and packed slice flags, format FString, first mip, mip count, bulk-map indices, dimensions, virtual marker and final terminator. Supported raw/BC formats require exact mip byte footprints. Whole native tails, unknown layouts and unsupported serializers remain unavailable rather than guessed.

Inline bytes are taken at the serialized cursor; their bulk-map reload offset is not used as a guessed file position. External references preserve the export bundle's package ID and segment index and select ordinary, optional or mapped chunk types. Duplicate optional data uses its stored duplicate offset when that chunk exists, with the ordinary bulk fallback otherwise. UE 5.3's reserved bulk-map word is distinguished from UE 5.5's later cooked-index byte and padding.

Every used external chunk passes through `Container::read` in full and must match its stored BLAKE3-160 source hash before a mip subrange is used. Missing chunks retain explicit unavailable mip records, allowing a resident lower mip to remain useful. Integrity errors propagate. Inventory mode (`load_payloads=false`) retains descriptors without claiming external bytes were read or verified; `chunkSourceHash` is their stored expected identity, while `loaded` and the mip SHA-256 describe actually prepared bytes.

Limits cover package/chunk reads, aggregate declared mip payloads, dimensions, formats and mip counts. The complete external chunk is charged to the read budget even if only a small range is requested. GPU conversion has its own pixel/output budget and cancellation controls. Stored samples and display conversion are documented in [GPU_TEXTURES.md](GPU_TEXTURES.md).

## CLI

```powershell
cyuediff zen-texture project.utoc CHUNK_ID --cooked-profile ue5.3 --schema context.json --script-objects global.utoc
cyuediff zen-preview project.utoc CHUNK_ID --cooked-profile ue5.3 --schema context.json --script-objects global.utoc --mip 0 --out texture.png
cyuediff zen-image-diff before.utoc BEFORE_ID after.utoc AFTER_ID --cooked-profile ue5.3 --schema context.json --script-objects global.utoc --heatmap changed.png --out pixels.json
cyuediff zen-hdr-diff before.utoc BEFORE_ID after.utoc AFTER_ID --cooked-profile ue5.3 --schema context.json --script-objects global.utoc --out samples.json
```

Explicit host codecs use `--codec`. Comparisons accept independent `--after-schema`, `--after-script-objects`, `--after-cooked-profile` and export/platform indices. `--export-index` selects an object, `--platform` selects a format, and pixel commands use `--mip`. Multiple format choices require selection. `zen-preview` accepts `--exposure` for floating samples. Unsupported options fail explicitly.

`zen-image-diff` compares RGBA8 stored-channel previews. `zen-hdr-diff` compares floating samples before tone mapping and also preserves full unsigned 16-bit sample precision. Thresholds and changed bounds use the existing image/HDR contracts. Reports retain dimension fields plus `beforeTexture`/`afterTexture` provenance. Output aliases to container files, schemas, mappings, script tables and codecs are rejected.

A package-only `zen-diff` can be identical while an external bulk texture changed. The pixel commands follow those references and detect the changed pixels. Equality concerns the selected decoded mip and threshold, not equality of every property or the complete container.

## Desktop

Inspect a cooked member with its explicit profile, global script TOC and schema context. **Cooked pixels / Pixels cooked** lists prepared objects, formats, mipmaps and before/after sides. The CyUI image viewport provides fitting, actual pixels, zoom/pan, changed-pixel masks and fitting to the changed region. HDR display uses the existing 0 EV Reinhard/sRGB tone map; comparisons use original floating samples. BC5 keeps its stored RG channels with zero blue; normal-Z reconstruction is explicit future work.

Document preparation and pixel comparisons run on the worker. The render thread uploads prepared images; drawing performs no file I/O. Document output budgets bound previews, and failures remain in texture diagnostics. Comparisons pair exact object path, format and stored mip ordinal. Workspace files retain the pixels chapter, selected side/mip and viewport state; old workspaces remain readable. Launch validation can select `--container-tab pixels`.

## Evidence and limits

Original fixtures cover inline, ordinary/optional/mapped/duplicate external bulk, verified corruption, missing chunks, every truncated native prefix, markers, references, dimensions, budgets, cancellation, session reuse and moved ownership. CLI tests include a byte-identical package with a changed external mip, guarded outputs, full-precision comparison, EN/FR native binding and workspace reopening.

Independent real validation covers 21 authorized UE 5.3 textures, 107 mipmaps, 15 external mip references and 5,854,628 channels. A separate Python native-window reader checks boundaries, fields, mip bytes and bulk selection; Pillow checks all preview pixels, with a maximum observed difference of 1/255. All source fingerprints remain unchanged. A controlled private copy changes one compressed block of a real external icon mip: exactly 16 pixels change in the independently expected 4 by 4 region. Reports and host-owned samples remain ignored build artifacts.

This reader does not yet cover native UE 5.5 texture bodies, cooked virtual texture tiles, derived-data references, custom mip providers, platform-specific packed mip tails, console swizzles, arrays, cubes, volumes, legacy inline bulk metadata or compressed legacy bulk records. A texture with a VT-like name can still serialize ordinary 2D data; coverage follows its bytes. Mapping identity does not certify all build compatibility or reconstruct archetype defaults. Other cooked asset families remain separate workstreams.

The native result retains the exact global script-table SHA-256 alongside package/schema fingerprints and export selection. This records the interpretation context independently for each comparison side.
