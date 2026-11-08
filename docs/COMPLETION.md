# Completion criteria

The product goal is the originally requested reusable Unreal inspection/diff engine and its separate bilingual CyUI desktop. Completion requires implemented public paths and evidence; a decoder extension point, fixture-only parser or opaque fingerprint does not by itself complete an asset format.

## Acceptance workstreams

- [ ] Native/plugin structures: exact build/version profiles, reusable typed values, named diffs and source/cooked integration; demonstrate host/plugin layouts and document serialization that cannot be discovered from stored metadata.
- [ ] Cooked assets: map property and native boundaries explicitly, read supported platform textures/geometry/physics/animation payloads, and bind those projections in CLI and desktop with independent before/after provenance.
- [ ] Advanced geometry: independently validate modern skeletal sources/morphs, decode relevant legacy/cooked skin and morph formats, and implement/validate Nanite data inspection and geometry correspondence for the selected supported profiles.
- [ ] Texture and animation coverage: supported platform pixels and compression, source and compressed animation, named frame/curve differences and truthful unavailable channels.
- [ ] Containers and reliability: verify stored integrity where specified, supply supported decompression paths, preserve bounded I/O/cancellation and reject damaged input before publishing decoded results. Validate all supported container versions and storage arrangements.
- [ ] Corpus: use authorized local engine/project/build inputs read-only, acquire or derive matching mapping evidence, compare against independent implementations where possible and retain hashes before/after. Synthetic fixtures remain identified as such.
- [ ] Desktop: every completed reader has a practical inspection/diff path, EN/FR labels, useful missing/unsupported diagnostics and retained context/navigation. Complete outstanding dialog/layout and cooked-view bindings.
- [ ] Delivery: locally verified Windows executable/CLI/SDK archive with matching notices/manuals, extraction/startup/SDK-consumer checks, security scan and accurate platform/version/release status. Public release publication follows explicit owner authorization.

## Current facts

Version 0.181.0 already supplies the foundational Core, optional Git adapter, source readers, declarative native layouts, container and Zen tables, explicit schema reads, source visuals, local virtualized resources, multiple documents, workspaces and file/block reconstruction. The public website, bilingual README and MIT license are published. Hosted Windows/Ubuntu Core Git-on/off and secret checks pass at commit d0ad51c. The detailed format contracts and remaining reader coverage are tracked in ROADMAP.md.

## Active work

1. Completed locally: required IoStore BLAKE3-160 source integrity, with original/corrupt fixtures, 4,696 verified real chunks, CLI/desktop propagation, Core-only SDK validation and unchanged original inputs. See the post-0.181 validation record.
2. Add reusable serialized struct/class field definitions from supported native reflection metadata, with exact profile gates and public named inspection; use that evidence to reduce reliance on manually supplied schemas where the file actually contains the needed declarations.
3. Continue cooked native visual readers and their explicit property/bulk contexts, then independent skeletal/morph/Nanite validation and delivery closure.

Do not mark the overall goal complete while any required workstream remains open. Generated reports, host-owned samples and private paths stay under ignored build directories; commits contain only original code/fixtures and public documentation. Development commits remain local until another push is authorized.
