# Cooked cloth deformer mappings

The post-0.181 source-built UE 5.3 Windows skeletal reader decodes stored cloth mapping data without Unreal, a GPU or a simulation runtime. It exposes typed section bindings, render-to-simulation triangle correspondences and the matching packed render buffer. Referenced physical simulation meshes and runtime cloth state remain outside this projection.

## Core contract

Use [the cooked skeletal reader](COOKED_SKELETAL.md) with matching Zen/script/USMAP context and explicit producer mobile-LOD/color facts. No byte search or guessed native boundary is used. Inline and streamed LODs retain the existing whole-used-chunk integrity verification.

Each `ClothSection` preserves its section ID, render vertex range, signed clothing asset index, physical asset LOD, four native GUID words and each stored LOD-bias mapping array. One or five influences per render vertex are supported within the selected producer profile; the explicit count budget can be adjusted by a host. The mapping contains three float4 vectors (position, normal endpoint and tangent endpoint barycentric coordinates/distances), four uint16 source fields, a float weight and a uint32 alignment word. The fourth source field is a static/simulated contribution flag, including the `0xffff` fixed/skinning-only sentinel; it is not a fourth simulation vertex ID.

The separate native cloth vertex buffer uses 64-byte records and a section index table containing first render vertex, mapping offset and LOD-bias stride. The reader checks the cached streaming dimensions/index table, buffer windows and counts, per-vertex cardinality and every record against the corresponding section array. Unclothed section index entries must be the stored zero dummy. Active coefficients and weights must be finite. Source triangle indices are preserved, but their bounds against a physical simulation mesh are not certified: metadata reports `simulationMeshBoundsVerified: false`.

The typed immutable result is shared by `CookedSkeletalLod::cloth` and `MeshGeometry::cloth`. Metadata-only reads retain focused typed queries. `to_json(true)` includes all stored mapping values and alignment words; metadata-only output retains counts, bindings, index windows and SHA-256. Inactive producer fields can be uninitialized; non-finite inactive values remain available as explicit `nonFiniteBits`, rather than becoming usable coefficients.

```cpp
#include <cyuediff/cooked_skeletal.hpp>

auto mesh = cyuediff::read_zen_skeletal_mesh(container, entry, zen, read, options);
if (auto cloth = mesh.lods.at(0).cloth) {
    auto metadata = cloth->to_json();
    auto values = cloth->vertex(0, 0, 0); // Section, LOD bias, render vertex.
    auto raw = cloth->vertex(0, 0, 0, true); // Includes padding/inactive bytes.
}
```

Selections must exist; invalid focused selections return `cloth-selection`. Section/vertex counts, total mapping records, per-vertex influences, read/output bytes and cancellation are bounded. The default aggregate mapping cap is 4,000,000 records across both section arrays and flat buffers; default per-vertex influence cap is five. Progress is reported during mapping reads and long validation/projection loops remain cancellable.

## Differences and inspection

The geometry `cloth` projection has scope `stored-cloth-deformer-mapping`. It compares section binding identities, LOD biases, render vertex identities and active mapping coefficients/source fields/weights. Alignment bytes and inactive unused coefficients are excluded from semantic changes; raw metadata hashes still detect them. This projection does not normalize weights, reconstruct normals, apply a cloth blend or evaluate a simulated pose. A known absent mapping scope can establish additions/removals; an unavailable peer mesh remains unknown in the desktop.

```text
cyuediff zen-skeletal BUILD.utoc PACKAGE_ID [explicit skeletal context]
cyuediff zen-skeletal-diff BEFORE.utoc ID AFTER.utoc ID [explicit skeletal context]
```

See [the required context flags and independent after options](COOKED_SKELETAL.md#cli-and-sdk). The specialized geometry diff covers this selected decoded projection. Use Zen/package differences as well to inspect changed physical assets, settings and opaque native tails outside it.

In **Cooked meshes → Cloth mappings**, select a section and LOD bias, search vertices or values, filter changed entries and navigate previous/next changed vertices. Selecting a vertex opens bounded typed before/after binding and influence values. Wire previews highlight edges incident to changed mappings. The clipped list supports large vertex tables without flattening unrelated records into the focused property inspector. Section, bias, vertex, filter, change-only mode and property selection persist with the cooked mesh in the current workspace format. English is the default; all new controls and scope messages have French translations. The existing 0.181 archive predates this source feature.

## Independent evidence and limits

Original numerical/cooked tests exercise single/five influence maps, multiple biases, native windows, alignment-only differences, inactive non-finite bytes, corruption/truncation, budgets, cancellation and focused selection. Widget/desktop tests cover EN/FR, five themes, wide/narrow layouts, 40,000 vertices, before/after identity and saved state. Private actual UE 5.3.2 examples are prepared through the real clothing factory/binding APIs and cooked into independent IoStore outputs. Structured API fields and every exported 64-byte mapping record are checked against a separately installed Core-only consumer. The optional skeletal validator accepts `clothReference` beside its geometry reference.

Unreal's ordinary numeric JSON can lose the sign of zero. The validator treats signed zero equivalently in that secondary API representation, while requiring the decoded Core buffer to reproduce the **exact** bytes from the actual exported native record serializer, including signed zeros and padding. GUID/index identities require exact integer equality.

This selected format covers stored render mappings. Physical cloth asset/configuration/transition payload decoding, simulation vertex bound checks, deformation/collision evaluation, further custom attribute formats and additional producers/platforms remain separate work. Supported finite vertex attributes have their own [contract](VERTEX_ATTRIBUTES.md). Retained duplicate-vertex tables have their own [stored overlap contract](MESH_OVERLAPS.md). Private assets, Engine implementation, generated buffers and reference outputs are not distributed.


Post-0.182 source adds [explicit hybrid struct bindings](HYBRID_STRUCTS.md) for selected mapped physical cloth properties, native tether values and exact raw transition records. This is a separate generic projection; it does not change this render-mapping model’s `simulationMeshBoundsVerified: false` contract. Dedicated physical geometry/bounds and interpreted transition work remain open.


## Physical data projection in 0.184 source

The separate [physical-cloth Core/CLI model](PHYSICAL_CLOTH.md) now supplies physical positions/topology/bones/tethers, interpreted native transitions and reference-pose wrap queries. Explicit base render GUID/LOD binding can certify source-vertex bounds against that model; biased sources remain explicitly selected. The existing standalone render-mapping model keeps its original unbound status until a host supplies the physical resource. Dedicated physical desktop binding, defaults and simulation remain separate work.
