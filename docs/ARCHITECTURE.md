# Architecture

The central contract is a deterministic snapshot of serialized objects, independent of UI, Unreal and Git. Object identities use their outer/name paths; package table indices resolve to stored reference identities. Properties retain types and decoded values. Unsupported ranges remain visible through their byte counts, SHA-256 fingerprints and diagnostics.

```text
Host / CLI / CyUI views
          |
  Packages, snapshots and comparisons
     /           |             \
Bounded I/O  Serialization   Project/engine context
             profiles and   Content mounts and
             explicit       dependency locations
             descriptions
```

## Core readers

`archive.cpp` supplies bounded random access, FString/byte-string reads and hashing. `package.cpp` reads supported classic package summaries, names/imports/exports and tagged properties. Recent export script windows bound the property traversal. A failed read does not search arbitrary bytes for another plausible property stream.

`schema.cpp` reads explicit USMAP definitions and supported unversioned values. `schema_context.cpp` validates immutable cooked class/schema/offset bindings. `zen.cpp` reads supported cooked package headers, object tables and explicitly bound property regions. A mapping fingerprint identifies a selected mapping; it does not establish that it matches a particular build. Archetype defaults are not reconstructed.

`decoders.cpp` provides immutable exact-target registries and archive/custom-version profile selection. Host C++ callbacks read bounded sequential windows and return identified JSON. `binary_layout.cpp` implements the same extension mechanism from bounded JSON descriptions: named records, fixed/dynamic arrays, optional markers, primitives and package references. It also supplies explicitly bound native structures to the unversioned reader. Description files contain data and do not load native code. Missing or ambiguous native profiles remain opaque.

`string_encoding.cpp` provides explicit, portable ASCII, Latin-1, Windows-1252 and UTF-8 conversion. Selected encodings participate in cache/context identities and before/after configuration. No process locale or statistical encoding detection is used.

`container.cpp` inventories supported Pak/IoStore containers and reconstructs member bytes. This layer is separate from interpreting a cooked asset's native body. `codec.cpp` loads only an explicitly selected host codec; optional adapters are not discovered from project files or bundled into Core.

`project.cpp` describes project/plugin/engine content mounts. Selecting an engine root supplies context and content locations; it does not extract serializers from arbitrary plugin C++ or instantiate their classes. Dependency lookup locates files without recursively loading every referenced asset. `catalog.cpp` inventories metadata before asset decoding. `cache.cpp` keeps verified package reads in a bounded LRU, distinguishing reader/decoder/encoding profiles.

## Semantic projections

Blueprint and material readers project stored editor graphs through stable node GUIDs, preserving pin/parameter changes. Texture readers describe and fingerprint source payloads; image/HDR readers decode supported previews and compare samples. MeshDescription readers expose supported static source geometry. Collision readers project reflected stored shapes. Skeleton and animation readers expose the implemented raw reference prefixes, stored retarget poses, attachments, metadata, source keys and curves.

A specialized projection carries its coverage. A generic decoded JSON object does not automatically produce a texture, mesh, animation projection or a specialized UI view. Native skinning, Nanite, compressed animation, virtualized source resolution and broader cooked visual payloads remain separate readers on the [roadmap](ROADMAP.md).

Snapshots omit copies of large bulk payloads. Packages retain decoded property values and selected tables; the API is not fully lazy per export. Payload decoding is explicit. Input/work/depth/text budgets and cooperative cancellation are shared where documented. These component limits are not a bound on the process's entire heap.

`diff.cpp` compares normalized trees while preserving binary evidence separately. Stored property/collection differences do not imply runtime equality or reconstruction of inherited defaults. Specialized comparisons expose their own completeness and correspondence rules.

## Binary reconstruction

The `binary_delta*` modules implement a separate byte-level layer: bounded block matching, JSON/compact manifests, compact output sinks, composition and verified reconstruction. Callback/file-backed generation avoids buffering entire endpoint files. Literals, matching indexes and manifest parsing still use resident storage. A byte-exact reconstruction does not certify semantic understanding of the Unreal asset. Object/block servers, retention and remote transport are outside this layer. See [the binary contract](BINARY_DELTA.md).

## Optional adapters and hosts

`CyUEDiff::Git` owns local revision/LFS materialization and its temporary files. It resolves commits without checkout, filters or fetch, verifies available local LFS objects and supplies provenance. Independent before/after `ReadOptions` support different engine/plugin wire descriptions. Historical project metadata and current local engine content remain distinct sources.

`CyUEDiff::UI` depends on Core and host-provided CyUI. Views receive immutable prepared objects; drawing does not open assets or descriptions. Native descriptions appear in the existing typed inspector and general diffs. English is the default UI language; French is built in.

`apps/desktop` owns the Windows/Direct3D host, background document queue, GPU resources, file dialogs and session/workspace/preferences persistence. It supports independent loose/Git contexts and cooked schema contexts. Workspace format five stores per-document native descriptions; older workspace readers retain their default decoding. Codec executables are not persisted by workspace files. The current native dialog layout and further cooked visual bindings still need work.

Core installs independently through `find_package(CyUEDiff)`. The bundled [layout consumer](../examples/binary_layout_consumer) builds against the installed Core alone. Extracted archive validation checks public consumers and desktop startup/persistence. Hosted CI is prepared but has not been executed for these local commits.

## Format and validation boundaries

Coverage follows serialized file/custom versions and explicit wire profiles, not an engine marketing version alone. Unknown future headers are rejected before reading an incompatible remainder. A declarative schema is a supplied format description, not automatic discovery or proof of compatibility.

Local engine sources are read-only format references. Host assets, mappings, codecs and generated reports are not redistributed. Original fixtures, independent byte/codepage checks and local supplied corpora are recorded in [validation](VALIDATION.md); outstanding formats and real matching-build mapping validation remain explicit in [the roadmap](ROADMAP.md).

Explicit local virtualized bulk resolution is shared by texture/HDR and modern MeshDescription decoders. `BulkDecodeOptions::virtualized_payload_root` selects a filesystem store and virtualized reads verify the full raw BLAKE3 against the buffer and package identity. The private portable BLAKE3 objects are embedded in Core with prefixed symbols; installed consumers need no additional link target. See [VIRTUALIZED.md](VIRTUALIZED.md).
