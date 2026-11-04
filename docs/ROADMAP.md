# Development status after 0.14

This tracks the full requested scope. A listed input or extension point does not imply that its data format has been implemented.

| Area | Implemented and checked | Remaining |
| --- | --- | --- |
| Visual comparisons | Stable Blueprint/material node outlines, stored material parameter comparison, source geometry edge colors, LDR/HDR pixel masks, typed property inspector | Further interaction and correspondence refinements |
| Unreal reading | Supported classic loose packages, tagged primitives/arrays/structs, stored map/set deltas, supported localized texts, native transforms/ticks, explicit C++ decoder registry | Unversioned schema loading, broader plugin-native layouts, cooked packages and Pak/IoStore readers |
| Advanced textures/meshes | LDR sources, finite raw FP16/FP32/RGBE HDR sources and mip selection, modern static MeshDescription source geometry | Virtualized source resolution, skeletal data/skin/animations, collision structures and Nanite payload decoding |
| Desktop | English/French, five themes, typed inspector, content browser, local Git/LFS comparisons, independent before/after contexts | Git history browser, multiple documents and persistent preferences |
| Runtime/reliability | Verified in-memory package LRU, cooperative cancellation/progress, bounded decoders, corruption tests, 150-package CyTrace corpus and real HDR sources | Additional backend-specific/cooked/virtualized corpora and performance work as those readers arrive |
| Distribution | MIT license, selected dependency notices, CPack CLI/desktop/SDK archives tested after extraction, prepared Windows/Linux CI with Gitleaks | Hosted CI execution after an explicit push; release publication/signing/installer if selected later |

The supplied CyTrace UE 5.3 build contains a version-11 Pak and version-5 IoStore TOCs with their UCAS files. These inputs are available locally for the next container/cooked implementation. No `.usmap` was found in that project tree. They have not yet been decoded by a CyUEDiff container backend. Source projects, containers, assets and generated reports remain outside Git.
