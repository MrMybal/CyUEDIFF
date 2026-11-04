# Raw reference skeletons

The reader retains stored raw bone hierarchy and local reference transforms without launching Unreal. No engine-owned source or asset data is redistributed.

## Bounded native hierarchy and local reference pose - 0.83

Classic Skeleton exports with file versions UE4 522 / UE5 1013 expose a bounded raw reference-skeleton prefix: bone FNames, parent indices, optional editor export names, double-precision local transforms and the stored name/index map. Counts, parent order, duplicate identities, pose/map consistency, finite values, byte/text/work limits and cancellation are checked. Unsupported profiles and failures retain the entire native suffix opaque; successful reads keep all later retarget/SmartName/marker payloads opaque too. Native skeleton decoding can be disabled, with a separate cache profile. Full-package fixtures and every truncated native prefix are covered; the filtered prefix is additionally tested through the bounded archive adapter. No skinning, animation, virtual-bone rebuilding or engine defaults are evaluated.

## Named skeleton snapshots and structural diffs - 0.84

Skeleton snapshots key raw bones by their exact stored FName and use named parents, while optional sourceIndex fields retain inspection ordering. The comparison omits those indices, so sibling source reordering alone does not change the named projection; ordinary package diffs still retain serialized array changes. Local pose components, exported names, UObject properties and remaining native fingerprints stay visible. Missing native prefixes report incomplete, malformed host models fail, and object/bone/node/text/depth budgets and cancellation remain explicit. General package inspection adds skeletons and comparisons add skeletonChanges.
