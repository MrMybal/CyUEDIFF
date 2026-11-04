# Development status after 0.16

This tracks the full requested scope. A listed input or extension point does not imply that its data format has been implemented.

| Area | Implemented and checked | Remaining |
| --- | --- | --- |
| Visual comparisons | Stable Blueprint/material node outlines, stored material parameter comparison, source geometry edge colors, LDR/HDR pixel masks, typed property inspector | Further interaction and correspondence refinements |
| Unreal reading | Supported classic loose packages, tagged primitives/arrays/structs, stored map/set deltas, supported localized texts, native transforms/ticks, explicit C++ decoder registry, Pak 10/11 and IoStore TOC 3–5 container byte readers/diffs, cooked Zen names/classes/object tables and opaque body diffs | Unversioned schema loading, broader plugin-native layouts, cooked property/native payload decoding, further container versions and encryption |
| Advanced textures/meshes | LDR sources, finite raw FP16/FP32/RGBE HDR sources and mip selection, modern static MeshDescription source geometry | Virtualized source resolution, skeletal data/skin/animations, collision structures and Nanite payload decoding |
| Desktop | English/French, five themes, typed inspector, content browser, local Git/LFS comparisons, independent before/after contexts | Git history browser, multiple documents, persistent preferences and container browsing |
| Runtime/reliability | Verified in-memory package LRU, cooperative cancellation/progress, bounded decoders, corruption tests, 150-package CyTrace corpus and real HDR sources | Additional backend-specific/cooked/virtualized corpora and performance work as those readers arrive |
| Distribution | MIT license, selected dependency notices, CPack CLI/desktop/SDK archives tested after extraction, prepared Windows/Linux CI with Gitleaks | Hosted CI execution after an explicit push; release publication/signing/installer if selected later |

The supplied CyTrace UE 5.3 build validates a version-11 Pak and version-5 IoStore TOCs/UCAS files: all 6,219 live members decode and match an independent container reader. Zen validation additionally reads 3,507 cooked package headers and 7,502 exports, resolves 31,616 global script objects and locates opaque body changes by object. Cooked property values and native payload semantics remain unimplemented. No `.usmap` was found in the project tree. Source projects, containers, assets and generated reports remain outside Git. See [container contracts](CONTAINERS.md) and [cooked Zen contracts](ZEN.md).
