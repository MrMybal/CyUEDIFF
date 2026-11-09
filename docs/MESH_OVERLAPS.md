# Stored skeletal vertex overlaps

Post-0.181 source builds read the UE 5.3 Windows skeletal section duplicate-vertex tables when the selected producer retained them. These tables describe section-local render vertex correspondences used for tangent seam accumulation. Core does not run Unreal or evaluate the runtime tangent shader.

## Typed Core contract

`CookedSkeletalSection::overlaps` is an immutable `MeshSectionOverlaps` shared with `MeshGeometry::vertex_overlaps`. It preserves the flat uint32 `indices` and each native `(length, offset)` window, section/base/vertex identity and SHA-256 of both complete serialized arrays. The native format is two ordinary count-prefixed arrays; the selected profile determines their element widths. No byte search, guessed stride header or runtime GPU allocation is used.

Each window resolves only section-local vertex IDs; `neighbors(render_vertex)` returns global render vertex IDs in stored order. Dimensions, reference windows and used IDs are checked. The reader retains unused allocation words, including the one-word no-overlap dummy. They do not create semantic neighbors. It does not infer geometric coincidence, symmetry, welded topology or runtime shading. Metadata explicitly reports those unverified/evaluation limits. Metadata-only reads retain typed section queries.

```cpp
#include <cyuediff/cooked_skeletal.hpp>

auto mesh = cyuediff::read_zen_skeletal_mesh(container, entry, zen, read, options);
for (const auto &section : mesh.lods.at(0).sections) {
    if (section.overlaps && section.vertices) {
        auto neighbors = section.overlaps->neighbors(section.first_vertex);
        auto raw = section.overlaps->to_json(true);
    }
}
```

A stripped table is unavailable rather than a verified empty relation. A loaded empty relation still has the native allocation dummy and zero-length windows. Counts, read/output bytes, aggregate stored indices and aggregate resolved references are bounded; the default index/reference cap is 12,000,000 across loaded LODs. Bounding resolved references as well prevents shared windows from expanding a small buffer into unbounded comparison output. Mapping reads, validation and queries are cancellable. Invalid queries report `mesh-overlaps-selection`.

## Differences and desktop

Existing `zen-skeletal`, `zen-skeletal-diff` and the installed Core-only sample expose the new projection with the [same explicit producer/schema context](COOKED_SKELETAL.md#cli-and-sdk). `to_json(true)` includes both raw arrays beneath each loaded section's `vertexOverlaps`. Geometry projects `vertexOverlaps` with scope `stored-section-vertex-overlaps` and global render vertex identities. Neighbors are sorted for comparison, preserving multiplicity: rearranging the native buffer/windows or neighbor order does not invent a relation change, while raw hashes retain those encoding differences. No cross-revision remapping is inferred when render vertex identities change.

Compared sections must have stored tables on both sides. Unavailable sections are masked and reported as incomplete, while known relation changes in common sections remain visible. A known empty table can prove removed neighbors; stripped data cannot. Existing geometry comparisons without this additional stored projection keep their previous scope. Native package/binary differences remain useful for omitted or changed native data.

Desktop mesh wireframes highlight edges incident to a changed known overlap relation. Unknown peer tables do not color false removals. Existing EN/FR mesh controls and persisted cameras/workspaces apply; raw tables and scoped differences remain available in the native reports. This feature does not add a tangent solver or a dedicated overlap-list editor.

## Independent validation and limits

Original fixtures cover original/sparse/empty/repacked relations, nonzero section bases, malformed counts/windows/IDs, aggregate raw and resolved budgets, cancellation and truncated known-native prefixes. CLI/desktop tests verify semantic changes, encoding-only equality, unavailable peers and protected outputs. Optional `overlapsReference` in the skeletal validation manifest checks every raw array and decoded serialized byte against the actual Engine serializer, then independently resolves each semantic relation using strict integer identity.

Private actual Engine examples are rebuilt from authorized copies, including an original UV variant that changes the cube's split render vertex count. The optional original reference adapter reads the Engine's CPU tables and calls its actual native serializer; it remains outside the runtime/SDK dependency graph. No private assets, generated native buffers or Engine implementation are distributed. See [validation evidence](VALIDATION.md).

The selected format does not cover other Unreal/platform producers, further custom vertex formats, physical cloth asset/configuration data or runtime deformation/tangent evaluation. The published 0.181 desktop archive predates this feature and remains unchanged.

Finite named custom attributes have a separate [stored value contract](VERTEX_ATTRIBUTES.md).
