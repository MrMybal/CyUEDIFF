# Stored skeletal vertex attributes

Post-0.181 source builds decode named UE 5.3 Windows cooked skeletal render attributes without Unreal, a GPU or a shader runtime. Finite float32, float16 and normalized uint8 values with one to four components are supported. Names, channel/component/vertex identities and decoded values remain separate from native encoding and payload fingerprints.

## Core and native contract

`MeshVertexAttribute` owns immutable encoded bytes, exposes format/component/vertex metadata and resolves a focused `vertex(id)` vector. `MeshVertexAttributes` groups channels by their stored names. The cooked reader shares a loaded LOD's typed `vertex_attributes` with `MeshGeometry::vertex_attributes`; metadata-only reads retain typed queries. Raw payload access and SHA-256 preserve signed zeros and encoding differences. UNorm8 values resolve to the float32 normalized value of the stored byte; no shader meaning, interpolation, deformation or input re-quantization is inferred. Non-finite half/float inputs fail explicitly under this selected finite-value contract.

```cpp
#include <cyuediff/cooked_skeletal.hpp>

auto mesh = cyuediff::read_zen_skeletal_mesh(container, entry, zen, read, options);
if (auto attributes = mesh.lods.at(0).vertex_attributes) {
    for (const auto &entry : attributes->channels) {
        auto metadata = entry.second->to_json();
        if (entry.second->vertex_count()) {
            auto value = entry.second->vertex(0);
            const auto &encoded = entry.second->encoded();
        }
    }
}
```

The native collection stores names and explicit component count, selected pixel format, component/record widths and a bounded byte array. The reader requires matching component counts/widths, array byte stride and `render vertices × record stride`. It rejects unknown formats, duplicate names, inconsistent dimensions, non-finite values and truncated buffers before publishing a model. Inline names use the package FName map; streamed memory-archive names use bounded explicit FString decoding. The native counting archive excludes these name bytes, which are reported separately as `attributeNameBytes` and checked against the complete physical window. This preserves the existing named weight profile counting rule.

The default reader cap is 64 channels and 8,000,000 decoded components in aggregate across loaded LODs, with a 4,096-byte name cap and the existing read/output-byte budgets. The standalone primitive also bounds names, vertices and decoded output before publication. Reads, validation, projection and focused queries are cancellable. Invalid focused vertices report `mesh-attribute-selection`. Matching Zen/script/USMAP context remains mandatory; packages storing LOD attribute settings require an explicit matching `/Script/Engine.SkeletalMeshVertexAttributeInfo` reflected binding as well as the existing cooked per-platform descriptors. No schema or native boundary is guessed.

## Differences and desktop

Existing `zen-skeletal`/`zen-skeletal-diff` and the installed Core-only sample include this projection with the [same independent context flags](COOKED_SKELETAL.md#cli-and-sdk). Default metadata output lists channel names, encodings, dimensions and fingerprints. `to_json(true)` adds decoded vectors; the typed API retains exact encoded bytes. Geometry differences use scope `stored-render-vertex-attributes`, stored channel names, global render vertex IDs, component counts and decoded vectors. The semantic projection excludes storage encoding and byte hashes, so equal decoded values can match across supported encodings. Native/package differences still expose their storage changes. Render vertex identities are not remapped across topology changes.

A loaded empty channel inventory can establish additions/removals. An unavailable peer scope is masked and reported incomplete; it cannot prove that channels were deleted. Comparisons without this additional stored projection retain their existing geometry scope. Missing streamed mesh data continues to be unavailable.

In **Cooked meshes → Vertex attributes**, select a named channel and render vertex, search cached vertex/value text, filter changed entries or navigate previous/next changed vertices. The clipped list supports long channels, and the property inspector prepares only the focused vector. Storage format/component summaries remain visible separately from value changes. Changed known values highlight incident mesh edges; unknown peer values do not create false removals. Channel/vertex/filter/property state persists with the selected cooked mesh/LOD and its independent source context. English is the default, with French translations for the new controls and scope messages.

## Reference evidence and limits

Original native/numerical tests cover all twelve encoding/component combinations, metadata-only typed access, independent inline/streamed LODs, narrow/UTF-16 streamed names, encoding-independent equality, named scalar differences, unknown/empty scopes, ownership, cancellation, limits, malformed input and truncated known-native prefixes. UI/CLI tests cover focused values, EN/FR, five themes, wide/narrow layouts, a clipped 40,000-vertex list, independent members and retained state. The optional validator accepts `vertexAttributesReference` beside the geometry reference.

Private actual Engine meshes provide independently built scalar channels in all three formats, including a streamed LOD. The optional original adapter exports actual source-model values and the actual native collection serializer. A separate CPU-only original channel set uses the Engine encoder for one to four components, signed zero, half subnormal/range examples and normalized-byte clamping. These procedural channels are identified as synthetic rather than claimed as additional real asset revisions. Ordinary Engine numeric JSON can lose zero sign: only that secondary source-quantization comparison canonicalizes zero signs; decoded native bits and payload hashes retain exact signs.

The selected Engine's source-import utility copies only one source value per render vertex when transferring attributes. The real prepared assets therefore use scalar channels; vector encoding is validated independently through original fixtures and actual Engine-encoded CPU channels. Other Engine/platform profiles, non-finite channels, attribute-specific shader semantics, physical cloth assets/configuration and runtime deformation remain separate work. Private source assets, native buffers, Engine implementation and generated references are not distributed. The published 0.181 archive predates this source feature. See [validation evidence](VALIDATION.md).
